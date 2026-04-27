// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fuvr/clock_sync.hpp"

using fuvr::daemon::ClockSync;

TEST(ClockSync, EmptyWindowReturnsZeros) {
    ClockSync cs;
    auto s = cs.snapshot();
    EXPECT_EQ(s.samples, 0u);
    EXPECT_EQ(s.offsetNs, 0);
    EXPECT_EQ(s.oneWayDelayNs, 0u);
    EXPECT_DOUBLE_EQ(s.varianceNs, 0.0);
}

TEST(ClockSync, RecoversOffsetAndDelayWithinOneNs) {
    ClockSync cs;

    // Construct a synthetic exchange:
    //   Mac sends ping at  T_send = 1000
    //   Quest receives at  t1     = 1'000'000'500     (offset = +999'999'500, delay = 250)
    //   Quest sends at     t2     = 1'000'000'700     (Quest dwell 200 ns)
    //   Mac receives at    T_recv = 1450              (RTT 450, dwell 200, delay = (450-200)/2 = 125)
    // Wait — offset and delay must be self-consistent. Use exact numbers:
    //   T_send=1000, T_recv=1500  -> macSpan=500
    //   t1=2000,     t2=2200      -> questSpan=200
    //   delay = (500-200)/2 = 150
    //   offset = ((2000-1000)+(2200-1500))/2 = (1000+700)/2 = 850
    cs.feedSynthetic(1000, 2000, 2200, 1500);
    auto s = cs.snapshot();
    EXPECT_EQ(s.samples, 1u);
    EXPECT_EQ(s.offsetNs, 850);
    EXPECT_EQ(s.oneWayDelayNs, 150u);
}

TEST(ClockSync, MedianOverMultipleSamples) {
    ClockSync cs;
    // Three samples with offsets 100, 200, 300 -> median 200.
    cs.feedSynthetic(0, 100, 100, 0);   // offset=(100+100)/2=100, delay=0
    cs.feedSynthetic(0, 200, 200, 0);
    cs.feedSynthetic(0, 300, 300, 0);
    auto s = cs.snapshot();
    EXPECT_EQ(s.samples, 3u);
    EXPECT_EQ(s.offsetNs, 200);
}

TEST(ClockSync, RollsOffBeyondWindowSize) {
    ClockSync cs;
    for (int i = 0; i < 32; ++i) {
        uint64_t v = static_cast<uint64_t>(i + 1) * 10ULL;
        cs.feedSynthetic(0, v, v, 0);
    }
    auto s = cs.snapshot();
    EXPECT_LE(s.samples, 16u);
}

TEST(ClockSync, OnPongWithoutPingDropsSilently) {
    ClockSync cs;
    cs.onPong(424242, 1, 2);
    EXPECT_EQ(cs.snapshot().samples, 0u);
}
