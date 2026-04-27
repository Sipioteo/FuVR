// SPDX-License-Identifier: Apache-2.0

#include "transport_client.hpp"

#include <android/log.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

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

bool TransportClient::start(const std::string& host, uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    int one = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::connect(fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("connect %s:%u failed: %s", host.c_str(), port, strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    running_ = true;
    thread_ = std::thread(&TransportClient::recv_loop, this);
    LOGI("transport connected to %s:%u", host.c_str(), port);
    return true;
}

void TransportClient::stop() {
    running_ = false;
    if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
    if (thread_.joinable()) thread_.join();
}

bool TransportClient::send(Channel ch, const uint8_t* data, size_t size) {
    if (fd_ < 0) return false;
    std::lock_guard<std::mutex> lk(send_mutex_);
    uint8_t hdr[5];
    uint32_t be = htonl(static_cast<uint32_t>(size + 1));
    std::memcpy(hdr, &be, 4);
    hdr[4] = static_cast<uint8_t>(ch);
    if (::send(fd_, hdr, 5, MSG_NOSIGNAL) != 5) return false;
    size_t off = 0;
    while (off < size) {
        ssize_t s = ::send(fd_, data + off, size - off, MSG_NOSIGNAL);
        if (s <= 0) return false;
        off += s;
    }
    return true;
}

void TransportClient::set_handler(Handler h) { handler_ = std::move(h); }

void TransportClient::recv_loop() {
    std::vector<uint8_t> buf;
    while (running_.load()) {
        uint8_t hdr[5];
        if (!read_exact(fd_, hdr, 5)) break;
        uint32_t len_plus_ch;
        std::memcpy(&len_plus_ch, hdr, 4);
        uint32_t total = ntohl(len_plus_ch);
        if (total == 0) continue;
        Channel ch = static_cast<Channel>(hdr[4]);
        size_t payload_size = total - 1;
        buf.resize(payload_size);
        if (!read_exact(fd_, buf.data(), payload_size)) break;
        if (handler_) handler_(ch, buf.data(), payload_size);
    }
}

}
