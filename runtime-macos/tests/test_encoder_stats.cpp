// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fuvr/runtime.hpp"

using fuvr::runtime::EncoderStats;
using fuvr::runtime::EncodeStatSample;

TEST(EncoderStats, EmptySnapshot) {
  EncoderStats s;
  auto snap = s.snapshot();
  EXPECT_EQ(snap.sampleCount, 0u);
  EXPECT_DOUBLE_EQ(snap.meanEncodeMs, 0.0);
}

TEST(EncoderStats, MeanAndP95Across256) {
  EncoderStats s;
  for (uint64_t i = 0; i < EncoderStats::kWindow; ++i) {
    EncodeStatSample sample{};
    sample.frameId = i;
    sample.encodeDurationNs = (i + 1) * 1'000'000;  // 1..256 ms
    sample.encodedSizeBytes = 1000;
    sample.arrivalNs = i * 10'000'000;  // 10ms apart
    s.push(sample);
  }
  auto snap = s.snapshot();
  EXPECT_EQ(snap.sampleCount, EncoderStats::kWindow);
  EXPECT_NEAR(snap.meanEncodeMs, 128.5, 0.01);
  // p95 over 1..256 ms => index floor(0.95 * 255) = 242 => value 243 ms
  EXPECT_NEAR(snap.p95EncodeMs, 243.0, 1.0);
  EXPECT_GT(snap.fps, 0.0);
}

TEST(EncoderStats, WindowRollOff) {
  EncoderStats s;
  for (uint64_t i = 0; i < EncoderStats::kWindow + 1; ++i) {
    EncodeStatSample sample{};
    sample.encodeDurationNs = (i == 0) ? 1'000'000'000ULL : 1'000'000ULL;
    sample.encodedSizeBytes = 1000;
    sample.arrivalNs = i * 10'000'000;
    s.push(sample);
  }
  auto snap = s.snapshot();
  EXPECT_EQ(snap.sampleCount, EncoderStats::kWindow);
  // The 1000-ms outlier should have rolled off; mean should be ~1ms.
  EXPECT_LT(snap.meanEncodeMs, 5.0);
}
