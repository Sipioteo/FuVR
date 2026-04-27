// SPDX-License-Identifier: Apache-2.0
#include "fuvr/rpc_server.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

#include <capnp/serialize-packed.h>
#include <kj/io.h>

namespace fuvr::daemon {

namespace {
std::mutex& sendMutex() { static std::mutex m; return m; }
}

std::string defaultSocketPath() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    std::string base;
    if (xdg && *xdg) {
        base = std::string(xdg) + "/fuvr";
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home ? home : "/tmp") + "/Library/Caches/fuvr";
    }
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    ::chmod(base.c_str(), 0700);
    return base + "/rpc.sock";
}

RpcServer::RpcServer() = default;

RpcServer::~RpcServer() { stop(); }

bool RpcServer::start(const std::string& path, EnvelopeHandler handler) {
    socketPath_ = path;
    handler_ = std::move(handler);
    ::unlink(socketPath_.c_str());

    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listenFd_); listenFd_ = -1; return false;
    }
    ::chmod(socketPath_.c_str(), 0600);

    if (::listen(listenFd_, 8) < 0) {
        ::close(listenFd_); listenFd_ = -1; return false;
    }

    running_.store(true);
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void RpcServer::stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    for (auto& t : clientThreads_) if (t.joinable()) t.join();
    clientThreads_.clear();
    if (!socketPath_.empty()) ::unlink(socketPath_.c_str());
}

void RpcServer::acceptLoop() {
    while (running_.load()) {
        int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) {
            if (!running_.load()) break;
            continue;
        }
        clientThreads_.emplace_back([this, fd] { clientLoop(fd); });
    }
}

namespace {
// Read one length-prefixed packed envelope. Why: framing is needed because
// SCM_RIGHTS arrives via recvmsg, not the streamed fd reader.
bool readEnvelope(int fd, InboundRpc& out) {
    uint32_t len = 0;
    char ctrl[CMSG_SPACE(sizeof(int) * 16)];
    iovec iov{};
    iov.iov_base = &len;
    iov.iov_len = sizeof(len);
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);

    ssize_t n = ::recvmsg(fd, &msg, 0);
    if (n <= 0) return false;
    if (n < static_cast<ssize_t>(sizeof(len))) return false;

    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            std::size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int* fds = reinterpret_cast<const int*>(CMSG_DATA(c));
            for (std::size_t i = 0; i < count; ++i) {
                out.machPorts.push_back(static_cast<mach_port_t>(fds[i]));
            }
        }
    }

    if (len == 0 || len > (8u * 1024u * 1024u)) return false;
    out.envelope.resize(len);
    std::size_t got = 0;
    while (got < len) {
        ssize_t r = ::read(fd, out.envelope.data() + got, len - got);
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}
}

void RpcServer::clientLoop(int fd) {
    while (running_.load()) {
        InboundRpc rpc;
        rpc.clientFd = fd;
        if (!readEnvelope(fd, rpc)) break;
        if (handler_) handler_(rpc);
    }
    ::close(fd);
}

bool RpcServer::send(int clientFd, const uint8_t* data, std::size_t len) {
    std::lock_guard lk(sendMutex());
    uint32_t l = static_cast<uint32_t>(len);
    if (::write(clientFd, &l, sizeof(l)) != sizeof(l)) return false;
    std::size_t off = 0;
    while (off < len) {
        ssize_t w = ::write(clientFd, data + off, len - off);
        if (w <= 0) return false;
        off += static_cast<std::size_t>(w);
    }
    return true;
}

} // namespace fuvr::daemon
