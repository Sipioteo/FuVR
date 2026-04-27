// SPDX-License-Identifier: Apache-2.0
#include "round_trip.hpp"

#include <cstdlib>
#include <cstring>

namespace fuvr {

std::atomic<int64_t> RoundTripEstimator::ewma_ns_{RoundTripEstimator::kSeedNs};

int64_t RoundTripEstimator::override_ns_() {
    // Read FUVR_QUEST_RTT_OVERRIDE_MS once and cache. -1 means "no override".
    // Function-local static initializer is thread-safe under C++11+.
    static const int64_t cached = []() -> int64_t {
        const char* env = std::getenv("FUVR_QUEST_RTT_OVERRIDE_MS");
        if (env == nullptr || *env == '\0') return -1;
        char* end = nullptr;
        long ms = std::strtol(env, &end, 10);
        if (end == env || ms < 0) return -1;
        return static_cast<int64_t>(ms) * 1'000'000ll;
    }();
    return cached;
}

void RoundTripEstimator::record_sample(uint64_t arrival_ns,
                                        uint64_t target_display_ns) {
    // arrival - target: positive means the frame landed AFTER the display
    // time it was destined for (typical case — the daemon stamps a display
    // time slightly in the past from the Quest's POV by the time the frame
    // arrives). We treat this gap as the round-trip surrogate.
    //
    // Negative or absurd values (clock-sync still warming up, or a very
    // early frame) are dropped: a single bad sample with EWMA α=0.05 still
    // pulls the estimate visibly off, and the bound clamp at the read site
    // would mask it.
    const int64_t raw =
        static_cast<int64_t>(arrival_ns) - static_cast<int64_t>(target_display_ns);
    if (raw < 0 || raw > 500'000'000ll) {
        return;  // implausible — likely clock-sync drift or a stale frame.
    }
    int64_t prev = ewma_ns_.load(std::memory_order_relaxed);
    const int64_t next = static_cast<int64_t>(
        static_cast<float>(prev) +
        kAlpha * (static_cast<float>(raw) - static_cast<float>(prev)));
    ewma_ns_.store(next, std::memory_order_relaxed);
}

int64_t RoundTripEstimator::current_ns() {
    const int64_t override_v = override_ns_();
    const int64_t raw =
        (override_v >= 0) ? override_v
                          : ewma_ns_.load(std::memory_order_relaxed);
    if (raw < kMinNs) return kMinNs;
    if (raw > kMaxNs) return kMaxNs;
    return raw;
}

}
