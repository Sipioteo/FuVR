// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fuvr/reconnect.hpp"

using fuvr::daemon::ReconnectFsm;
using fuvr::daemon::TransportState;

TEST(Reconnect, FiresDisconnectOnceAndRecoversOnReconnect) {
    ReconnectFsm fsm;
    auto t0 = ReconnectFsm::Clock::now();

    EXPECT_EQ(fsm.state(), TransportState::Connected);
    fsm.onDisconnect(t0);
    EXPECT_EQ(fsm.state(), TransportState::Disconnected);
    EXPECT_EQ(fsm.disconnectCount(), 1u);

    // Repeated disconnects do not multi-fire.
    fsm.onDisconnect(t0 + std::chrono::milliseconds(50));
    EXPECT_EQ(fsm.disconnectCount(), 1u);

    // Initial backoff is 100 ms; not yet due at t0+10 ms.
    EXPECT_FALSE(fsm.dueForAttempt(t0 + std::chrono::milliseconds(10)));
    EXPECT_TRUE(fsm.dueForAttempt(t0 + std::chrono::milliseconds(150)));

    // Failing the attempt doubles the backoff up to a 5 s cap.
    fsm.markAttemptFailed(t0 + std::chrono::milliseconds(150));
    EXPECT_EQ(fsm.currentDelay(), std::chrono::milliseconds(200));
    fsm.markAttemptFailed(t0 + std::chrono::milliseconds(400));
    EXPECT_EQ(fsm.currentDelay(), std::chrono::milliseconds(400));
    for (int i = 0; i < 10; ++i) {
        fsm.markAttemptFailed(t0 + std::chrono::seconds(1 + i));
    }
    EXPECT_EQ(fsm.currentDelay(), ReconnectFsm::kMaxDelay);

    // Reconnect resets and counts.
    fsm.onConnected();
    EXPECT_EQ(fsm.state(), TransportState::Connected);
    EXPECT_EQ(fsm.reconnectCount(), 1u);
    EXPECT_EQ(fsm.currentDelay(), ReconnectFsm::kInitialDelay);

    // Second disconnect cycle counts independently.
    fsm.onDisconnect(t0 + std::chrono::seconds(20));
    EXPECT_EQ(fsm.disconnectCount(), 2u);
}
