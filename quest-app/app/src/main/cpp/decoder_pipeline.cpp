// SPDX-License-Identifier: Apache-2.0

#include "decoder_pipeline.hpp"

#include <android/log.h>
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.dec", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.dec", __VA_ARGS__)

namespace fuvr {

namespace {
constexpr const char* kHevcMime = "video/hevc";
constexpr const char* kH264Mime = "video/avc";
constexpr int32_t kMaxImages = 4;
constexpr size_t kMetricsWindow = 256;

uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifndef AHARDWAREBUFFER_USAGE_VIDEO_DECODE_OUTPUT_BUFFER
// Why: this usage flag was added in NDK r24+; older NDKs lack the symbol but
// the kernel still understands the bit. Defining it here keeps the build
// portable across the minimum-supported NDK versions (we target r26+ but
// some CI images are older).
#define AHARDWAREBUFFER_USAGE_VIDEO_DECODE_OUTPUT_BUFFER 0x00100000ULL
#endif
}

void DecoderPipeline::set_output_size(int32_t width, int32_t height) {
    width_ = width;
    height_ = height;
}

bool DecoderPipeline::start(Codec codec) {
    const char* mime = (codec == Codec::Hevc) ? kHevcMime : kH264Mime;

    // Why: AIMAGE_FORMAT_PRIVATE yields opaque GPU-only AHardwareBuffers we
    // bind via EGLImage as samplerExternalOES; CPU-readable formats would
    // force a YUV conversion blit that the Quest GPU does not need.
    const uint64_t usage =
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
        AHARDWAREBUFFER_USAGE_VIDEO_DECODE_OUTPUT_BUFFER;

    media_status_t st = AImageReader_newWithUsage(width_, height_,
                                                  AIMAGE_FORMAT_PRIVATE,
                                                  usage, kMaxImages, &reader_);
    if (st != AMEDIA_OK || !reader_) {
        LOGE("AImageReader_newWithUsage(%dx%d) failed: %d", width_, height_, st);
        return false;
    }

    AImageReader_ImageListener listener{ this, &DecoderPipeline::on_image_available_thunk };
    AImageReader_setImageListener(reader_, &listener);

    if (AImageReader_getWindow(reader_, &surface_) != AMEDIA_OK || !surface_) {
        LOGE("AImageReader_getWindow failed");
        return false;
    }

    codec_ = AMediaCodec_createDecoderByType(mime);
    if (!codec_) {
        LOGE("AMediaCodec_createDecoderByType(%s) failed", mime);
        return false;
    }

    format_ = AMediaFormat_new();
    AMediaFormat_setString(format_, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_WIDTH, width_);
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_HEIGHT, height_);
#if __ANDROID_API__ >= 30
    AMediaFormat_setInt32(format_, AMEDIAFORMAT_KEY_LOW_LATENCY, 1);
#endif

    // Why: passing the AImageReader's ANativeWindow as the output surface
    // makes MediaCodec write decoded frames straight into AHardwareBuffer-
    // backed AImages; releaseOutputBuffer(idx, true) then renders to the
    // surface without any CPU-visible copy.
    if (AMediaCodec_configure(codec_, format_, surface_, nullptr, 0) != AMEDIA_OK) {
        LOGE("AMediaCodec_configure failed");
        return false;
    }
    if (AMediaCodec_start(codec_) != AMEDIA_OK) {
        LOGE("AMediaCodec_start failed");
        return false;
    }

    running_ = true;
    LOGI("decoder started %dx%d codec=%s", width_, height_, mime);
    return true;
}

void DecoderPipeline::stop() {
    running_ = false;
    if (codec_) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    if (format_) {
        AMediaFormat_delete(format_);
        format_ = nullptr;
    }
    if (reader_) {
        AImageReader_setImageListener(reader_, nullptr);
        AImageReader_delete(reader_);
        reader_ = nullptr;
        surface_ = nullptr;
    }
    std::lock_guard<std::mutex> lk(frame_mutex_);
    if (latest_.buffer) {
        AHardwareBuffer_release(latest_.buffer);
        latest_ = {};
    }
}

