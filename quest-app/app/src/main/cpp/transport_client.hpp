// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>

namespace fuvr {

// Why: channel ids must match the Rust transport crate's `Channel` enum at
// transport/transport-core/src/channel.rs exactly — that crate is the
// single source of truth for the wire framing across all FuVR endpoints.
// Audited against commit 75fb94b: order is identical.
enum class Channel : uint8_t {
    Video   = 0,
    Audio   = 1,
    Pose    = 2,
    Input   = 3,
    Haptics = 4,
    Control = 5,
};

enum class TransportMode : uint8_t {
    /// Legacy: TCP loopback over `adb reverse`. Kept as a fallback path.
    LegacyTcp,
    /// UDP over RNDIS USB tethering. Listens on 0.0.0.0:UDP_VR_PORT and
    /// targets the Mac on the 192.168.42.0/24 subnet.
    UdpRndis,
};

class TransportClient {
public:
    using Handler = std::function<void(Channel, const uint8_t*, size_t)>;

    /// Default UDP port matching `transport_udp::DEFAULT_RNDIS_PORT` on the Mac.
    static constexpr uint16_t kUdpVrPort = 59000;

    ~TransportClient() { stop(); }

    /// Connect via legacy TCP-over-`adb reverse`. Mac listens, Quest connects.
    bool start(const std::string& host, uint16_t port);

    /// Bind a UDP socket on `0.0.0.0:port` and listen for datagrams from the
    /// Mac. The peer address is auto-learned from the first inbound packet
    /// (the Mac heartbeat ensures this lands within ~500ms of the Quest
    /// enabling USB Tethering). All sends are addressed to the latched peer.
    bool start_udp(uint16_t port = kUdpVrPort);

    void stop();

    /// 4-byte BE length, 1-byte channel id, payload (TCP) — or
    /// fragmented + FEC-encoded UDP datagrams matching `PktHeader` in
    /// `transport-udp/src/lib.rs`.
    bool send(Channel ch, const uint8_t* data, size_t size);

    void set_handler(Handler h);

    TransportMode mode() const { return mode_; }

    /// True once a UDP datagram from the daemon has been received and the
    /// peer address has been latched. Used by `main.cpp` to decide whether
    /// to keep the UDP path or fall back to TCP — `bind(0.0.0.0, 59000)`
    /// always succeeds, so a successful bind does NOT prove the macOS
    /// host actually supports the RNDIS function class. Polling this flag
    /// for ~2s after `start_udp` gives a reliable answer.
    bool peer_known() const {
        std::lock_guard<std::mutex> lk(peer_mutex_);
        return peer_known_;
    }

private:
    // ---- TCP path ----
    void recv_loop_tcp();
    bool connect_tcp_locked();
    void close_tcp_locked();

    // ---- UDP path ----
    void recv_loop_udp();
    bool open_udp_locked(uint16_t port);
    void close_udp_locked();
    bool send_udp(Channel ch, const uint8_t* data, size_t size);

    int fd_{-1};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex send_mutex_;
    Handler handler_;

    // TCP state
    std::string host_;
    uint16_t port_{0};

    // UDP state
    TransportMode mode_{TransportMode::LegacyTcp};
    std::atomic<uint64_t> udp_seq_{1};
    // `mutable` so the const `peer_known()` accessor can lock — the mutex
    // protects state, not the logical const-ness of the object.
    mutable std::mutex peer_mutex_;
    bool peer_known_{false};
    sockaddr_in peer_addr_{};
};

}
