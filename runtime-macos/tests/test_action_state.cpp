// SPDX-License-Identifier: Apache-2.0
#include "fuvr/action_state.hpp"

#include <gtest/gtest.h>

namespace fr = fuvr::runtime;

namespace {

fr::InputSnapshot mkSnap(uint64_t t, bool leftActive, float trigger,
                          bool rightActive, float thumbX) {
  fr::InputSnapshot s{};
  s.receivedAtNs = t;
  s.left.active = leftActive;
  s.left.trigger = trigger;
  s.right.active = rightActive;
  s.right.thumbstickX = thumbX;
  return s;
}

}  // namespace

TEST(ActionStateCache, InitiallyInactive) {
  fr::ActionStateCache c;
  auto s = c.snapshot();
  EXPECT_FALSE(s.left.active);
  EXPECT_FALSE(s.right.active);
  EXPECT_EQ(c.triggerChangeTime(fr::Hand::Left), 0);
}

TEST(ActionStateCache, UpdateRecordsValuesAndChangeTime) {
  fr::ActionStateCache c;
  c.update(mkSnap(1000, true, 0.5f, false, 0.0f));
  auto s = c.snapshot();
  EXPECT_TRUE(s.left.active);
  EXPECT_FLOAT_EQ(s.left.trigger, 0.5f);
  EXPECT_EQ(c.triggerChangeTime(fr::Hand::Left), 1000);
  EXPECT_EQ(c.activeChangeTime(fr::Hand::Left), 1000);
}

TEST(ActionStateCache, UnchangedFieldDoesNotBumpTime) {
  fr::ActionStateCache c;
  c.update(mkSnap(1000, true, 0.5f, false, 0.0f));
  c.update(mkSnap(2000, true, 0.5f, false, 0.0f));
  EXPECT_EQ(c.triggerChangeTime(fr::Hand::Left), 1000);
}

TEST(ActionStateCache, ChangedFieldBumpsTime) {
  fr::ActionStateCache c;
  c.update(mkSnap(1000, true, 0.5f, false, 0.0f));
  c.update(mkSnap(2000, true, 0.7f, false, 0.0f));
  EXPECT_EQ(c.triggerChangeTime(fr::Hand::Left), 2000);
}

TEST(ActionStateCache, ActiveFlipBumpsActiveTime) {
  fr::ActionStateCache c;
  c.update(mkSnap(1000, false, 0.0f, false, 0.0f));
  c.update(mkSnap(2000, true, 0.0f, false, 0.0f));
  EXPECT_EQ(c.activeChangeTime(fr::Hand::Left), 2000);
}

TEST(ActionStateCache, ThumbstickMovementUpdatesAxisAndTime) {
  fr::ActionStateCache c;
  c.update(mkSnap(1000, false, 0.0f, true, 0.5f));
  EXPECT_EQ(c.thumbstickChangeTime(fr::Hand::Right), 1000);
  c.update(mkSnap(2000, false, 0.0f, true, -0.5f));
  EXPECT_EQ(c.thumbstickChangeTime(fr::Hand::Right), 2000);
}
