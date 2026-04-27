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

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.tx", __VA_ARGS__)
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
}

bool TransportClient::connect_locked() {
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
    LOGI("transport connected to %s:%u (fd=%d)", host_.c_str(), port_, fd);
    return true;
}

void TransportClient::close_fd_locked() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

bool TransportClient::start(const std::string& host, uint16_t port) {
    host_ = host;
    port_ = port;
    {
        std::lock_guard<std::mutex> lk(send_mutex_);
        if (!connect_locked()) {
            // Initial connect failed but we still start the recv loop — it
            // will keep retrying with backoff until the daemon comes up.
            LOGI("initial connect failed; entering reconnect loop");
        }
    }
    running_ = true;
    thread_ = std::thread(&TransportClient::recv_loop, this);
    return true;
}

void TransportClient::stop() {
    running_ = false;
    {
        std::lock_guard<std::mutex> lk(send_mutex_);
        close_fd_locked();
    }
    if (thread_.joinable()) thread_.join();
}

bool TransportClient::send(Channel ch, const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lk(send_mutex_);
    if (fd_ < 0) return false;  // Reconnect happens in recv_loop on next read.
    uint8_t hdr[5];
    uint32_t be = htonl(static_cast<uint32_t>(size + 1));
    std::memcpy(hdr, &be, 4);
    hdr[4] = static_cast<uint8_t>(ch);
    if (::send(fd_, hdr, 5, MSG_NOSIGNAL) != 5) {
        // Why: send failure = peer is dead. Mark fd as broken so recv_loop
        // observes the same and triggers reconnect. Without this, send keeps
        // returning false on the same dead fd forever (the symptom the user
        // hit: sends_ok stuck at 940 while sends_fail rose to 80k).
        close_fd_locked();
        return false;
    }
    size_t off = 0;
    while (off < size) {
        ssize_t s = ::send(fd_, data + off, size - off, MSG_NOSIGNAL);
        if (s <= 0) {
            close_fd_locked();
            return false;
        }
        off += s;
    }
    return true;
}

void TransportClient::set_handler(Handler h) { handler_ = std::move(h); }

void TransportClient::recv_loop() {
    std::vector<uint8_t> buf;
    int backoff_ms = 100;
    while (running_.load()) {
        // If we have no fd, reconnect with capped exponential backoff.
        if (fd_ < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, 2000);
            std::lock_guard<std::mutex> lk(send_mutex_);
            if (!connect_locked()) continue;
            backoff_ms = 100;
        }
        // Read one frame; on failure clear fd and let the next loop iteration
        // reconnect.
        uint8_t hdr[5];
        if (!read_exact(fd_, hdr, 5)) {
            LOGI("recv_loop: read failed, reconnecting");
            std::lock_guard<std::mutex> lk(send_mutex_);
            close_fd_locked();
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
            LOGI("recv_loop: payload read failed, reconnecting");
            std::lock_guard<std::mutex> lk(send_mutex_);
            close_fd_locked();
            continue;
        }
        if (handler_) handler_(ch, buf.data(), payload_size);
    }
}

}
