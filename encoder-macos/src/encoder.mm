// SPDX-License-Identifier: Apache-2.0
#include "fuvr/encoder.hpp"

#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace fuvr {

namespace {

struct FrameContext {
    uint64_t frameId;
    uint64_t renderStartNs;
};

class EncoderImpl final : public Encoder {
public:
    EncoderImpl(const EncoderConfig& cfg, FrameSink* sink)
        : config_(cfg), sink_(sink) {}

    ~EncoderImpl() override {
        if (session_) {
            VTCompressionSessionInvalidate(session_);
            CFRelease(session_);
            session_ = nullptr;
        }
    }

    bool init() {
        CMVideoCodecType cmCodec = (config_.codec == VideoCodec::Hevc)
                                       ? kCMVideoCodecType_HEVC
                                       : kCMVideoCodecType_H264;

        NSMutableDictionary* spec = [NSMutableDictionary dictionary];
        spec[(__bridge NSString*)
             kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder] = @YES;
        spec[(__bridge NSString*)
             kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder] = @YES;

        // Why: WWDC21 low-latency rate control is H.264-only per Apple docs.
        if (config_.codec == VideoCodec::H264) {
            spec[(__bridge NSString*)
                 kVTVideoEncoderSpecification_EnableLowLatencyRateControl] = @YES;
        }

        NSDictionary* sourceAttrs = @{
            (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey:
                @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        };

        OSStatus s = VTCompressionSessionCreate(
            kCFAllocatorDefault,
            (int32_t)config_.width,
            (int32_t)config_.height,
            cmCodec,
            (__bridge CFDictionaryRef)spec,
            (__bridge CFDictionaryRef)sourceAttrs,
            nullptr,
            &EncoderImpl::compressionCallback,
            this,
            &session_);
        if (s != noErr || session_ == nullptr) {
            return false;
        }

        setProp(kVTCompressionPropertyKey_RealTime,
                config_.realTime ? kCFBooleanTrue : kCFBooleanFalse);
        setProp(kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);

        if (config_.codec == VideoCodec::Hevc) {
            setProp(kVTCompressionPropertyKey_ProfileLevel,
                    kVTProfileLevel_HEVC_Main_AutoLevel);
        } else {
            setProp(kVTCompressionPropertyKey_ProfileLevel,
                    kVTProfileLevel_H264_High_AutoLevel);
        }

        setIntProp(kVTCompressionPropertyKey_MaxKeyFrameInterval,
                   (int)config_.maxKeyframeIntervalFrames);
        setIntProp(kVTCompressionPropertyKey_ExpectedFrameRate,
                   (int)config_.framerateHz);
        setIntProp(kVTCompressionPropertyKey_AverageBitRate,
                   (int)config_.targetBitrateBps);

        // DataRateLimits: cap to ~1.5x average over 1s window.
        const double bytesPerSec = (double)config_.targetBitrateBps / 8.0 * 1.5;
        NSArray* limits = @[ @((long long)bytesPerSec), @1.0 ];
        VTSessionSetProperty(session_, kVTCompressionPropertyKey_DataRateLimits,
                             (__bridge CFArrayRef)limits);

        VTCompressionSessionPrepareToEncodeFrames(session_);
        return true;
    }

    bool submit(CVPixelBufferRef pixelBuffer,
                uint64_t frameId,
                uint64_t renderStartNs,
                bool forceIdr) override {
        if (!session_ || !pixelBuffer) return false;

        auto* fctx = new FrameContext{frameId, renderStartNs};

        NSDictionary* frameProps = nil;
        if (forceIdr) {
            frameProps = @{
                (__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES
            };
        }

        CMTime pts = CMTimeMake((int64_t)frameId,
                                (int32_t)config_.framerateHz);
        CMTime dur = CMTimeMake(1, (int32_t)config_.framerateHz);

        OSStatus s = VTCompressionSessionEncodeFrame(
            session_, pixelBuffer, pts, dur,
            (__bridge CFDictionaryRef)frameProps,
            fctx, nullptr);
        if (s != noErr) {
            delete fctx;
            return false;
        }
        return true;
    }

    void flush() override {
        if (session_) {
            VTCompressionSessionCompleteFrames(session_, kCMTimeInvalid);
        }
    }

private:
    void setProp(CFStringRef key, CFTypeRef value) {
        VTSessionSetProperty(session_, key, value);
    }
    void setIntProp(CFStringRef key, int value) {
        CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
        VTSessionSetProperty(session_, key, n);
        CFRelease(n);
    }

    // Why: VideoToolbox invokes this on its own compression thread; the sink
    // is called inline so the caller decides any threading policy.
    static void compressionCallback(void* outputCallbackRefCon,
                                    void* sourceFrameRefCon,
                                    OSStatus status,
                                    VTEncodeInfoFlags /*infoFlags*/,
                                    CMSampleBufferRef sampleBuffer) {
        std::unique_ptr<FrameContext> fctx(
            static_cast<FrameContext*>(sourceFrameRefCon));
        auto* self = static_cast<EncoderImpl*>(outputCallbackRefCon);
        if (status != noErr || !sampleBuffer || !self || !self->sink_) return;
        if (!CMSampleBufferDataIsReady(sampleBuffer)) return;

        const bool isKeyframe = sampleBufferIsKeyframe(sampleBuffer);

        if (isKeyframe) {
            self->emitCsdIfNeeded(sampleBuffer, *fctx);
        }

        self->emitAnnexB(sampleBuffer, *fctx, isKeyframe);
    }

    static bool sampleBufferIsKeyframe(CMSampleBufferRef sb) {
        CFArrayRef attachments =
            CMSampleBufferGetSampleAttachmentsArray(sb, false);
        if (!attachments || CFArrayGetCount(attachments) == 0) return false;
        CFDictionaryRef attach =
            (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        if (!attach) return false;
        CFBooleanRef notSync = (CFBooleanRef)CFDictionaryGetValue(
            attach, kCMSampleAttachmentKey_NotSync);
        return notSync == nullptr || !CFBooleanGetValue(notSync);
    }

    void emitCsdIfNeeded(CMSampleBufferRef sb, const FrameContext& fctx) {
        CMFormatDescriptionRef desc = CMSampleBufferGetFormatDescription(sb);
        if (!desc) return;

        std::vector<uint8_t> csd;
        const bool hevc = (config_.codec == VideoCodec::Hevc);
        size_t paramCount = 0;
        int nalUnitHeaderLen = 0;

        if (hevc) {
            CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                desc, 0, nullptr, nullptr, &paramCount, &nalUnitHeaderLen);
        } else {
            CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                desc, 0, nullptr, nullptr, &paramCount, &nalUnitHeaderLen);
        }

        for (size_t i = 0; i < paramCount; ++i) {
            const uint8_t* psPtr = nullptr;
            size_t psSize = 0;
            OSStatus s;
            if (hevc) {
                s = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                    desc, i, &psPtr, &psSize, nullptr, nullptr);
            } else {
                s = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    desc, i, &psPtr, &psSize, nullptr, nullptr);
            }
            if (s != noErr || !psPtr) continue;
            csd.push_back(0x00);
            csd.push_back(0x00);
            csd.push_back(0x00);
            csd.push_back(0x01);
            csd.insert(csd.end(), psPtr, psPtr + psSize);
        }

