// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <CoreVideo/CoreVideo.h>

#include "fuvr/clock_sync.hpp"
#include "fuvr/input_router.hpp"
#include "fuvr/iosurface_xpc_service.hpp"
#include "fuvr/metrics.hpp"
#include "fuvr/openvr_listener.hpp"
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

    // OpenVR-shim entry points. Used by OpenVrListener to spin up an
    // encoder-backed Session on Hello and to resolve per-frame surface tokens
    // into CVPixelBuffers on SubmitFrame. Kept on Daemon (rather than the
    // listener) because XPC service ownership lives here and Session creation
    // must latch the Quest transport that this class already owns.
    //
    // Returns the session id, or 0 on failure.
    uint64_t openOpenVrSession(uint32_t perEyeWidth,
                               uint32_t perEyeHeight,
                               uint32_t refreshRateHz,
                               const std::string& appKey);
    void     closeOpenVrSession(uint64_t sessionId);
    // Resolve `surfaceToken` to a retained CVPixelBuffer; caller releases.
    CVPixelBufferRef resolveSurfaceToken(uint64_t surfaceToken);
    // Submit a fully-built CVPixelBuffer through the named session. Returns
    // false if the session has been torn down meanwhile.
    bool submitOpenVrFrame(uint64_t sessionId,
                           CVPixelBufferRef pb,
                           uint64_t frameId,
                           uint64_t renderStartNs,
                           const float renderedLeftPose[7],
                           const float renderedRightPose[7],
                           const float leftFov[4],
                           const float rightFov[4]);

    // Snapshot of the currently-active OpenVR stream for the rpc.sock query.
    struct ActiveStreamSnapshot {
        bool        connected{false};
        uint32_t    perEyeWidth{0};
        uint32_t    perEyeHeight{0};
        uint32_t    refreshRateHz{0};
        float       currentFps{0.0f};
        uint64_t    framesSubmitted{0};
        std::string appKey;
    };
    ActiveStreamSnapshot activeStreamSnapshot() const;
    void noteOpenVrFrameSubmitted();   // called by listener to drive fps / count
    void setActiveStreamMeta(const std::string& appKey,
                             uint32_t perEyeWidth,
                             uint32_t perEyeHeight,
                             uint32_t refreshRateHz);
    void clearActiveStream();

private:
    void onEnvelope(const InboundRpc& rpc);
    void metricsLoop();
    void clockSyncLoop();
    void dispatchEncodeStats(const EncodeStatsEvent& ev);
    void handleControlMessage(const uint8_t* data, std::size_t len);
    static void onTransportRecv(void* user, uint8_t channel,
                                const uint8_t* data, std::size_t len);

    RpcServer rpc_;
    OpenVrListener openvrListener_;
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

    // Latest device capabilities reported by the Quest's `helloFromQuest` on
    // the control channel. Initially `valid = false` until a Quest connects.
    // Runtime queries this via the `getDeviceCapabilities` RPC so it can stop
    // hardcoding Quest 3 values and adapt to the actual headset (Quest 2/3/3S/
    // Pro/future). Mutex-protected because helloFromQuest arrives on the
    // transport thread and getDeviceCapabilities is served on the RPC thread.
    struct CachedCapabilities {
        bool valid = false;
        std::string deviceModel;
        std::string systemVersion;
        uint32_t perEyeWidth = 0;
        uint32_t perEyeHeight = 0;
        std::vector<uint32_t> refreshRatesHz;
        bool hasHandTracking = false;
        bool hasEyeTracking = false;
    };
    std::mutex capsMu_;
    CachedCapabilities caps_;

    // OpenVR active-stream telemetry. Single-stream only for now; later this
    // becomes a vector keyed by sessionId. Mutex guards everything inside.
    mutable std::mutex activeStreamMu_;
    struct ActiveStreamState {
        bool        connected{false};
        uint64_t    sessionId{0};
        std::string appKey;
        uint32_t    perEyeWidth{0};
        uint32_t    perEyeHeight{0};
        uint32_t    refreshRateHz{0};
        uint64_t    framesSubmitted{0};
        // Sliding 1-second timestamp ring (ns since steady_clock epoch).
        // Capped at 256 entries — at 90 Hz that holds ~2.8 s worth which
        // is more than enough for a 1 s window even with brief stalls.
        std::vector<uint64_t> recentSubmitNs;
    };
    ActiveStreamState activeStream_;

    FuvrTransport* transport_ = nullptr;
    std::unique_ptr<IOSurfaceXpcService> xpcService_;
    std::atomic<bool> running_{false};
    std::thread metricsThread_;
    std::thread clockSyncThread_;
    ClockSync   clockSync_;
};

} // namespace fuvr::daemon
