// SPDX-License-Identifier: Apache-2.0
#include "fuvr/reconnect.hpp"

namespace fuvr::daemon {

void ReconnectFsm::onDisconnect(TimePoint now) {
    if (state_ == TransportState::Disconnected) return;
    state_ = TransportState::Disconnected;
    delay_ = kInitialDelay;
    dueAt_ = now + delay_;
    ++disconnectCount_;
}

void ReconnectFsm::onConnected() {
    bool wasDown = (state_ == TransportState::Disconnected);
    state_ = TransportState::Connected;
    delay_ = kInitialDelay;
    dueAt_ = TimePoint{};
    if (wasDown) ++reconnectCount_;
}

bool ReconnectFsm::dueForAttempt(TimePoint now) const {
    return state_ == TransportState::Disconnected && now >= dueAt_;
}

void ReconnectFsm::markAttemptFailed(TimePoint now) {
    auto next = delay_ * 2;
    if (next > kMaxDelay) next = kMaxDelay;
    delay_ = next;
    dueAt_ = now + delay_;
}

} // namespace fuvr::daemon