        if (csd.empty()) return;

        EncodedFragment frag{};
        frag.frameId = fctx.frameId;
        frag.renderStartNs = fctx.renderStartNs;
        frag.isKeyframe = false;
        frag.endOfFrame = false;
        frag.isCsd = true;
        frag.data = csd.data();
        frag.size = csd.size();
        sink_->onFragment(frag);
    }

    void emitAnnexB(CMSampleBufferRef sb,
                    const FrameContext& fctx,
                    bool isKeyframe) {
        CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
        if (!bb) return;

        size_t totalLen = 0;
        char* dataPtr = nullptr;
        OSStatus s = CMBlockBufferGetDataPointer(bb, 0, nullptr, &totalLen, &dataPtr);
        if (s != noErr || !dataPtr) return;

        // AVCC -> Annex-B: 4-byte big-endian length prefixes -> 00 00 00 01.
        std::vector<uint8_t>& buf = scratch_;
        buf.clear();
        buf.reserve(totalLen + 16);

        size_t off = 0;
        while (off + 4 <= totalLen) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(dataPtr) + off;
            uint32_t nalLen = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                              (uint32_t)p[2] << 8  | (uint32_t)p[3];
            off += 4;
            if (nalLen == 0 || off + nalLen > totalLen) break;
            buf.push_back(0x00);
            buf.push_back(0x00);
            buf.push_back(0x00);
            buf.push_back(0x01);
            buf.insert(buf.end(),
                       reinterpret_cast<const uint8_t*>(dataPtr) + off,
                       reinterpret_cast<const uint8_t*>(dataPtr) + off + nalLen);
            off += nalLen;
        }

        EncodedFragment frag{};
        frag.frameId = fctx.frameId;
        frag.renderStartNs = fctx.renderStartNs;
        frag.isKeyframe = isKeyframe;
        frag.endOfFrame = true;
        frag.isCsd = false;
        frag.data = buf.data();
        frag.size = buf.size();
        sink_->onFragment(frag);
    }

    EncoderConfig config_;
    FrameSink* sink_;
    VTCompressionSessionRef session_ = nullptr;
    std::vector<uint8_t> scratch_;
};

} // namespace

std::unique_ptr<Encoder> Encoder::create(const EncoderConfig& cfg,
                                         FrameSink* sink) {
    auto enc = std::make_unique<EncoderImpl>(cfg, sink);
    if (!enc->init()) return nullptr;
    return enc;
}

} // namespace fuvr
