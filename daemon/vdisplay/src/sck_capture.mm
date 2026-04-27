// SPDX-License-Identifier: Apache-2.0
//
// ScreenCaptureKit-backed capture of a CGVirtualDisplay. See header for the
// thread / lifetime contract. This file is Objective-C++ because SCStream and
// SCStreamConfiguration are Objective-C APIs.
#include "fuvr/vdisplay/sck_capture.hpp"

#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>)
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#define FUVR_HAS_SCREENCAPTUREKIT 1
#else
#define FUVR_HAS_SCREENCAPTUREKIT 0
#endif

#include <atomic>
#include <memory>
#include <mutex>

namespace fuvr::vdisplay {

namespace {

#if FUVR_HAS_SCREENCAPTUREKIT

uint64_t machHostTimeNs() {
    CMTime t = CMClockGetTime(CMClockGetHostTimeClock());
    if (CMTIME_IS_INVALID(t) || t.timescale == 0) return 0;
    // CMTime nanoseconds: convert via the public CMTime API to dodge int128.
    CMTime nsScale = CMTimeConvertScale(t, 1'000'000'000,
                                        kCMTimeRoundingMethod_Default);
    if (CMTIME_IS_INVALID(nsScale)) return 0;
    return static_cast<uint64_t>(nsScale.value);
}

#endif  // FUVR_HAS_SCREENCAPTUREKIT

}  // namespace

#if FUVR_HAS_SCREENCAPTUREKIT

// Forward-declare the impl so we can hand it to the Objective-C output handler.
class SckCaptureImpl;

}  // namespace fuvr::vdisplay

API_AVAILABLE(macos(13.0))
@interface FuvrSckOutput : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, assign) fuvr::vdisplay::SckCaptureImpl* owner;
@end

namespace fuvr::vdisplay {

class API_AVAILABLE(macos(13.0)) SckCaptureImpl final : public SckCapture {
public:
    SckCaptureImpl(uint32_t displayId, uint32_t fps, FrameSink sink)
        : displayId_(displayId), fps_(fps), sink_(std::move(sink)) {}

    ~SckCaptureImpl() override { stop(); }

    bool init() {
        @autoreleasepool {
            output_ = [[FuvrSckOutput alloc] init];
            output_.owner = this;

            __block SCDisplay* found = nil;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            CGDirectDisplayID wanted = displayId_;

            [SCShareableContent getShareableContentWithCompletionHandler:
                ^(SCShareableContent* _Nullable content, NSError* _Nullable err) {
                    (void)err;
                    if (content) {
                        for (SCDisplay* d in content.displays) {
                            if (d.displayID == wanted) { found = d; break; }
                        }
                    }
                    dispatch_semaphore_signal(sem);
                }];
            // 5 s budget: SCShareableContent can stall briefly on first call.
            dispatch_semaphore_wait(sem,
                dispatch_time(DISPATCH_TIME_NOW, 5LL * NSEC_PER_SEC));

            if (!found) return false;
            display_ = found;

            SCContentFilter* filter =
                [[SCContentFilter alloc] initWithDisplay:display_ excludingWindows:@[]];

            SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
            cfg.width  = static_cast<size_t>(display_.width);
            cfg.height = static_cast<size_t>(display_.height);
            cfg.pixelFormat   = kCVPixelFormatType_32BGRA;
            cfg.queueDepth    = 4;
            cfg.minimumFrameInterval = CMTimeMake(1, static_cast<int32_t>(fps_));
            cfg.showsCursor   = NO;
            cfg.colorSpaceName = kCGColorSpaceSRGB;
            cfg.sourceRect    = CGRectMake(0, 0, display_.width, display_.height);

            stream_ = [[SCStream alloc] initWithFilter:filter
                                         configuration:cfg
                                              delegate:output_];
            queue_ = dispatch_queue_create("dev.fuvr.sck", DISPATCH_QUEUE_SERIAL);
            NSError* addErr = nil;
            BOOL ok = [stream_ addStreamOutput:output_
                                          type:SCStreamOutputTypeScreen
                            sampleHandlerQueue:queue_
                                         error:&addErr];
            return ok ? true : false;
        }
    }

    void start() override {
        @autoreleasepool {
            std::lock_guard<std::mutex> lock(mtx_);
            if (!stream_ || running_) return;
            running_ = true;
            [stream_ startCaptureWithCompletionHandler:^(NSError* _Nullable err) {
                if (err) {
                    // Stream failed to start; flip back to not-running so a
                    // future start() can retry. No exception path on macOS APIs.
                    (void)err;
                }
            }];
        }
    }

    void stop() override {
        @autoreleasepool {
            SCStream* s = nil;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (!running_) return;
                running_ = false;
                s = stream_;
            }
            if (!s) return;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [s stopCaptureWithCompletionHandler:^(NSError* _Nullable) {
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem,
                dispatch_time(DISPATCH_TIME_NOW, 2LL * NSEC_PER_SEC));
        }
    }

    void deliver(CMSampleBufferRef sb) {
        if (!sb || !sink_) return;
        if (!CMSampleBufferIsValid(sb)) return;
        CVImageBufferRef img = CMSampleBufferGetImageBuffer(sb);
        if (!img) return;
        // CVImageBufferRef and CVPixelBufferRef are toll-free in this direction.
        sink_(static_cast<CVPixelBufferRef>(img), machHostTimeNs());
    }

private:
    uint32_t            displayId_;
    uint32_t            fps_;
    FrameSink           sink_;
    SCDisplay*          display_  = nil;
    SCStream*           stream_   = nil;
    FuvrSckOutput*      output_   = nil;
    dispatch_queue_t    queue_    = nullptr;
    std::mutex          mtx_;
    bool                running_  = false;
};

}  // namespace fuvr::vdisplay

@implementation FuvrSckOutput
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen) return;
    if (self.owner) self.owner->deliver(sampleBuffer);
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream; (void)error;
}
@end

namespace fuvr::vdisplay {

std::unique_ptr<SckCapture> SckCapture::create(uint32_t displayId,
                                               uint32_t fps,
                                               FrameSink sink) {
    if (@available(macOS 13.0, *)) {
        auto impl = std::make_unique<SckCaptureImpl>(displayId, fps, std::move(sink));
        if (!impl->init()) return nullptr;
        return impl;
    }
    return nullptr;
}

#else  // !FUVR_HAS_SCREENCAPTUREKIT

std::unique_ptr<SckCapture> SckCapture::create(uint32_t, uint32_t, FrameSink) {
    return nullptr;
}

#endif

}  // namespace fuvr::vdisplay
