// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/opus_encoder.hpp"

#include <gtest/gtest.h>
#include <opus.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

std::vector<int16_t> sineStereo(uint32_t sampleRate, uint32_t durationMs,
                                float freqHz, float amp = 0.3f) {
    const size_t frames = (size_t)sampleRate * durationMs / 1000;
    std::vector<int16_t> out(frames * 2);
    const double w = 2.0 * 3.14159265358979323846 * (double)freqHz
                     / (double)sampleRate;
    for (size_t i = 0; i < frames; ++i) {
        double s = amp * std::sin(w * (double)i);
        int16_t v = (int16_t)(s * 32767.0);
        out[i * 2 + 0] = v;
        out[i * 2 + 1] = v;
    }
    return out;
}

} // namespace

TEST(OpusEncoder, EncodesSineNonZero) {
    auto enc = fuvr::audio::OpusEncoderWrap::create(48000, 2);
    ASSERT_NE(enc, nullptr);

    auto pcm = sineStereo(48000, 1000, 440.0f);
    const uint32_t fpp = enc->framesPerPacket();
    ASSERT_EQ(fpp, 960u);

    std::vector<uint8_t> packet(4000);
    size_t totalBytes = 0;
    size_t packets = 0;
    for (size_t off = 0; off + fpp <= pcm.size() / 2; off += fpp) {
        size_t n = enc->encode(pcm.data() + off * 2, fpp, packet);
        ASSERT_GT(n, 0u);
        totalBytes += n;
        packets++;
    }
    EXPECT_EQ(packets, 50u);
    EXPECT_GT(totalBytes, 100u);
}

TEST(OpusEncoder, RoundTripRmsSmall) {
    auto enc = fuvr::audio::OpusEncoderWrap::create(48000, 2);
    ASSERT_NE(enc, nullptr);

    int err = OPUS_OK;
    OpusDecoder* dec = opus_decoder_create(48000, 2, &err);
    ASSERT_EQ(err, OPUS_OK);
    ASSERT_NE(dec, nullptr);

    auto pcm = sineStereo(48000, 1000, 440.0f, 0.5f);
    const uint32_t fpp = enc->framesPerPacket();
    std::vector<uint8_t> packet(4000);
    std::vector<int16_t> dpcm(fpp * 2);
    std::vector<int16_t> decoded;
    decoded.reserve(pcm.size());

    for (size_t off = 0; off + fpp <= pcm.size() / 2; off += fpp) {
        size_t n = enc->encode(pcm.data() + off * 2, fpp, packet);
        ASSERT_GT(n, 0u);
        int got = opus_decode(dec, packet.data(), (opus_int32)n,
                              dpcm.data(), (int)fpp, 0);
        ASSERT_EQ(got, (int)fpp);
        decoded.insert(decoded.end(), dpcm.begin(), dpcm.end());
    }

    // Skip first 40 ms to let the codec warm up.
    const size_t skip = 48000 * 40 / 1000 * 2;
    ASSERT_GT(decoded.size(), skip);
    double sumSq = 0.0;
    size_t cmp = std::min(pcm.size(), decoded.size()) - skip;
    for (size_t i = skip; i < skip + cmp; ++i) {
        double d = ((double)pcm[i] - (double)decoded[i]) / 32768.0;
        sumSq += d * d;
    }
    double rms = std::sqrt(sumSq / (double)cmp);
    // Threshold is generous: OPUS_APPLICATION_RESTRICTED_LOWDELAY at the
    // complexity / bitrate we ship has audible coloration on a sine. We are
    // only asserting "decode produces something correlated", not transparency.
    EXPECT_LT(rms, 0.30);

    opus_decoder_destroy(dec);
}
