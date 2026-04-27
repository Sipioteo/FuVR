// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>

namespace fuvr {

// EWMA estimator of the Quest <-> Mac round-trip (more precisely: the gap
// between the time a video fragment lands on the Quest and the
// targetDisplayTimeNs the Mac stamped into its header). Used by
// pose_forwarder to bias the OpenXR predicted display time by an estimate
// of "how far in the future does the frame I'm sampling pose for need to
// land?", so the pose we ship upstream is already extrapolated to the right
// future on the Quest's clock.
//
// Thread-safe via a single relaxed atomic: record_sample and current_ns
// can be called from any thread without locking.
class RoundTripEstimator {
public:
    // Default seed when no samples have been recorded yet: 35 ms,
    // matching the empirical end-to-end pipeline latency (HEVC encode +
    // tethered-USB transport + decode + scan-out). With Mac now using
    // predictor.latest() instead of re-extrapolating, this is the only
    // extrapolation hop, and it must roughly equal the real round-trip
    // for rendered_pose to land at display_pose. The stationary
    // deadband in pose_forwarder protects against gyro-noise
    // amplification when the user holds still.
    static constexpr int64_t kSeedNs = 35'000'000ll;
    // Output clamp range: [10 ms, 120 ms]. Upper bound widened from 80 ms
    // to give the pose forwarder more lookahead headroom when the Mac
    // pipeline genuinely runs slow; ATW + the Mac-side pose predictor's
    // 60 ms cap together absorb the residual error.
    static constexpr int64_t kMinNs = 10'000'000ll;
    static constexpr int64_t kMaxNs = 120'000'000ll;

    // EWMA weight for new samples. Small alpha = stable, slow to react.
    static constexpr float kAlpha = 0.05f;

    // Feed one (frame_arrival_time_ns, header.targetDisplayTimeNs) pair.
    // Both are absolute nanoseconds on the Quest's CLOCK_MONOTONIC /
    // OpenXR display clock (the daemon stamps targetDisplayTimeNs in the
    // Quest's clock domain after clock-sync convergence).
    static void record_sample(uint64_t arrival_ns, uint64_t target_display_ns);

    // Current estimate in nanoseconds, clamped to [kMinNs, kMaxNs].
    // Honors FUVR_QUEST_RTT_OVERRIDE_MS (read once, cached) when set.
    static int64_t current_ns();

private:
    static std::atomic<int64_t> ewma_ns_;
    static int64_t override_ns_();  // -1 if unset.
};

}
