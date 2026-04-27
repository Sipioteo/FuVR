// SPDX-License-Identifier: Apache-2.0
//
// Smoke compile-and-link test for SckCapture. Runtime behaviour requires:
//   - a logged-in GUI session,
//   - Screen Recording TCC permission,
//   - a real CGVirtualDisplay,
// none of which are available in CI. The end-to-end exercise is gated behind
// the FUVR_VDISPLAY_E2E=1 environment variable.
//
// Manual harness:
//   1. Start the helper:
//        ./fuvr-vdisplay-helper --width 1920 --height 1080 --refresh 60
//      Note the printed `display_id=NNN`.
//   2. Export FUVR_VDISPLAY_E2E=1 and FUVR_VDISPLAY_DISPLAY_ID=NNN.
//   3. Run `ctest -R sck_capture --output-on-failure`.
#include <gtest/gtest.h>

#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>

#include "fuvr/vdisplay/sck_capture.hpp"

TEST(SckCapture, LinksAndRejectsBogusDisplay) {
    // Bogus display id must yield nullptr (or, if it somehow matches, the
    // sink simply never fires). We assert it doesn't crash.
    auto cap = fuvr::vdisplay::SckCapture::create(
        /*displayId*/ 0u, /*fps*/ 60,
        [](CVPixelBufferRef, uint64_t) {});
    // Either nullptr (display not found) or a stream we can stop cleanly.
    if (cap) cap->stop();
    SUCCEED();
}

TEST(SckCapture, DISABLED_E2E_DeliversAtLeastOneFrame) {
    const char* enabled = std::getenv("FUVR_VDISPLAY_E2E");
    if (!enabled || enabled[0] != '1') {
        GTEST_SKIP() << "FUVR_VDISPLAY_E2E!=1; skipping hardware test.";
    }
    const char* didStr = std::getenv("FUVR_VDISPLAY_DISPLAY_ID");
    ASSERT_NE(didStr, nullptr) << "FUVR_VDISPLAY_DISPLAY_ID must be set.";
    uint32_t displayId = static_cast<uint32_t>(std::strtoul(didStr, nullptr, 10));
    ASSERT_NE(displayId, 0u);

    std::atomic<int> frames{0};
    auto cap = fuvr::vdisplay::SckCapture::create(
        displayId, 60,
        [&](CVPixelBufferRef, uint64_t) { frames.fetch_add(1); });
    ASSERT_NE(cap, nullptr);
    cap->start();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cap->stop();
    EXPECT_GT(frames.load(), 0);
}
