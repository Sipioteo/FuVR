// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fuvr/metrics.hpp"

using fuvr::daemon::RollingWindow;

TEST(RollingWindow, MeanOfUniform) {
    RollingWindow w;
    for (int i = 1; i <= 10; ++i) w.push(static_cast<double>(i));
    EXPECT_NEAR(w.mean(), 5.5, 1e-9);
    EXPECT_EQ(w.size(), 10u);
}

TEST(RollingWindow, P95) {
    RollingWindow w;
    for (int i = 1; i <= 100; ++i) w.push(static_cast<double>(i));
    double p95 = w.percentile(0.95);
    EXPECT_GE(p95, 94.0);
    EXPECT_LE(p95, 96.5);
}

TEST(RollingWindow, OverflowDropsOldest) {
    RollingWindow w;
    for (int i = 0; i < static_cast<int>(RollingWindow::kCapacity) + 50; ++i) {
        w.push(static_cast<double>(i));
    }
    EXPECT_EQ(w.size(), RollingWindow::kCapacity);
    // Oldest 50 dropped: remaining mean should reflect last 256 samples.
    double expectedMean = 0.0;
    int total = static_cast<int>(RollingWindow::kCapacity) + 50;
    for (int i = total - static_cast<int>(RollingWindow::kCapacity); i < total; ++i) {
        expectedMean += i;
    }
    expectedMean /= static_cast<double>(RollingWindow::kCapacity);
    EXPECT_NEAR(w.mean(), expectedMean, 1e-6);
}
