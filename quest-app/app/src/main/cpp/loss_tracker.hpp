// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace fuvr {

// Pure-logic adaptive bitrate / keyframe request tracker.
//
// note_loss(now_ns) is called for every frame that exhibited packet loss
// (fragments missing, reassembly failed). When more than `loss_threshold`
// loss events occur within the trailing 1 s window, poll_bitrate_request()
// returns a `bitrate-req: kbps=N` string (rate-limited to once per second).
//
// note_decode_failure() flags a corrupted decoded frame; poll_keyframe_request
// returns a `keyframe-req: now` string at most once per kKeyframeRateLimitNs.
class LossTracker {
public:
    static constexpr uint32_t kDefaultLossThreshold = 5;
    static constexpr uint64_t kWindowNs            = 1'000'000'000ULL;
    static constexpr uint64_t kBitrateRateLimitNs  = 1'000'000'000ULL;
    static constexpr uint64_t kKeyframeRateLimitNs = 250'000'000ULL;
    static constexpr uint32_t kMinBitrateKbps      = 20'000;
    static constexpr uint32_t kMaxBitrateKbps      = 150'000;
    static constexpr uint32_t kStepDownKbps        = 80'000;

    explicit LossTracker(uint32_t loss_threshold = kDefaultLossThreshold)
        : threshold_(loss_threshold) {}

    void note_loss(uint64_t now_ns);
    void note_decode_failure(uint64_t now_ns);

    // Returns the bitrate-req string, or empty if not due / under threshold.
    std::optional<std::string> poll_bitrate_request(uint64_t now_ns);

    // Returns "keyframe-req: now" if a decode failure is pending and the
    // rate limit allows; else empty.
    std::optional<std::string> poll_keyframe_request(uint64_t now_ns);

    // Inspect the current loss-event count within the trailing window.
    size_t loss_events_in_window(uint64_t now_ns) const;

private:
    void prune(uint64_t now_ns);

    uint32_t threshold_;
    std::deque<uint64_t> loss_times_;
    uint64_t last_bitrate_emit_ns_{0};
    uint64_t last_keyframe_emit_ns_{0};
    bool decode_failure_pending_{false};
};

}
