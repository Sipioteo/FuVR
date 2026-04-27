// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/capture.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

TEST(AudioCaptureSmoke, FactoryReturnsInstance) {
    auto cap = fuvr::audio::Capture::create(
        [](const int16_t*, std::size_t, std::uint64_t) {});
    ASSERT_NE(cap, nullptr);
}

TEST(AudioCaptureSmoke, StartStopGated) {
    if (std::getenv("FUVR_AUDIO_CAPTURE_RUNTIME") == nullptr) {
        GTEST_SKIP() << "ScreenCaptureKit requires a GUI session and TCC; "
                        "set FUVR_AUDIO_CAPTURE_RUNTIME=1 to run.";
    }
    std::atomic<int> hits{0};
    auto cap = fuvr::audio::Capture::create(
        [&](const int16_t*, std::size_t, std::uint64_t) { hits.fetch_add(1); });
    ASSERT_NE(cap, nullptr);
    ASSERT_TRUE(cap->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    cap->stop();
    EXPECT_GT(hits.load(), 0);
}
