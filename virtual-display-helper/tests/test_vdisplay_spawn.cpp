// SPDX-License-Identifier: Apache-2.0
//
// DISABLED by default: requires WindowServer + a logged-in GUI session.
// Will not run in CI. Run manually:
//     ctest --gtest_also_run_disabled_tests -R vdisplay
#include <gtest/gtest.h>

#include "fuvr_vdisplay_control.h"
#include "clamp_dimensions.h"

TEST(ClampDimensions, BelowLimitNoSplit) {
  bool split = false;
  auto modes = fuvr::vdisplay::clamp_dimensions(1920, 1080, 60.0, &split);
  EXPECT_FALSE(split);
  EXPECT_EQ(modes.size(), 1u);
}

TEST(ClampDimensions, OverWidthSplits) {
  bool split = false;
  auto modes = fuvr::vdisplay::clamp_dimensions(8192, 2160, 90.0, &split);
  EXPECT_TRUE(split);
  EXPECT_EQ(modes.size(), 2u);
  EXPECT_EQ(modes[0].width + modes[1].width, 8192u);
}

TEST(VDisplaySpawn, DISABLED_SpawnsAndReturnsId) {
  auto* h = fuvr_vdisplay_spawn(1920, 1080, 60);
  ASSERT_NE(h, nullptr);
  EXPECT_NE(fuvr_vdisplay_id(h), 0u);
  fuvr_vdisplay_kill(h);
}
