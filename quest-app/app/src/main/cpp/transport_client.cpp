// SPDX-License-Identifier: Apache-2.0

#include "transport_client.hpp"

#include <android/log.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <unordered_map>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "fuvr.tx", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "fuvr.tx", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.tx", __VA_ARGS__)

namespace fuvr {

namespace {
bool read_exact(int fd, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

// Wire layout — must match transport/transport-udp/src/lib.rs `PktHeader`.
//   u8  channel
//   u64 seq            (LE)
//   u32 shard_idx      (LE)
//   u32 shard_count    (LE)
//   u16 data_shards    (LE)
//   u16 _reserved      (LE)
//   u32 original_len   (LE)
constexpr size_t kPktHeaderSize = 1 + 8 + 4 + 4 + 2 + 2 + 4;
constexpr size_t kMtuPayload    = 1450;
constexpr size_t kMaxDatagram   = kPktHeaderSize + kMtuPayload + 64;
constexpr int    kReassemblyTimeoutMs = 200;

inline uint16_t rd_u16le(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t rd_u32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint64_t rd_u64le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= uint64_t(p[i]) << (8 * i);
    return v;
}
inline void wr_u16le(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void wr_u32le(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}
inline void wr_u64le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = uint8_t(v >> (8 * i));
}

struct PktHeader {
    uint8_t  channel;
    uint64_t seq;
    uint32_t shard_idx;
    uint32_t shard_count;
    uint16_t data_shards;
    uint32_t original_len;

    void encode(uint8_t* out) const {
        out[0] = channel;
        wr_u64le(out + 1, seq);
        wr_u32le(out + 9, shard_idx);
        wr_u32le(out + 13, shard_count);
        wr_u16le(out + 17, data_shards);
        wr_u16le(out + 19, 0);
        wr_u32le(out + 21, original_len);
    }
    static bool decode(const uint8_t* in, size_t n, PktHeader& out) {
        if (n < kPktHeaderSize) return false;
        out.channel      = in[0];
        out.seq          = rd_u64le(in + 1);
        out.shard_idx    = rd_u32le(in + 9);
        out.shard_count  = rd_u32le(in + 13);
        out.data_shards  = rd_u16le(in + 17);
        out.original_len = rd_u32le(in + 21);
        return true;
    }
};

uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct ReassemblyEntry {
    std::vector<std::vector<uint8_t>> shards;  // sized to shard_count
    std::vector<uint8_t> presence;             // 0/1
    uint32_t received{0};
    uint32_t data_shards{0};
    uint32_t original_len{0};
    uint64_t deadline_ms{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// TCP path (legacy fallback)
// ---------------------------------------------------------------------------

bool TransportClient::connect_tcp_locked() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("connect %s:%u failed: %s", host_.c_str(), port_, strerror(errno));
        ::close(fd);
        return false;
    }
    fd_ = fd;
    LOGI("transport(tcp) connected to %s:%u (fd=%d)", host_.c_str(), port_, fd);
    return true;
}

void TransportClient::close_tcp_locked() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

bool TransportClient::start(const std::string& host, uint16_t port) {
    mode_ = TransportMode::LegacyTcp;
    host_ = host;
    port_ = port;
    {
        std::lock_guard<std::mutex> lk(send_mutex_);
        if (!connect_tcp_locked()) {
            LOGI("initial tcp connect failed; entering reconnect loop");
        }
    }
    running_ = true;
    thread_ = std::thread(&TransportClient::recv_loop_tcp, this);
    return true;
}

void TransportClient::recv_loop_tcp() {
    std::vector<uint8_t> buf;
    int backoff_ms = 100;
    while (running_.load()) {
        if (fd_ < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, 2000);
            std::lock_guard<std::mutex> lk(send_mutex_);
            if (!connect_tcp_locked()) continue;
            backoff_ms = 100;
        }
        uint8_t hdr[5];
        if (!read_exact(fd_, hdr, 5)) {
            LOGI("recv_loop_tcp: read failed, reconnecting");
            std::lock_guard<std::mutex> lk(send_mutex_);
            close_tcp_locked();
            continue;
        }
        uint32_t len_plus_ch;
        std::memcpy(&len_plus_ch, hdr, 4);
        uint32_t total = ntohl(len_plus_ch);
        if (total == 0) continue;
        Channel ch = static_cast<Channel>(hdr[4]);
        size_t payload_size = total - 1;
        buf.resize(payload_size);
        if (!read_exact(fd_, buf.data(), payload_size)) {
            LOGI("recv_loop_tcp: payload read failed, reconnecting");
            std::lock_guard<std::mutex> lk(send_mutex_);
            close_tcp_locked();
            continue;
        }
        if (handler_) handler_(ch, buf.data(), payload_size);
    }
}

// ---------------------------------------------------------------------------
// UDP path (RNDIS tethering)
// ---------------------------------------------------------------------------

bool TransportClient::open_udp_locked(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGE("udp socket() failed: %s", strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOGE("udp bind 0.0.0.0:%u failed: %s", port, strerror(errno));
        ::close(fd);
        return false;
    }
    fd_ = fd;
    LOGI("transport(udp) listening on 0.0.0.0:%u (fd=%d)", port, fd);
    return true;
}

void TransportClient::close_udp_locked() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    {
        std::lock_guard<std::mutex> lk(peer_mutex_);
        peer_known_ = false;
    }
}

bool TransportClient::start_udp(uint16_t port) {
    mode_ = TransportMode::UdpRndis;
    port_ = port;
    {
        std::lock_guard<std::mutex> lk(send_mutex_);
        if (!open_udp_locked(port)) {
            LOGE("udp open failed");
            return false;
        }
    }
    running_ = true;
    thread_ = std::thread(&TransportClient::recv_loop_udp, this);
    return true;
}

void TransportClient::recv_loop_udp() {
    std::vector<uint8_t> buf(kMaxDatagram);
    std::unordered_map<uint64_t, ReassemblyEntry> pending;
    uint64_t last_gc_ms = now_ms();

    while (running_.load()) {
        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0, (sockaddr*)&from, &fromlen);
        if (n <= 0) {
            if (!running_.load()) break;
            if (errno == EINTR) continue;
            LOGW("udp recvfrom: %s", strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // Latch peer on first datagram so send() targets the Mac.
        {
            std::lock_guard<std::mutex> lk(peer_mutex_);
            if (!peer_known_) {
                peer_addr_ = from;
                peer_known_ = true;
                char ip[INET_ADDRSTRLEN]{0};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                LOGI("udp peer latched: %s:%u", ip, ntohs(from.sin_port));
            }
        }

        PktHeader h{};
        if (!PktHeader::decode(buf.data(), size_t(n), h)) continue;
        const uint8_t* body = buf.data() + kPktHeaderSize;
        size_t body_len = size_t(n) - kPktHeaderSize;

        // Heartbeat sentinel: 4 zero bytes followed by "fuvr-hb" — drop silently.
        if (h.shard_count == 0 || h.original_len == 0 || h.data_shards == 0) {
            // Likely a malformed/heartbeat datagram. Ignore.
            continue;
        }

        // Compose key from (channel, seq) into a single 64-bit value:
        // top 8 bits = channel, low 56 bits = seq (more than enough headroom).
        uint64_t key = (uint64_t(h.channel) << 56) | (h.seq & 0x00FFFFFFFFFFFFFFull);
        auto it = pending.find(key);
        if (it == pending.end()) {
            ReassemblyEntry e;
            e.shards.assign(h.shard_count, {});
            e.presence.assign(h.shard_count, 0);
            e.data_shards = h.data_shards;
            e.original_len = h.original_len;
            e.deadline_ms = now_ms() + kReassemblyTimeoutMs;
            it = pending.emplace(key, std::move(e)).first;
        }
        auto& entry = it->second;
        if (h.shard_idx < entry.shards.size() && !entry.presence[h.shard_idx]) {
            entry.shards[h.shard_idx].assign(body, body + body_len);
            entry.presence[h.shard_idx] = 1;
            entry.received++;
        }

        // Fast path: all data shards present → concatenate. (Reed-Solomon
        // parity reconstruction for the case where some data shards are
        // missing but parity arrived is intentionally not implemented yet
        // on the Quest side; over a stable RNDIS link, observed loss is
        // <0.1% so the fast path covers >99% of frames. Frames with missing
        // data shards are dropped after the 200ms timeout. Tracked for a
        // future iteration.)
        bool data_complete = true;
        for (uint32_t i = 0; i < entry.data_shards; i++) {
            if (!entry.presence[i]) { data_complete = false; break; }
        }
        if (data_complete) {
            std::vector<uint8_t> payload;
            payload.reserve(entry.original_len);
            for (uint32_t i = 0; i < entry.data_shards; i++) {
                payload.insert(payload.end(), entry.shards[i].begin(), entry.shards[i].end());
            }
            payload.resize(entry.original_len);
            Channel ch = static_cast<Channel>(h.channel);
            if (handler_) handler_(ch, payload.data(), payload.size());
            pending.erase(it);
        }

        // Periodic GC of stale partial frames.
        uint64_t t = now_ms();
        if (t - last_gc_ms > 50) {
            for (auto pit = pending.begin(); pit != pending.end();) {
                if (pit->second.deadline_ms <= t) pit = pending.erase(pit);
                else                              ++pit;
            }
            last_gc_ms = t;
        }
    }
}

bool TransportClient::send_udp(Channel ch, const uint8_t* data, size_t size) {
    sockaddr_in peer;
    {
        std::lock_guard<std::mutex> lk(peer_mutex_);
        if (!peer_known_) return false;
        peer = peer_addr_;
    }
    // Single-shard fragmentation path: when payload <= mtu_payload we send
    // it as data_shards=1, shard_count=1 (no parity). The Mac side decodes
    // this as the "all-data-present" fast path. Larger payloads on the
    // Quest→Mac direction (input/pose/control) are well under one MTU.
    if (size > kMtuPayload) {
        // Quest→Mac payloads (pose/input) are tiny; bail loudly so we
        // don't silently corrupt frames if this assumption ever breaks.
        LOGE("udp send: payload %zu > mtu %zu (q→m fragmentation not implemented)",
             size, kMtuPayload);
        return false;
    }
    uint64_t seq = udp_seq_.fetch_add(1, std::memory_order_relaxed);
    uint8_t buf[kPktHeaderSize + kMtuPayload];
    PktHeader h{
        .channel = uint8_t(ch),
        .seq = seq,
        .shard_idx = 0,
        .shard_count = 1,
        .data_shards = 1,
        .original_len = uint32_t(size),
    };
    h.encode(buf);
    std::memcpy(buf + kPktHeaderSize, data, size);
    ssize_t s = ::sendto(fd_, buf, kPktHeaderSize + size, 0,
                        (sockaddr*)&peer, sizeof(peer));
    return s == ssize_t(kPktHeaderSize + size);
}

// ---------------------------------------------------------------------------
// Common
// ---------------------------------------------------------------------------

void TransportClient::stop() {
    running_ = false;
    {
        std::lock_guard<std::mutex> lk(send_mutex_);
        if (mode_ == TransportMode::UdpRndis) close_udp_locked();
        else                                  close_tcp_locked();
    }
    if (thread_.joinable()) thread_.join();
}

bool TransportClient::send(Channel ch, const uint8_t* data, size_t size) {
    if (mode_ == TransportMode::UdpRndis) {
        std::lock_guard<std::mutex> lk(send_mutex_);
        if (fd_ < 0) return false;
        return send_udp(ch, data, size);
    }
    // TCP path.
    std::lock_guard<std::mutex> lk(send_mutex_);
    if (fd_ < 0) return false;
    uint8_t hdr[5];
    uint32_t be = htonl(static_cast<uint32_t>(size + 1));
    std::memcpy(hdr, &be, 4);
    hdr[4] = static_cast<uint8_t>(ch);
    if (::send(fd_, hdr, 5, MSG_NOSIGNAL) != 5) {
        close_tcp_locked();
        return false;
    }
    size_t off = 0;
    while (off < size) {
        ssize_t s = ::send(fd_, data + off, size - off, MSG_NOSIGNAL);
        if (s <= 0) {
            close_tcp_locked();
            return false;
        }
        off += s;
    }
    return true;
}

void TransportClient::set_handler(Handler h) { handler_ = std::move(h); }

}  // namespace fuvr
