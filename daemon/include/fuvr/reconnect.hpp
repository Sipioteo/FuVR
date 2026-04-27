// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace fuvr::daemon {

enum class TransportState {
    Connected,
    Disconnected,
};

// Backoff-and-retry state machine for the underlying transport. Pure logic;
// no threading. The owner drives `tick()` and `onConnected()` / `onDisconnect()`.
//
// Behaviour:
//   - On the first transition to Disconnected, fires `onDisconnectEvent` once
//     (so the caller can emit a one-shot error envelope) and arms an attempt
//     after `nextDelay` (starts at 100 ms, doubles to a 5 s cap).
//   - `tick(now)` is the caller's clock heartbeat; when armed and `now >=
//     dueAt`, it returns true so the caller knows to attempt reconnect.
//   - On `onConnected()`, resets the backoff and fires `onReconnectEvent`
//     so the caller can re-issue helloFromMac.
class ReconnectFsm {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr std::chrono::milliseconds kInitialDelay{100};
    static constexpr std::chrono::milliseconds kMaxDelay{5000};

    void onDisconnect(TimePoint now);
    void onConnected();

    // Returns true if `now` has passed the next scheduled attempt time and the
    // FSM is in Disconnected state. The caller is expected to actually try the
    // reconnect; on failure call `markAttemptFailed()` to extend the backoff.
    bool dueForAttempt(TimePoint now) const;

    void markAttemptFailed(TimePoint now);

    [[nodiscard]] TransportState state() const { return state_; }
    [[nodiscard]] std::chrono::milliseconds currentDelay() const { return delay_; }

    // Test hook: how many disconnect transitions have been observed (each
    // contiguous Connected -> Disconnected edge counts once).
    [[nodiscard]] uint64_t disconnectCount() const { return disconnectCount_; }
    [[nodiscard]] uint64_t reconnectCount() const { return reconnectCount_; }

private:
    TransportState state_ = TransportState::Connected;
    std::chrono::milliseconds delay_ = kInitialDelay;
    TimePoint dueAt_{};
    uint64_t disconnectCount_ = 0;
    uint64_t reconnectCount_  = 0;
};

} // namespace fuvr::daemon
