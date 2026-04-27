// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fuvr {

// Pure-logic responder for the ControlMessage.clockSync union arm.
//
// Per proto/fuvr.capnp: a Ping{ t0 } from the Mac is answered with a
// Pong{ t0, t1=now_ns_at_receive, t2=now_ns_at_send }. The Mac uses
// (t0, t1, t2, t3=its-own-receive-now) to derive RTT and skew per NTP.
//
// Quest never spontaneously emits pings; pure responder.
struct PongTimestamps {
    uint64_t t0{0};
    uint64_t t1{0};
    uint64_t t2{0};
};

class ClockSyncResponder {
public:
    // Read steady_clock now in nanoseconds (monotonic).
    static uint64_t now_ns();

    // Build a Pong reply for a ping received at receive_ns, with the send
    // timestamp captured immediately before the actual transport send.
    // t1 = receive timestamp, t2 = send timestamp.
    static PongTimestamps build_pong(uint64_t ping_t0, uint64_t receive_ns,
                                     uint64_t send_ns);

    // Convenience: capture both t1 and t2 from now_ns(). The caller is
    // expected to pass send_ns as a fresh now_ns() call right before
    // pushing bytes onto the wire.
    static PongTimestamps build_pong_now(uint64_t ping_t0);
};

}
