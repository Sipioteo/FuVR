// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fuvr {

enum class Channel : uint8_t {
    Video   = 0,
    Audio   = 1,
    Pose    = 2,
    Input   = 3,
    Haptics = 4,
    Control = 5,
};

class TransportClient {
public:
    using Handler = std::function<void(Channel, const uint8_t*, size_t)>;

    ~TransportClient() { stop(); }

    bool start(const std::string& host, uint16_t port);
    void stop();

    // 4-byte BE length, 1-byte channel id, payload. Matches Rust transport crate.
    bool send(Channel ch, const uint8_t* data, size_t size);

    void set_handler(Handler h);

private:
    void recv_loop();

    int fd_{-1};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex send_mutex_;
    Handler handler_;
};

}
