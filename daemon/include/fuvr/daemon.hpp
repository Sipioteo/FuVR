// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "fuvr/clock_sync.hpp"
#include "fuvr/input_router.hpp"
#include "fuvr/iosurface_xpc_service.hpp"
#include "fuvr/metrics.hpp"
#include "fuvr/pose_router.hpp"
#include "fuvr/rpc_server.hpp"
#include "fuvr/session.hpp"

struct FuvrTransport;

namespace fuvr::daemon {

class Daemon {
public:
    Daemon();
    ~Daemon();

    bool start(const std::string& socketPath = {});
    void stop();

    PoseRouter& poseRouter() { return poseRouter_; }
    InputRouter& inputRouter() { return inputRouter_; }
    MetricsAggregator& globalMetrics() { return globalMetrics_; }
    ClockSync& clockSync() { return clockSync_; }

private:
    void onEnvelope(const InboundRpc& rpc);
    void metricsLoop();
    void clockSyncLoop();
    void dispatchEncodeStats(const EncodeStatsEvent& ev);
    void handleControlMessage(const uint8_t* data, std::size_t len);
    static void onTransportRecv(void* user, uint8_t channel,
                                const uint8_t* data, std::size_t len);

    RpcServer rpc_;
    PoseRouter poseRouter_;
    InputRouter inputRouter_;
    MetricsAggregator globalMetrics_;

    std::mutex sessionsMu_;
    std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions_;
    uint64_t nextSessionId_ = 1;

    struct MetricsSubscriber { int fd; uint64_t streamId; };
    std::mutex metricsSubsMu_;
    std::vector<MetricsSubscriber> metricsSubs_;

    struct EncodeStatsSubscriber { int fd; uint64_t streamId; };
    std::mutex encodeStatsSubsMu_;
    std::vector<EncodeStatsSubscriber> encodeStatsSubs_;
    std::atomic<bool> piggybackWarnLogged_{false};

    struct LogSubscriber { int fd; uint64_t streamId; };
    std::mutex logSubsMu_;
    std::vector<LogSubscriber> logSubs_;

    // Q-side metrics, fed from `q-metrics:` lines on the control channel.
    std::mutex qMetricsMu_;
    float qDecoderFps_         = 0.0f;
    float qDecoderDecodeMsP95_ = 0.0f;

    FuvrTransport* transport_ = nullptr;
    std::unique_ptr<IOSurfaceXpcService> xpcService_;
    std::atomic<bool> running_{false};
    std::thread metricsThread_;
    std::thread clockSyncThread_;
    ClockSync   clockSync_;
};

} // namespace fuvr::daemon
