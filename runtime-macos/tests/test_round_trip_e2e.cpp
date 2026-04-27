// SPDX-License-Identifier: Apache-2.0
//
// End-to-end validation that a *forward-stamped* pose sample (Quest-side
// prediction landed Issue B) flowing through pose_router → predictor →
// xrLocateViews_impl produces a near-identity rendered pose.
//
// Distinct from quest-app's test_round_trip.cpp (EWMA convergence + clamp on
// the Quest side) and from test_pose_predictor.cpp (predictor unit semantics).
// This test exercises the runtime-macos predictor as the *consumer* of
// already-future-stamped samples: when sample.timestampNs ≈ requested
// displayTime, the predictor's extrapolation Δt collapses to ~0 and the
// returned pose must be (within float noise) the input pose itself.
//
// If Quest-side prediction works correctly end-to-end, this is what Mac sees:
// the predictor becomes a passthrough and the OS compositor's scan-out
// timewarp handles only residual error.

#include "fuvr/pose_predictor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

using fuvr::runtime::Pose;
using fuvr::runtime::PosePredictor;
using fuvr::runtime::PoseSample;
using fuvr::runtime::Quat;
using fuvr::runtime::Vec3;

namespace {

// Build a non-trivial sample — translated, rotated about Y by ~10°, with
// non-zero linear and angular velocity so the predictor's extrapolation
// math actually runs (rather than short-circuiting on a degenerate input).
PoseSample makeForwardStamped(uint64_t target_display_ns) {
    PoseSample s{};
    s.timestampNs = target_display_ns;  // already-future: arrival ≪ this
    const float halfYaw = 0.5f * 0.1745f;  // ~10° about Y
    const Quat q{0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
    s.leftEye.position = Vec3{0.10f, 1.60f, -0.20f};
    s.rightEye.position = Vec3{0.16f, 1.60f, -0.20f};
    s.leftEye.orientation = q;
    s.rightEye.orientation = q;
    // 1 m/s along +X, 90°/s yaw — values the Quest would plausibly report
    // and that the predictor would extrapolate over Δt.
    s.linearVelocity = Vec3{1.0f, 0.0f, 0.0f};
    s.angularVelocity = Vec3{0.0f, static_cast<float>(M_PI_2), 0.0f};
    return s;
}

// Pairs (arrival_ns, target_display_ns) — a Quest forward-stamping sequence.
// arrival is when the daemon would receive the sample over USB; target is
// the displayTime the Quest predicted into. The router stamps timestampNs =
// target (Issue B), and xrLocateViews_impl asks predict(displayTime) where
// displayTime ≈ target (the Mac predictor is at most a few hundred µs late).
std::vector<std::pair<uint64_t, uint64_t>> kForwardStampedSequence() {
    return {
        // arrival_ns,             target_display_ns
        {  10'000'000ULL,  45'000'000ULL },  // ~35 ms RTT
        {  21'000'000ULL,  56'000'000ULL },
        {  32'000'000ULL,  67'000'000ULL },
        {  43'000'000ULL,  78'000'000ULL },
        {  54'000'000ULL,  89'000'000ULL },
        {  65'000'000ULL, 100'000'000ULL },
    };
}

// "Near identity" tolerance — the predictor uses a 60 ms cap and IMU velocity
// extrapolation. With Δt = (displayTime - timestampNs) ≈ 0, extrapolation
// should produce sub-millimeter / sub-milliradian deviation.
constexpr float kPosTol_m = 5e-4f;     // 0.5 mm
constexpr float kQuatTol  = 1e-3f;     // ≈ 0.06° on each component

}  // namespace

TEST(RoundTripE2E, ForwardStampedSequenceProducesNearIdentityPose) {
    PosePredictor predictor;
    const auto seq = kForwardStampedSequence();

    for (const auto& [arrival_ns, target_ns] : seq) {
        // pose_router on the Mac side stamps timestampNs = target (Issue B).
        // We feed the predictor in the same order the daemon would.
        (void)arrival_ns;  // arrival_ns is documentation; predictor doesn't use it.
        PoseSample s = makeForwardStamped(target_ns);
        predictor.push(s);

        // xrLocateViews_impl asks the predictor for the *current* frame's
        // displayTime. With Quest-side prediction, displayTime ≈ target_ns.
        // We model the worst realistic late-arrival: Mac is 200 µs behind.
        const uint64_t requested_display_ns = target_ns + 200'000ULL;
        auto out = predictor.predict(requested_display_ns);
        ASSERT_TRUE(out.has_value());

        // Rendered pose must be near the forward-stamped input — i.e. the
        // predictor is acting as a near-passthrough, which is the whole
        // point of relocating prediction to the Quest.
        EXPECT_NEAR(out->leftEye.position.x, s.leftEye.position.x, kPosTol_m);
        EXPECT_NEAR(out->leftEye.position.y, s.leftEye.position.y, kPosTol_m);
        EXPECT_NEAR(out->leftEye.position.z, s.leftEye.position.z, kPosTol_m);

        EXPECT_NEAR(out->leftEye.orientation.x, s.leftEye.orientation.x, kQuatTol);
        EXPECT_NEAR(out->leftEye.orientation.y, s.leftEye.orientation.y, kQuatTol);
        EXPECT_NEAR(out->leftEye.orientation.z, s.leftEye.orientation.z, kQuatTol);
        EXPECT_NEAR(out->leftEye.orientation.w, s.leftEye.orientation.w, kQuatTol);

        // Quaternion must remain unit-norm regardless of Δt direction.
        const auto& q = out->leftEye.orientation;
        const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        EXPECT_NEAR(n, 1.0f, 1e-3f);
    }
}

TEST(RoundTripE2E, RequestExactlyAtTimestampIsIdentity) {
    // Edge case: requested display == sample timestamp. Δt = 0 → the
    // predictor must return the sample unchanged (within float noise).
    PosePredictor predictor;
    const uint64_t target_ns = 50'000'000ULL;
    PoseSample s = makeForwardStamped(target_ns);
    predictor.push(s);

    auto out = predictor.predict(target_ns);
    ASSERT_TRUE(out.has_value());

    EXPECT_FLOAT_EQ(out->leftEye.position.x, s.leftEye.position.x);
    EXPECT_FLOAT_EQ(out->leftEye.position.y, s.leftEye.position.y);
    EXPECT_FLOAT_EQ(out->leftEye.position.z, s.leftEye.position.z);
    EXPECT_NEAR(out->leftEye.orientation.x, s.leftEye.orientation.x, 1e-6f);
    EXPECT_NEAR(out->leftEye.orientation.y, s.leftEye.orientation.y, 1e-6f);
    EXPECT_NEAR(out->leftEye.orientation.z, s.leftEye.orientation.z, 1e-6f);
    EXPECT_NEAR(out->leftEye.orientation.w, s.leftEye.orientation.w, 1e-6f);
}
