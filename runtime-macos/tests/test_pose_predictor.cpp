// SPDX-License-Identifier: Apache-2.0
#include "fuvr/pose_predictor.hpp"

#include <gtest/gtest.h>

#include <cmath>

using fuvr::runtime::Pose;
using fuvr::runtime::PosePredictor;
using fuvr::runtime::PoseSample;
using fuvr::runtime::Quat;
using fuvr::runtime::Vec3;

namespace {

PoseSample makeSample(uint64_t t, float x) {
  PoseSample s{};
  s.timestampNs = t;
  s.leftEye.position = Vec3{x, 0.0f, 0.0f};
  s.rightEye.position = Vec3{x + 0.06f, 0.0f, 0.0f};
  s.leftEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
  s.rightEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
  s.linearVelocity = Vec3{1.0f, 0.0f, 0.0f};
  return s;
}

}  // namespace

TEST(PosePredictor, EmptyReturnsNullopt) {
  PosePredictor p;
  EXPECT_FALSE(p.predict(1000).has_value());
  EXPECT_TRUE(p.empty());
}

TEST(PosePredictor, SingleSampleReturnsItself) {
  PosePredictor p;
  p.push(makeSample(1'000'000, 0.5f));
  auto out = p.predict(2'000'000);
  ASSERT_TRUE(out.has_value());
  EXPECT_FLOAT_EQ(out->leftEye.position.x, 0.5f);
}

TEST(PosePredictor, LinearExtrapolation) {
  PosePredictor p;
  for (int i = 0; i < 4; ++i) {
    p.push(makeSample(static_cast<uint64_t>(i) * 10'000'000ull,
                      static_cast<float>(i) * 0.1f));
  }
  auto out = p.predict(40'000'000);
  ASSERT_TRUE(out.has_value());
  EXPECT_NEAR(out->leftEye.position.x, 0.4f, 1e-3f);
}

TEST(PosePredictor, RingBufferOverflowKeepsLatest) {
  PosePredictor p;
  for (int i = 0; i < 50; ++i) {
    p.push(makeSample(static_cast<uint64_t>(i) * 1'000'000ull,
                      static_cast<float>(i)));
  }
  EXPECT_EQ(p.size(), PosePredictor::kCapacity);
  auto latest = p.latest();
  ASSERT_TRUE(latest.has_value());
  EXPECT_FLOAT_EQ(latest->leftEye.position.x, 49.0f);
}

TEST(PosePredictor, OrientationStaysNormalized) {
  PosePredictor p;
  for (int i = 0; i < 5; ++i) {
    PoseSample s = makeSample(static_cast<uint64_t>(i) * 10'000'000ull,
                              static_cast<float>(i) * 0.01f);
    s.angularVelocity = Vec3{0.0f, 1.0f, 0.0f};
    p.push(s);
  }
  auto out = p.predict(60'000'000);
  ASSERT_TRUE(out.has_value());
  const auto& q = out->leftEye.orientation;
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  EXPECT_NEAR(n, 1.0f, 1e-3f);
}
