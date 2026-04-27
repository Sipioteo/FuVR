// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <functional>

#include "fuvr/encoder.hpp"
#include "fuvr/metrics.hpp"

#include <CoreVideo/CoreVideo.h>

struct fuvr_vdisplay_handle;
struct FuvrTransport;

namespace fuvr::daemon {

// Per-frame encoder summary, fired once on the encoder's last fragment.
struct EncodeStatsEvent {
    uint64_t frameId;
    uint64_t encodeDurationNs;
    uint32_t encodedSizeBytes;
    bool     wasKeyframe;
};

using EncodeStatsSink = std::function<void(const EncodeStatsEvent&)>;

struct SessionConfig {
    uint32_t perEyeWidth = 0;
    uint32_t perEyeHeight = 0;
    uint32_t refreshRateHz = 90;
    fuvr::VideoCodec codec = fuvr::VideoCodec::Hevc;
    uint32_t bitrateBps = 30'000'000;
    uint32_t forceIdrEveryFrames = 240;
    bool enableVirtualDisplay = false;
    bool audioEnabled = false;
};

class Session {
public:
    Session(uint64_t id, const SessionConfig& cfg, FuvrTransport* transport,
            EncodeStatsSink statsSink = {});
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    [[nodiscard]] uint64_t id() const { return id_; }
    [[nodiscard]] uint32_t virtualDisplayId() const { return virtualDisplayId_; }
    MetricsAggregator& metrics() { return metrics_; }

    // Submit a CVPixelBufferRef. Takes ownership (CFRetain done by caller is
    // optional; this function CFRetains internally before returning to caller).
    bool submitFrame(CVPixelBufferRef pb,
                     uint64_t frameId,
                     uint64_t renderStartNs,
                     bool forceIdr,
                     const float renderedLeft[7],
                     const float renderedRight[7]);

    // Test hook: feed synthetic encoded fragments straight into the FrameSink
    // path so tests can exercise EncodeStats fan-out and Metrics aggregation
    // without spinning up VideoToolbox.
    void testInjectFragment(const fuvr::EncodedFragment& f);

private:
    class FragmentSink;

    uint64_t id_;
    SessionConfig cfg_;
    FuvrTransport* transport_;
    std::unique_ptr<FragmentSink> sink_;
    std::unique_ptr<fuvr::Encoder> encoder_;
    fuvr_vdisplay_handle* vdisplay_ = nullptr;
    uint32_t virtualDisplayId_ = 0;
    MetricsAggregator metrics_;
    std::atomic<uint64_t> lastEncodeStartNs_{0};
    EncodeStatsSink statsSink_;

    // Per-frame accumulators (last fragment fires EncodeStatsEvent).
    uint64_t curFrameId_       = 0;
    uint32_t curFrameBytes_    = 0;
    bool     curFrameKeyframe_ = false;
    bool     curFrameActive_   = false;
};

} // namespace fuvr::daemon
