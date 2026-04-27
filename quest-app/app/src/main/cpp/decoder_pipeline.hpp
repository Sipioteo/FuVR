// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <android/hardware_buffer.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "proto_codec.hpp"

struct AMediaCodec;
struct AMediaFormat;
struct AImageReader;
struct ANativeWindow;

namespace fuvr {

struct DecodedFrame {
    AHardwareBuffer* buffer{nullptr};
    uint64_t frame_id{0};
    uint64_t presentation_time_ns{0};
    // Pose used to render this frame on the Mac (per-eye, ViewState carries
    // pose+fov). Compositor reprojects against xrLocateViews-now to apply
    // rotational ATW. Defaulted to identity for frames pushed before the
    // pose plumbing is complete.
    PlainViewState rendered_left{};
    PlainViewState rendered_right{};
};

struct DecoderMetrics {
    float fps{0.0f};
    float decode_ms_p95{0.0f};
    float decode_ms_avg{0.0f};
    uint64_t frames_delivered{0};
    uint64_t dropped_frames{0};
};

class DecoderPipeline {
public:
    enum class Codec { Hevc, H264 };

    ~DecoderPipeline() { stop(); }

    bool start(Codec codec);
    void stop();

    // Renegotiate output dimensions before start() once SessionConfig arrives.
    void set_output_size(int32_t width, int32_t height);

    // Push a NAL/access-unit fragment received from transport.
    void push_encoded(const uint8_t* data, size_t size, uint64_t pts_ns, bool is_key,
                      const PlainViewState& rendered_left = {},
                      const PlainViewState& rendered_right = {});

    // Drop-old policy: take the freshest decoded AHardwareBuffer.
    // The returned `buffer` carries one ref the caller must release with
    // AHardwareBuffer_release after binding into GL.
    DecodedFrame pop_latest();

    DecoderMetrics snapshot_metrics();

    // Bumped each time a previously-buffered AHardwareBuffer is replaced
    // before the compositor consumed it (drop-old policy).
    void note_dropped_frame() { ++dropped_frames_; }

private:
    static void on_image_available_thunk(void* ctx, AImageReader* reader);
    void on_image_available(AImageReader* reader);

    AMediaCodec* codec_{nullptr};
    AMediaFormat* format_{nullptr};
    AImageReader* reader_{nullptr};
    ANativeWindow* surface_{nullptr};
    std::atomic<bool> running_{false};

    int32_t width_{4128};
    int32_t height_{2208};

    std::mutex frame_mutex_;
    DecodedFrame latest_{};

    struct InflightPts {
        uint64_t pts_us;
        uint64_t enqueue_ns;
        PlainViewState rendered_left;
        PlainViewState rendered_right;
    };

    std::mutex pts_mutex_;
    std::deque<InflightPts> queued_;

    std::mutex metrics_mutex_;
    std::deque<uint64_t> arrival_intervals_ns_;
    std::deque<uint64_t> decode_latency_ns_;
    uint64_t last_arrival_ns_{0};
    uint64_t total_frames_{0};
    std::atomic<uint64_t> dropped_frames_{0};
};

}
