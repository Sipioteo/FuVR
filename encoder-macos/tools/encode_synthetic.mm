// SPDX-License-Identifier: Apache-2.0
#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <mutex>
#include <vector>

#include "fuvr/encoder.hpp"

namespace {

class FileSink final : public fuvr::FrameSink {
public:
    explicit FileSink(FILE* f) : f_(f) {}
    void onFragment(const fuvr::EncodedFragment& frag) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (frag.size && frag.data) {
            fwrite(frag.data, 1, frag.size, f_);
        }
        ++count_;
        if (frag.isCsd) ++csd_;
        if (frag.isKeyframe) ++key_;
    }
    int count() const { return count_; }
    int csd() const { return csd_; }
    int key() const { return key_; }
private:
    std::mutex mu_;
    FILE* f_;
    int count_ = 0;
    int csd_ = 0;
    int key_ = 0;
};

CVPixelBufferRef makePixelBuffer(uint32_t w, uint32_t h) {
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey:
            @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
    };
    CVPixelBufferRef pb = nullptr;
    CVReturn r = CVPixelBufferCreate(
        kCFAllocatorDefault, w, h,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
        (__bridge CFDictionaryRef)attrs, &pb);
    if (r != kCVReturnSuccess) return nullptr;
    return pb;
}

void fillGradient(CVPixelBufferRef pb, uint32_t frame) {
    CVPixelBufferLockBaseAddress(pb, 0);
    size_t w = CVPixelBufferGetWidthOfPlane(pb, 0);
    size_t h = CVPixelBufferGetHeightOfPlane(pb, 0);
    size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
    uint8_t* yPlane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            yPlane[y * yStride + x] =
                (uint8_t)((x + y + frame * 4) & 0xFF);
        }
    }
    size_t cw = CVPixelBufferGetWidthOfPlane(pb, 1);
    size_t ch = CVPixelBufferGetHeightOfPlane(pb, 1);
    size_t cStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
    uint8_t* cPlane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
    for (size_t y = 0; y < ch; ++y) {
        for (size_t x = 0; x < cw; ++x) {
            cPlane[y * cStride + x * 2 + 0] = (uint8_t)(128 + (frame & 0x3F));
            cPlane[y * cStride + x * 2 + 1] = (uint8_t)(128 - (frame & 0x3F));
        }
    }
    CVPixelBufferUnlockBaseAddress(pb, 0);
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        const char* outPath = (argc > 1) ? argv[1] : "synthetic.h265";
        FILE* f = fopen(outPath, "wb");
        if (!f) {
            fprintf(stderr, "failed to open %s\n", outPath);
            return 1;
        }

        fuvr::EncoderConfig cfg{};
        cfg.width = 4128;
        cfg.height = 2208;
        cfg.targetBitrateBps = 120 * 1000 * 1000;
        cfg.framerateHz = 90;
        cfg.codec = fuvr::VideoCodec::Hevc;
        cfg.realTime = true;
        cfg.maxKeyframeIntervalFrames = 90;

        FileSink sink(f);
        auto enc = fuvr::Encoder::create(cfg, &sink);
        if (!enc) {
            fprintf(stderr, "encoder create failed\n");
            fclose(f);
            return 2;
        }

        const uint32_t frames = 256;
        auto t0 = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < frames; ++i) {
            CVPixelBufferRef pb = makePixelBuffer(cfg.width, cfg.height);
            if (!pb) {
                fprintf(stderr, "pixel buffer alloc failed\n");
                break;
            }
            fillGradient(pb, i);
            uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            enc->submit(pb, i, now, /*forceIdr=*/(i == 0));
            CVPixelBufferRelease(pb);
        }
        enc->flush();
        auto t1 = std::chrono::steady_clock::now();
        fclose(f);

        double secs = std::chrono::duration<double>(t1 - t0).count();
        fprintf(stderr,
                "encoded %u frames in %.2fs (%.1f fps), fragments=%d csd=%d key=%d\n",
                frames, secs, frames / secs, sink.count(), sink.csd(), sink.key());
    }
    return 0;
}
