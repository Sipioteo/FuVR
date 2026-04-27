// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <mach/mach.h>

namespace fuvr::daemon {

// One incoming RPC. `envelope` is packed Cap'n Proto bytes; `machPorts` is
// the SCM_RIGHTS payload (used for IOSurface mach send-rights).
struct InboundRpc {
    int clientFd = -1;
    std::vector<uint8_t> envelope;
    std::vector<mach_port_t> machPorts;
};

using EnvelopeHandler = std::function<void(const InboundRpc&)>;

// Listens on a Unix domain socket, reads one packed Envelope per receive,
// and invokes the handler. Why: a single accept loop in a dedicated thread
// keeps the rest of the daemon free of socket I/O concerns.
class RpcServer {
public:
    RpcServer();
    ~RpcServer();

    bool start(const std::string& socketPath, EnvelopeHandler handler);
    void stop();

    // Send a packed Envelope back to a specific client. Thread-safe.
    bool send(int clientFd, const uint8_t* data, std::size_t len);

    [[nodiscard]] const std::string& socketPath() const { return socketPath_; }

private:
    void acceptLoop();
    void clientLoop(int fd);

    std::string socketPath_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::vector<std::thread> clientThreads_;
    EnvelopeHandler handler_;
};

// Resolve the daemon's socket path: $XDG_RUNTIME_DIR/fuvr/rpc.sock or
// fallback ~/Library/Caches/fuvr/rpc.sock. Creates the parent dir 0700.
std::string defaultSocketPath();

} // namespace fuvr::daemon
