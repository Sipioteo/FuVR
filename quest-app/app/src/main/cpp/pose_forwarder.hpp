// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <thread>

namespace fuvr {

class OpenXrSession;
class TransportClient;

class PoseForwarder {
public:
    PoseForwarder(OpenXrSession& xr, TransportClient& tx) : xr_(xr), tx_(tx) {}
    ~PoseForwarder() { stop(); }

    void start();
    void stop();

private:
    void run();

    OpenXrSession& xr_;
    TransportClient& tx_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}
