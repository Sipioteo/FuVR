// SPDX-License-Identifier: Apache-2.0

#include "loss_tracker.hpp"

#include <cstdio>

namespace fuvr {

void LossTracker::prune(uint64_t now_ns) {
    while (!loss_times_.empty() && (now_ns - loss_times_.front()) > kWindowNs) {
        loss_times_.pop_front();
    }
}

void LossTracker::note_loss(uint64_t now_ns) {
    prune(now_ns);
    loss_times_.push_back(now_ns);
}

void LossTracker::note_decode_failure(uint64_t /*now_ns*/) {
    decode_failure_pending_ = true;
}

size_t LossTracker::loss_events_in_window(uint64_t now_ns) const {
    size_t count = 0;
    for (uint64_t t : loss_times_) {
        if ((now_ns - t) <= kWindowNs) ++count;
    }
    return count;
}

std::optional<std::string> LossTracker::poll_bitrate_request(uint64_t now_ns) {
    prune(now_ns);
    if (loss_times_.size() <= threshold_) return std::nullopt;
    if ((now_ns - last_bitrate_emit_ns_) < kBitrateRateLimitNs && last_bitrate_emit_ns_ != 0) {
        return std::nullopt;
    }
    last_bitrate_emit_ns_ = now_ns;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "bitrate-req: kbps=%u", (unsigned)kStepDownKbps);
    return std::string(buf);
}

std::optional<std::string> LossTracker::poll_keyframe_request(uint64_t now_ns) {
    if (!decode_failure_pending_) return std::nullopt;
    if ((now_ns - last_keyframe_emit_ns_) < kKeyframeRateLimitNs && last_keyframe_emit_ns_ != 0) {
        return std::nullopt;
    }
    last_keyframe_emit_ns_ = now_ns;
    decode_failure_pending_ = false;
    return std::string("keyframe-req: now");
}

}
