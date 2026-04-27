// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/capture.hpp"

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <AVFoundation/AVFoundation.h>
#import <mach/mach_time.h>

#include <atomic>
#include <vector>
#include <mutex>

namespace fuvr::audio {

namespace {

uint64_t hostTimeToNanos(uint64_t hostTime) {
    static mach_timebase_info_data_t tb = {0, 0};
    if (tb.denom == 0) mach_timebase_info(&tb);
    return (hostTime * tb.numer) / tb.denom;
}

} // namespace

} // namespace fuvr::audio

API_AVAILABLE(macos(13.0))
@interface FuvrAudioStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
@end

@implementation FuvrAudioStreamOutput {
@public
    fuvr::audio::CaptureCallback _cb;
    std::vector<int16_t> _scratch;
}

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeAudio) return;
    if (!CMSampleBufferIsValid(sampleBuffer)) return;

    CMItemCount frames = CMSampleBufferGetNumSamples(sampleBuffer);
    if (frames <= 0) return;

    CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sampleBuffer);
    if (!fmt) return;
    const AudioStreamBasicDescription* asbd =
        CMAudioFormatDescriptionGetStreamBasicDescription(fmt);
    if (!asbd) return;
    const uint32_t channels = asbd->mChannelsPerFrame;
    if (channels == 0) return;

    AudioBufferList abl{};
    CMBlockBufferRef block = nullptr;
    OSStatus s = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
        sampleBuffer,
        nullptr,
        &abl,
        sizeof(abl),
        kCFAllocatorDefault,
        kCFAllocatorDefault,
        kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
        &block);
    if (s != noErr || abl.mNumberBuffers == 0) {
        if (block) CFRelease(block);
        return;
    }

    const size_t outFrames = (size_t)frames;
    _scratch.assign(outFrames * 2, 0);

    const bool isFloat = (asbd->mFormatID == kAudioFormatLinearPCM) &&
                         (asbd->mFormatFlags & kAudioFormatFlagIsFloat);
    const bool packed = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0;

    auto clipToS16 = [](float v) -> int16_t {
        float c = v;
        if (c > 1.0f) c = 1.0f;
        if (c < -1.0f) c = -1.0f;
        return (int16_t)(c * 32767.0f);
    };

    if (isFloat && !packed && abl.mNumberBuffers >= 1) {
        // Non-interleaved float planes; fold to stereo.
        const float* L = (const float*)abl.mBuffers[0].mData;
        const float* R = (abl.mNumberBuffers >= 2)
            ? (const float*)abl.mBuffers[1].mData
            : L;
        for (size_t i = 0; i < outFrames; ++i) {
            _scratch[i * 2 + 0] = clipToS16(L ? L[i] : 0.0f);
            _scratch[i * 2 + 1] = clipToS16(R ? R[i] : 0.0f);
        }
    } else if (isFloat && packed) {
        const float* p = (const float*)abl.mBuffers[0].mData;
        if (channels >= 2) {
            for (size_t i = 0; i < outFrames; ++i) {
                _scratch[i * 2 + 0] = clipToS16(p[i * channels + 0]);
                _scratch[i * 2 + 1] = clipToS16(p[i * channels + 1]);
            }
        } else {
            for (size_t i = 0; i < outFrames; ++i) {
                int16_t v = clipToS16(p[i]);
                _scratch[i * 2 + 0] = v;
                _scratch[i * 2 + 1] = v;
            }
        }
    } else if (!isFloat && packed) {
        const int16_t* p = (const int16_t*)abl.mBuffers[0].mData;
        if (channels >= 2) {
            for (size_t i = 0; i < outFrames; ++i) {
                _scratch[i * 2 + 0] = p[i * channels + 0];
                _scratch[i * 2 + 1] = p[i * channels + 1];
            }
        } else {
            for (size_t i = 0; i < outFrames; ++i) {
                _scratch[i * 2 + 0] = p[i];
                _scratch[i * 2 + 1] = p[i];
            }
        }
    }

    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    uint64_t hostNs = 0;
    if (CMTIME_IS_VALID(pts)) {
        hostNs = (uint64_t)((double)pts.value * 1e9 / (double)pts.timescale);
    } else {
        hostNs = fuvr::audio::hostTimeToNanos(mach_absolute_time());
    }

    if (_cb) _cb(_scratch.data(), outFrames, hostNs);
    if (block) CFRelease(block);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    (void)stream; (void)error;
}
@end

namespace fuvr::audio {

class CaptureImpl final : public Capture {
public:
    explicit CaptureImpl(CaptureCallback cb) : cb_(std::move(cb)) {}
    ~CaptureImpl() override { stop(); }

    bool start() override {
        if (running_.exchange(true)) return true;
        if (@available(macOS 13.0, *)) {
            __block bool ok = false;
            CaptureImpl* me = this;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [SCShareableContent getShareableContentWithCompletionHandler:^(
                SCShareableContent *content, NSError *err) {
                if (err || !content || content.displays.count == 0) {
                    dispatch_semaphore_signal(sem);
                    return;
                }
                SCDisplay* display = content.displays.firstObject;
                SCContentFilter* filter = [[SCContentFilter alloc]
                    initWithDisplay:display excludingWindows:@[]];
                SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
                cfg.capturesAudio = YES;
                cfg.sampleRate = (NSInteger)Capture::kSampleRate;
                cfg.channelCount = (NSInteger)Capture::kChannels;
                cfg.excludesCurrentProcessAudio = YES;
                cfg.minimumFrameInterval = CMTimeMake(1, 60);

                FuvrAudioStreamOutput* out = [[FuvrAudioStreamOutput alloc] init];
                out->_cb = me->cb_;
                me->output_ = out;

                SCStream* stream = [[SCStream alloc]
                    initWithFilter:filter configuration:cfg delegate:out];
                NSError* addErr = nil;
                [stream addStreamOutput:out
                                   type:SCStreamOutputTypeAudio
                     sampleHandlerQueue:dispatch_get_global_queue(
                                            QOS_CLASS_USER_INTERACTIVE, 0)
                                  error:&addErr];
                if (addErr) {
                    dispatch_semaphore_signal(sem);
                    return;
                }
                me->stream_ = stream;
                [stream startCaptureWithCompletionHandler:^(NSError *startErr) {
                    ok = (startErr == nil);
                    dispatch_semaphore_signal(sem);
                }];
            }];
            dispatch_semaphore_wait(sem,
                dispatch_time(DISPATCH_TIME_NOW, 5LL * NSEC_PER_SEC));
            if (!ok) running_.store(false);
            return ok;
        }
        running_.store(false);
        return false;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (@available(macOS 13.0, *)) {
            SCStream* s = (SCStream*)stream_;
            if (s) {
                [s stopCaptureWithCompletionHandler:^(NSError *err) { (void)err; }];
            }
            stream_  = nil;
            output_  = nil;
        }
    }

private:
    CaptureCallback cb_;
    std::atomic<bool> running_{false};
    id stream_{nil};
    id output_{nil};
};

std::unique_ptr<Capture> Capture::create(CaptureCallback cb) {
    return std::make_unique<CaptureImpl>(std::move(cb));
}

} // namespace fuvr::audio
