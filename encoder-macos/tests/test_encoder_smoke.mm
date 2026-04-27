// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#import <Foundation/Foundation.h>

#include <mutex>

#include <CoreVideo/CoreVideo.h>

#include "fuvr/encoder.hpp"

namespace {

class CountingSink final : public fuvr::FrameSink {
public:
    void onFragment(const fuvr::EncodedFragment& f) override {
        std::lock_guard<std::mutex> lk(mu_);
        ++total;
        if (f.isCsd) ++csd;
        else if (f.isKeyframe) ++keyframes;
        else ++inter;
    }
    std::mutex mu_;
    int total = 0;
    int csd = 0;
    int keyframes = 0;
    int inter = 0;
};

CVPixelBufferRef makeBuffer(uint32_t w, uint32_t h, uint32_t seed) {
    CVPixelBufferRef pb = nullptr;
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
    };
    CVPixelBufferCreate(kCFAllocatorDefault, w, h,
                        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                        (__bridge CFDictionaryRef)attrs, &pb);
    if (!pb) return nullptr;
    CVPixelBufferLockBaseAddress(pb, 0);
    size_t yh = CVPixelBufferGetHeightOfPlane(pb, 0);
    size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
    uint8_t* y = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
    for (size_t i = 0; i < yh * yStride; ++i) y[i] = (uint8_t)((i + seed) & 0xFF);
    size_t ch = CVPixelBufferGetHeightOfPlane(pb, 1);
    size_t cStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
    uint8_t* c = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
    for (size_t i = 0; i < ch * cStride; ++i) c[i] = 128;
    CVPixelBufferUnlockBaseAddress(pb, 0);
    return pb;
}

} // namespace

TEST(EncoderSmoke, ProducesCsdAndFrames) {
    fuvr::EncoderConfig cfg{};
    cfg.width = 1280;
    cfg.height = 720;
    cfg.targetBitrateBps = 8 * 1000 * 1000;
    cfg.framerateHz = 30;
    cfg.codec = fuvr::VideoCodec::Hevc;
    cfg.maxKeyframeIntervalFrames = 60;

    CountingSink sink;
    auto enc = fuvr::Encoder::create(cfg, &sink);
    ASSERT_NE(enc, nullptr);

    const uint32_t N = 30;
    for (uint32_t i = 0; i < N; ++i) {
        CVPixelBufferRef pb = makeBuffer(cfg.width, cfg.height, i);
        ASSERT_NE(pb, nullptr);
        ASSERT_TRUE(enc->submit(pb, i, i * 33000000ULL, /*forceIdr=*/(i == 0)));
        CVPixelBufferRelease(pb);
    }
    enc->flush();

    EXPECT_GE(sink.csd, 1);
    EXPECT_GE(sink.keyframes, 1);
    EXPECT_GE(sink.inter + sink.keyframes, (int)N - 1);
}
