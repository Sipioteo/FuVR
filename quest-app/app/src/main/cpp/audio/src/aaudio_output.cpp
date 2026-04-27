// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/aaudio_output.hpp"

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#endif

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace fuvr::audio {

namespace {

class RingBuffer {
public:
    explicit RingBuffer(std::size_t samples) : buf_(samples, 0) {}

    void write(const std::int16_t* in, std::size_t n) {
        std::lock_guard<std::mutex> lk(mu_);
        for (std::size_t i = 0; i < n; ++i) {
            buf_[wr_] = in[i];
            wr_ = (wr_ + 1) % buf_.size();
            if (wr_ == rd_) {
                // Overwrite oldest sample.
                rd_ = (rd_ + 1) % buf_.size();
            }
        }
        size_ = (wr_ >= rd_) ? (wr_ - rd_) : (buf_.size() - (rd_ - wr_));
    }

    std::size_t read(std::int16_t* out, std::size_t n) {
        std::lock_guard<std::mutex> lk(mu_);
        std::size_t avail = (wr_ >= rd_) ? (wr_ - rd_)
                                          : (buf_.size() - (rd_ - wr_));
        std::size_t take = (n < avail) ? n : avail;
        for (std::size_t i = 0; i < take; ++i) {
            out[i] = buf_[rd_];
            rd_ = (rd_ + 1) % buf_.size();
        }
        if (take < n) std::memset(out + take, 0, (n - take) * sizeof(std::int16_t));
        size_ -= take;
        return take;
    }

private:
    std::vector<std::int16_t> buf_;
    std::size_t rd_{0};
    std::size_t wr_{0};
    std::size_t size_{0};
    std::mutex mu_;
};

#if defined(__ANDROID__)

class AAudioOutputImpl final : public AAudioOutput {
public:
    AAudioOutputImpl(std::uint32_t sr, std::uint32_t ch)
        : sr_(sr), ch_(ch),
          ring_(std::size_t(sr) * ch * 40 / 1000) {}

    ~AAudioOutputImpl() override { stop(); }

    bool start() override {
        if (running_.exchange(true)) return true;
        AAudioStreamBuilder* b = nullptr;
        if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) {
            running_.store(false);
            return false;
        }
        AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setSampleRate(b, (int32_t)sr_);
        AAudioStreamBuilder_setChannelCount(b, (int32_t)ch_);
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setDataCallback(b, &AAudioOutputImpl::dataCb, this);

        AAudioStream* s = nullptr;
        if (AAudioStreamBuilder_openStream(b, &s) != AAUDIO_OK) {
            AAudioStreamBuilder_delete(b);
            running_.store(false);
            return false;
        }
        AAudioStreamBuilder_delete(b);
        if (AAudioStream_requestStart(s) != AAUDIO_OK) {
            AAudioStream_close(s);
            running_.store(false);
            return false;
        }
        stream_ = s;
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (stream_) {
            AAudioStream_requestStop(stream_);
            AAudioStream_close(stream_);
            stream_ = nullptr;
        }
    }

    void onPcm(const std::int16_t* frames, std::size_t numFrames,
               std::uint32_t channels, std::uint32_t sampleRate,
               std::uint64_t /*ts*/) override {
        if (channels != ch_ || sampleRate != sr_) return;
        ring_.write(frames, numFrames * channels);
    }

private:
    static aaudio_data_callback_result_t dataCb(AAudioStream* /*s*/,
                                                void* userData,
                                                void* audioData,
                                                int32_t numFrames) {
        auto* self = static_cast<AAudioOutputImpl*>(userData);
        self->ring_.read(static_cast<std::int16_t*>(audioData),
                         (std::size_t)numFrames * self->ch_);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    std::uint32_t sr_;
    std::uint32_t ch_;
    RingBuffer ring_;
    AAudioStream* stream_{nullptr};
    std::atomic<bool> running_{false};
};

#else // host build

class AAudioOutputImpl final : public AAudioOutput {
public:
    AAudioOutputImpl(std::uint32_t sr, std::uint32_t ch)
        : sr_(sr), ch_(ch), ring_(std::size_t(sr) * ch * 40 / 1000) {}
    bool start() override { return true; }
    void stop() override {}
    void onPcm(const std::int16_t* frames, std::size_t numFrames,
               std::uint32_t channels, std::uint32_t sampleRate,
               std::uint64_t /*ts*/) override {
        if (channels != ch_ || sampleRate != sr_) return;
        ring_.write(frames, numFrames * channels);
    }
private:
    std::uint32_t sr_;
    std::uint32_t ch_;
    RingBuffer ring_;
};

#endif

} // namespace

std::unique_ptr<AAudioOutput> AAudioOutput::create(std::uint32_t sr,
                                                   std::uint32_t ch) {
    return std::make_unique<AAudioOutputImpl>(sr, ch);
}

} // namespace fuvr::audio
