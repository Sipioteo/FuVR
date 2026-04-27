// SPDX-License-Identifier: Apache-2.0

#include "clock_sync.hpp"

#include <chrono>

namespace fuvr {

uint64_t ClockSyncResponder::now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

PongTimestamps ClockSyncResponder::build_pong(uint64_t ping_t0,
                                              uint64_t receive_ns,
                                              uint64_t send_ns) {
    PongTimestamps p;
    p.t0 = ping_t0;
    p.t1 = receive_ns;
    // Why: t2 must be >= t1 for the Mac's RTT/offset math to remain
    // physically meaningful; clamp to defend against clock jitter or a
    // caller that built receive_ns later than send_ns by mistake.
    p.t2 = (send_ns >= receive_ns) ? send_ns : receive_ns;
    return p;
}

PongTimestamps ClockSyncResponder::build_pong_now(uint64_t ping_t0) {
    const uint64_t t1 = now_ns();
    const uint64_t t2 = now_ns();
    return build_pong(ping_t0, t1, t2);
}

}
