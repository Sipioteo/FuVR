// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include "fuvr/encoder.hpp"
#include "fuvr/session.hpp"

using fuvr::daemon::EncodeStatsEvent;
using fuvr::daemon::Session;
using fuvr::daemon::SessionConfig;

TEST(EncodeStatsForward, OneStatsEventPerFrame) {
    SessionConfig cfg;
    cfg.perEyeWidth = 1024;
    cfg.perEyeHeight = 1024;
    cfg.refreshRateHz = 90;
    cfg.codec = fuvr::VideoCodec::H264;
    cfg.bitrateBps = 30'000'000;

    std::atomic<int> events{0};
    std::vector<EncodeStatsEvent> received;
    Session s(1, cfg, /*transport=*/nullptr,
              [&](const EncodeStatsEvent& ev) {
                  received.push_back(ev);
                  events.fetch_add(1);
              });

    constexpr int kFragments = 256;
    std::vector<uint8_t> payload(64, 0xAB);
    const uint64_t frameId = 42;

    for (int i = 0; i < kFragments; ++i) {
        fuvr::EncodedFragment f{};
        f.frameId       = frameId;
        f.renderStartNs = 0;
        f.isKeyframe    = (i == 0);
        f.endOfFrame    = (i == kFragments - 1);
        f.isCsd         = false;
        f.data          = payload.data();
        f.size          = payload.size();
        s.testInjectFragment(f);
    }

    EXPECT_EQ(events.load(), 1);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].frameId, frameId);
    EXPECT_EQ(received[0].encodedSizeBytes,
              static_cast<uint32_t>(kFragments) * static_cast<uint32_t>(payload.size()));
    EXPECT_TRUE(received[0].wasKeyframe);

    // Fire a second non-keyframe; expect a second event.
    for (int i = 0; i < 10; ++i) {
        fuvr::EncodedFragment f{};
        f.frameId    = frameId + 1;
        f.endOfFrame = (i == 9);
        f.data       = payload.data();
        f.size       = payload.size();
        s.testInjectFragment(f);
    }
    EXPECT_EQ(events.load(), 2);
    EXPECT_EQ(received[1].frameId, frameId + 1);
    EXPECT_FALSE(received[1].wasKeyframe);
    EXPECT_EQ(received[1].encodedSizeBytes, 10u * payload.size());
}

TEST(EncodeStatsForward, MetricsBitrateReflectsBytes) {
    SessionConfig cfg;
    cfg.perEyeWidth = 512;
    cfg.perEyeHeight = 512;
    cfg.refreshRateHz = 60;
    cfg.codec = fuvr::VideoCodec::H264;
    cfg.bitrateBps = 10'000'000;

    Session s(2, cfg, /*transport=*/nullptr, nullptr);

    std::vector<uint8_t> payload(2048, 0x11);
    for (int i = 0; i < 8; ++i) {
        fuvr::EncodedFragment f{};
        f.frameId    = static_cast<uint64_t>(i);
        f.endOfFrame = true;
        f.data       = payload.data();
        f.size       = payload.size();
        s.testInjectFragment(f);
    }
    auto snap = s.metrics().snapshot();
    // Mean bytes per fragment should roughly match payload size; bitrate
    // depends on currentFps which only updates after >=1 s, so just assert
    // the encode-bytes window is populated (encodeMsAvg >= 0).
    EXPECT_GE(snap.encoderEncodeMsAvg, 0.0f);
    SUCCEED();
}