void DecoderPipeline::push_encoded(const uint8_t* data, size_t size,
                                   uint64_t pts_ns, bool is_key,
                                   const PlainViewState& rendered_left,
                                   const PlainViewState& rendered_right) {
    if (!codec_) return;
    ssize_t idx = AMediaCodec_dequeueInputBuffer(codec_, 0);
    if (idx < 0) return;
    size_t cap = 0;
    uint8_t* dst = AMediaCodec_getInputBuffer(codec_, idx, &cap);
    if (!dst || cap < size) return;
    std::memcpy(dst, data, size);
    // Why: NDK MediaCodec does not surface a key-frame input flag — the codec
    // detects keyframes from the bitstream itself. We only need to flag end-of-
    // stream (which we do not yet do).
    (void)is_key;
    uint32_t flags = 0;
    const uint64_t pts_us = pts_ns / 1000ULL;
    AMediaCodec_queueInputBuffer(codec_, idx, 0, size, pts_us, flags);

    {
        std::lock_guard<std::mutex> lk(pts_mutex_);
        if (queued_.size() > 64) queued_.pop_front();
        queued_.push_back(InflightPts{pts_us, now_ns(), rendered_left, rendered_right});
    }

    AMediaCodecBufferInfo info;
    while (true) {
        ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, 0);
        if (out_idx < 0) break;
        // Why: render=true routes the buffer into our AImageReader surface;
        // the AImage arrives via the on_image_available callback.
        AMediaCodec_releaseOutputBuffer(codec_, out_idx, true);
    }
}

void DecoderPipeline::on_image_available_thunk(void* ctx, AImageReader* reader) {
    static_cast<DecoderPipeline*>(ctx)->on_image_available(reader);
}

void DecoderPipeline::on_image_available(AImageReader* reader) {
    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image) return;

    AHardwareBuffer* buf = nullptr;
    if (AImage_getHardwareBuffer(image, &buf) != AMEDIA_OK || !buf) {
        AImage_delete(image);
        return;
    }

    // Why: AImage_getHardwareBuffer does not transfer ownership; we must
    // acquire our own reference so the buffer survives AImage_delete below.
    AHardwareBuffer_acquire(buf);

    int64_t pts_us = 0;
    AImage_getTimestamp(image, &pts_us);
    AImage_delete(image);

    DecodedFrame f;
    f.buffer = buf;
    f.presentation_time_ns = (uint64_t)pts_us;

    // Why: do the pts → rendered-pose lookup *before* publishing latest_ so
    // ATW always sees the matched render pose for the frame it samples.
    const uint64_t arrival_ns = now_ns();
    uint64_t latency_ns = 0;
    {
        std::lock_guard<std::mutex> lk(pts_mutex_);
        for (auto it = queued_.begin(); it != queued_.end(); ++it) {
            if (it->pts_us == (uint64_t)pts_us) {
                latency_ns = arrival_ns - it->enqueue_ns;
                f.rendered_left = it->rendered_left;
                f.rendered_right = it->rendered_right;
                queued_.erase(queued_.begin(), it + 1);
                break;
            }
        }
    }

    AHardwareBuffer* prev = nullptr;
    {
        std::lock_guard<std::mutex> lk(frame_mutex_);
        prev = latest_.buffer;
        latest_ = f;
    }
    // Why: drop-old replacement releases the previously-buffered frame's
    // ref that pop_latest never picked up; failing this leaks AHardwareBuffer
    // pages until the AImageReader hits its max-images limit and stalls.
    if (prev) {
        AHardwareBuffer_release(prev);
        ++dropped_frames_;
    }

    std::lock_guard<std::mutex> lk(metrics_mutex_);
    if (last_arrival_ns_ != 0) {
        arrival_intervals_ns_.push_back(arrival_ns - last_arrival_ns_);
        if (arrival_intervals_ns_.size() > kMetricsWindow) arrival_intervals_ns_.pop_front();
    }
    last_arrival_ns_ = arrival_ns;
    if (latency_ns > 0) {
        decode_latency_ns_.push_back(latency_ns);
        if (decode_latency_ns_.size() > kMetricsWindow) decode_latency_ns_.pop_front();
    }
    ++total_frames_;
}

DecodedFrame DecoderPipeline::pop_latest() {
    std::lock_guard<std::mutex> lk(frame_mutex_);
    DecodedFrame out = latest_;
    latest_ = {};
    return out;
}

DecoderMetrics DecoderPipeline::snapshot_metrics() {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    DecoderMetrics m;
    m.frames_delivered = total_frames_;
    m.dropped_frames = dropped_frames_.load();
    if (!arrival_intervals_ns_.empty()) {
        uint64_t sum = 0;
        for (uint64_t v : arrival_intervals_ns_) sum += v;
        const double mean_ns = (double)sum / (double)arrival_intervals_ns_.size();
        if (mean_ns > 0.0) m.fps = (float)(1e9 / mean_ns);
    }
    if (!decode_latency_ns_.empty()) {
        std::vector<uint64_t> sorted(decode_latency_ns_.begin(), decode_latency_ns_.end());
        std::sort(sorted.begin(), sorted.end());
        const size_t idx = (size_t)((double)(sorted.size() - 1) * 0.95);
        m.decode_ms_p95 = (float)((double)sorted[idx] / 1.0e6);
        uint64_t sum = 0;
        for (uint64_t v : decode_latency_ns_) sum += v;
        m.decode_ms_avg = (float)((double)sum / (double)decode_latency_ns_.size() / 1.0e6);
    }
    return m;
}

}
