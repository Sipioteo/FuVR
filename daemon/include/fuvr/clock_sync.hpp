// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>

namespace fuvr::daemon {

// NTP-style two-way clock sync between Mac (initiator) and Quest (responder).
//
// Mac sends ControlMessage.clockSync.ping(t0 = Mac steady_clock ns at send).
// Quest replies ControlMessage.clockSync.pong(t0, t1, t2) where t1 is the
// Quest steady_clock at receive and t2 is the Quest steady_clock at reply
// send. Mac records T_recv on pong arrival and computes per RFC-2030:
//
//   oneWayDelayNs = ((T_recv - T_send) - (t2 - t1)) / 2
//   offsetNs      = ((t1 - T_send) + (t2 - T_recv)) / 2  (Quest = Mac + offset)
//
// snapshot() returns the median of up to 16 recent samples. Why median: a
// single packet stuck behind a USB bulk burst skews the mean noticeably; the
// median is robust at the cost of two extra sorts per second, negligible at
// 1 Hz.
class ClockSync {
public:
    struct Snapshot {
        int64_t  offsetNs       = 0;
        uint64_t oneWayDelayNs  = 0;
        uint32_t samples        = 0;
        double   varianceNs     = 0.0;
    };

    using SendFn = std::function<void(const uint8_t* data, std::size_t len)>;

    // Build a packed ControlMessage{ ping{ t0 } } and hand it to `send`.
    // Records T_send keyed by t0 so a later onPong can compute offset/delay.
    void issuePing(const SendFn& send);

    // Feed a parsed pong into the rolling window. Drops the entry if no
    // matching ping was issued or if T_send is older than 5 s.
    void onPong(uint64_t t0, uint64_t t1, uint64_t t2);

    // Read-only window summary; returns zeros if no samples yet.
    [[nodiscard]] Snapshot snapshot() const;

    // Wait up to `timeout` for at least one sample, returning the snapshot.
    // Used at session start so StartSessionResponse can carry a real offset.
    [[nodiscard]] Snapshot waitForFirst(std::chrono::milliseconds timeout) const;

    // Test hook: feed a synthetic exchange directly.
    void feedSynthetic(uint64_t tSendNs, uint64_t t1Ns, uint64_t t2Ns,
                       uint64_t tRecvNs);

private:
    static constexpr std::size_t kWindow = 16;
    static constexpr uint64_t    kMaxAgeNs = 5'000'000'000ULL;

    struct Sample {
        uint64_t recordedAtNs;
        int64_t  offsetNs;
        uint64_t oneWayDelayNs;
    };

    struct Pending {
        uint64_t tSendNs;
    };

    void pushSampleLocked(int64_t offsetNs, uint64_t delayNs, uint64_t nowNs);
    void evictOldLocked(uint64_t nowNs);

    mutable std::mutex                    mu_;
    std::deque<Sample>                    samples_;
    // Outstanding pings keyed by t0 (Mac steady_clock at send).
    std::deque<std::pair<uint64_t, Pending>> pending_;
};

} // namespace fuvr::daemon
