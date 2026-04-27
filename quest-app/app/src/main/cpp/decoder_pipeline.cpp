// SPDX-License-Identifier: Apache-2.0

#include "decoder_pipeline.hpp"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.dec", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.dec", __VA_ARGS__)

namespace fuvr {

namespace {
constexpr const char* kHevcMime = "video/hevc";
constexpr const char* kH264Mime = "video/avc";
constexpr int32_t kPerEyeWidth = 2064;
constexpr int32_t kPerEyeHeight = 2208;
}

bool DecoderPipeline::start(Codec codec) {
    const char* mime = (codec == Codec::Hevc) ? kHevcMime : kH264Mime;
    codec_ = AMediaCodec_createDecoderByType(mime);
    if (!codec_) {
        LOGE("AMediaCodec_createDecoderByType(%s) failed", mime);
        return false;
    }

    format_ = AMediaFormat_new();
    AMediaFormat_setString(format_, AMEDIAFORMAT_KEY_MIME, mime);
    // Side-by-side stereo: width = per-eye * 2.
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_WIDTH, kPerEyeWidth * 2);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_HEIGHT, kPerEyeHeight);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_LOW_LATENCY, 1);
    // OUTPUT_FORMAT 0x7F420888 = COLOR_FormatYUV420Flexible; with surface
    // output (which we want, so we get AHardwareBuffer-backed images) we
    // must configure with an output surface before start(). The surface is
    // created by the compositor and wired in once OpenXR swapchains exist.
    // For the skeleton we configure without a surface; runtime path is TODO.

    if (AMediaCodec_configure(codec_, format_, nullptr, nullptr, 0) != AMEDIA_OK) {
        LOGE("AMediaCodec_configure failed");
        return false;
    }
    if (AMediaCodec_start(codec_) != AMEDIA_OK) {
        LOGE("AMediaCodec_start failed");
        return false;
    }

    running_ = true;
    output_thread_ = std::thread(&DecoderPipeline::output_loop, this);
    return true;
}

void DecoderPipeline::stop() {
    running_ = false;
    if (output_thread_.joinable()) output_thread_.join();
    if (codec_) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    if (format_) {
        AMediaFormat_delete(format_);
        format_ = nullptr;
    }
}

void DecoderPipeline::push_encoded(const uint8_t* data, size_t size, uint64_t pts_ns, bool is_key) {
    if (!codec_) return;
    ssize_t idx = AMediaCodec_dequeueInputBuffer(codec_, 0);
    if (idx < 0) return;
    size_t cap = 0;
    uint8_t* dst = AMediaCodec_getInputBuffer(codec_, idx, &cap);
    if (!dst || cap < size) return;
    std::memcpy(dst, data, size);
    uint32_t flags = is_key ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME : 0;
    AMediaCodec_queueInputBuffer(codec_, idx, 0, size, pts_ns / 1000, flags);
}

void DecoderPipeline::output_loop() {
    AMediaCodecBufferInfo info;
    while (running_.load()) {
        ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, 10000);
        if (idx < 0) continue;

        // TODO: convert codec output to AHardwareBuffer. With a SurfaceTexture
        // backed by AImageReader we obtain AHardwareBuffer-backed AImages
        // directly; the compositor consumes those via EGLImageKHR.
        DecodedFrame f;
        f.presentation_time_ns = (uint64_t)info.presentationTimeUs * 1000;

        {
            std::lock_guard<std::mutex> lk(frame_mutex_);
            // drop-old: replace any previous unread frame (release its buffer first).
            if (latest_.buffer) AHardwareBuffer_release(latest_.buffer);
            latest_ = f;
        }
        AMediaCodec_releaseOutputBuffer(codec_, idx, false);
    }
}

DecodedFrame DecoderPipeline::pop_latest() {
    std::lock_guard<std::mutex> lk(frame_mutex_);
    DecodedFrame out = latest_;
    latest_ = {};
    return out;
}

}
