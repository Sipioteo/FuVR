// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <android/hardware_buffer.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct AMediaCodec;
struct AMediaFormat;

namespace fuvr {

struct DecodedFrame {
    AHardwareBuffer* buffer{nullptr};
    uint64_t frame_id{0};
    uint64_t presentation_time_ns{0};
};

class DecoderPipeline {
public:
    enum class Codec { Hevc, H264 };

    ~DecoderPipeline() { stop(); }

    bool start(Codec codec);
    void stop();

    // Push a NAL/access-unit fragment received from transport.
    void push_encoded(const uint8_t* data, size_t size, uint64_t pts_ns, bool is_key);

    // Drop-old policy: return the freshest decoded frame, or empty if none.
    DecodedFrame pop_latest();

private:
    void output_loop();

    AMediaCodec* codec_{nullptr};
    AMediaFormat* format_{nullptr};
    std::thread output_thread_;
    std::atomic<bool> running_{false};

    std::mutex frame_mutex_;
    DecodedFrame latest_{};
};

}
