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

// Default sample: IMU-derived velocity = 10 m/s along +X, matching the
// position progression used in the LinearExtrapolation test (x advances by
// 0.1 every 10 ms ⇒ 10 m/s). Tests that need the no-IMU-velocity fallback
// path explicitly zero linearVelocity / angularVelocity.
PoseSample makeSample(uint64_t t, float x) {
  PoseSample s{};
  s.timestampNs = t;
  s.leftEye.position = Vec3{x, 0.0f, 0.0f};
  s.rightEye.position = Vec3{x + 0.06f, 0.0f, 0.0f};
  s.leftEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
  s.rightEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
  s.linearVelocity = Vec3{10.0f, 0.0f, 0.0f};
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

TEST(PosePredictor, AntipodalQuatDoesNotGlitch) {
  // Two near-equal rotations encoded as antipodal quats. Before the fix,
  // slerp/extrapolation across the buffer treated them as a ~360° jump,
  // producing a wild predicted orientation.
  PosePredictor p;
  // Small rotation about Y: ~2°. Quat ≈ (0, 0.01745, 0, 0.99985).
  const float a = 0.01745f;
  const float w = 0.99985f;
  for (int i = 0; i < 4; ++i) {
    PoseSample s = makeSample(static_cast<uint64_t>(i) * 10'000'000ull, 0.0f);
    // Alternate signs to simulate Quest double-cover sign flips.
    const float sgn = (i % 2 == 0) ? 1.0f : -1.0f;
    s.leftEye.orientation = Quat{0.0f, sgn * a, 0.0f, sgn * w};
    s.rightEye.orientation = Quat{0.0f, sgn * a, 0.0f, sgn * w};
    p.push(s);
  }
  auto out = p.predict(35'000'000);
  ASSERT_TRUE(out.has_value());
  const auto& q = out->leftEye.orientation;
  // Predicted orientation must remain near the (canonicalized) sample, NOT
  // somewhere on the far side of the great circle.
  // |q.y| should still be near a, |q.w| near w.
  EXPECT_NEAR(std::fabs(q.y), a, 0.05f);
  EXPECT_NEAR(std::fabs(q.w), w, 0.05f);
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  EXPECT_NEAR(n, 1.0f, 1e-3f);
}

TEST(PosePredictor, ImuAngularVelocityRotatesAroundExpectedAxis) {
  // SoTA path: with the canonical Meta-IMU ω plumbed through, a single
  // sample with angular velocity ω = (0, π/2, 0) rad/s should, after dt =
  // 100 ms (capped well below the 60 ms predictor cap, so 60 ms applies),
  // rotate the identity orientation about +Y by π/2 * 0.06 = 0.0942 rad
  // ≈ 5.4°. quat = (0, sin(0.0471), 0, cos(0.0471)).
  PosePredictor p;
  PoseSample s{};
  s.timestampNs = 0;
  s.leftEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
  s.rightEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
  s.angularVelocity = Vec3{0.0f, static_cast<float>(M_PI_2), 0.0f};
  // Two samples; nudge the right-eye position by 1 µm on the second so the
  // dedup in push() (which compares pose bits) doesn't drop the second push
  // and we end up with size_>=1 (sufficient for the IMU-velocity path).
  p.push(s);
  s.timestampNs = 10'000'000;  // 10 ms later, near-identical orientation.
  s.rightEye.position.x += 1e-6f;
  p.push(s);
  // Predict 100 ms ahead — the predictor's 60 ms cap clamps this.
  auto out = p.predict(110'000'000);
  ASSERT_TRUE(out.has_value());
  const float expectedHalf = 0.5f * static_cast<float>(M_PI_2) * 0.060f;
  EXPECT_NEAR(out->leftEye.orientation.y, std::sin(expectedHalf), 1e-3f);
  EXPECT_NEAR(out->leftEye.orientation.w, std::cos(expectedHalf), 1e-3f);
  EXPECT_NEAR(out->leftEye.orientation.x, 0.0f, 1e-4f);
  EXPECT_NEAR(out->leftEye.orientation.z, 0.0f, 1e-4f);
}

TEST(PosePredictor, NoImuVelocityFallsBackToFiniteDifferenceLinear) {
  // Older-Quest fallback: without IMU velocities (linVel=angVel=0) the
  // predictor still extrapolates position via single-sample finite difference.
  PosePredictor p;
  for (int i = 0; i < 4; ++i) {
    PoseSample s{};
    s.timestampNs = static_cast<uint64_t>(i) * 10'000'000ull;
    s.leftEye.position = Vec3{static_cast<float>(i) * 0.05f, 0.0f, 0.0f};
    s.rightEye.position = Vec3{static_cast<float>(i) * 0.05f + 0.06f, 0.0f, 0.0f};
    s.leftEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    s.rightEye.orientation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    // linVel/angVel intentionally zero: simulate an upstream that hasn't
    // started populating XrSpaceVelocity yet.
    p.push(s);
  }
  auto out = p.predict(40'000'000);
  ASSERT_TRUE(out.has_value());
  // Last two samples 10ms apart, Δx = 0.05 ⇒ v = 5 m/s. predict 10ms ahead
  // ⇒ 0.15 + 5*0.01 = 0.20.
  EXPECT_NEAR(out->leftEye.position.x, 0.20f, 1e-3f);
  // Orientation must NOT rotate when angular velocity is unknown — refusing
  // to amplify finite-difference noise into a wild rotation is the whole
  // point of the new design.
  EXPECT_FLOAT_EQ(out->leftEye.orientation.w, 1.0f);
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
