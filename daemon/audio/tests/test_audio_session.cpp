// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon/audio/audio_session.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

// We can't easily intercept fuvr_transport_send without linking the
// transport. Instead the AudioSession exposes packetsSent() so we drive
// `injectPcmForTest` and assert monotonic accumulation. Transport-side
// fan-out is verified by the existing transport loopback test.

TEST(DaemonAudioSession, SynthesizesAndCountsPackets) {
    auto sess = fuvr::daemon::audio::AudioSession::create(
        nullptr, fuvr::daemon::audio::AudioConfig{});
    ASSERT_NE(sess, nullptr);

    constexpr uint32_t sr = 48000;
    constexpr uint32_t ch = 2;
    constexpr uint32_t durMs = 1000;
    const size_t frames = sr * durMs / 1000;
    std::vector<int16_t> pcm(frames * ch);
    const double w = 2.0 * 3.14159265358979 * 440.0 / (double)sr;
    for (size_t i = 0; i < frames; ++i) {
        int16_t v = (int16_t)(0.3 * 32767.0 * std::sin(w * (double)i));
        pcm[i * ch + 0] = v;
        pcm[i * ch + 1] = v;
    }

    // Feed in 10 ms chunks to exercise the buffering path.
    const size_t chunkFrames = sr * 10 / 1000;
    uint64_t pts = 1'000'000'000ull;
    const uint64_t step = (uint64_t)chunkFrames * 1'000'000'000ull / sr;
    for (size_t off = 0; off + chunkFrames <= frames; off += chunkFrames) {
        sess->injectPcmForTest(pcm.data() + off * ch, chunkFrames, pts);
        pts += step;
    }

    // 1 s @ 20 ms frames = 50 packets.
    EXPECT_EQ(sess->packetsSent(), 50u);
}
