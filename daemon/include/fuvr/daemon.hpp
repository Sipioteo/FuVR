// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "fuvr/clock_sync.hpp"
#include "fuvr/iosurface_xpc_service.hpp"
#include "fuvr/metrics.hpp"
#include "fuvr/pose_router.hpp"
#include "fuvr/rpc_server.hpp"
#include "fuvr/session.hpp"

struct FuvrTransport;

namespace fuvr::daemon {

// Top-level fuvrd state.
//
// Threading model:
//   - One accept thread (in RpcServer) + one reader thread per client.
//   - One metrics ticker thread fires at 10 Hz to push Metrics envelopes.
//   - Encoder fragment callbacks run on VideoToolbox's own threads.
//   - Transport recv callback runs on the Rust transport's thread.
// Why std::mutex everywhere: callers come from heterogeneous threads and the
// hot paths are short; finer-grained sync would not pay off at this scope.
class Daemon {
public:
    Daemon();
    ~Daemon();

    bool start(const std::string& socketPath = {});
    void stop();

    // Test-only access.
    PoseRouter& poseRouter() { return poseRouter_; }
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
    MetricsAggregator globalMetrics_;

    std::mutex sessionsMu_;
    std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions_;
    uint64_t nextSessionId_ = 1;

    struct MetricsSubscriber { int fd; uint64_t streamId; };
    std::mutex metricsSubsMu_;
    std::vector<MetricsSubscriber> metricsSubs_;

    FuvrTransport* transport_ = nullptr;
    std::unique_ptr<IOSurfaceXpcService> xpcService_;
    std::atomic<bool> running_{false};
    std::thread metricsThread_;
    std::thread clockSyncThread_;
    ClockSync   clockSync_;
};

} // namespace fuvr::daemon
