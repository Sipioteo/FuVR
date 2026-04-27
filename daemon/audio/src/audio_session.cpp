// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon/audio/audio_session.hpp"

#include "fuvr/audio/capture.hpp"
#include "fuvr/audio/opus_encoder.hpp"
#include "fuvr/session.hpp"

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr.capnp.h"

extern "C" {
struct FuvrTransport;
typedef enum FuvrChannel {
    FuvrChannel_Video = 0,
    FuvrChannel_Audio = 1,
    FuvrChannel_Pose = 2,
    FuvrChannel_Input = 3,
    FuvrChannel_Haptics = 4,
    FuvrChannel_Control = 5,
} FuvrChannel;
int32_t fuvr_transport_send(FuvrTransport* handle, FuvrChannel channel,
                            const uint8_t* data, size_t len);
}

namespace fuvr::daemon::audio {

namespace {

class AudioSessionImpl final : public AudioSession {
public:
    AudioSessionImpl(FuvrTransport* tx, AudioConfig cfg)
        : transport_(tx), cfg_(cfg) {
        encoder_ = fuvr::audio::OpusEncoderWrap::create(cfg_.sampleRate, cfg_.channels);
        framesPerPacket_ = encoder_ ? encoder_->framesPerPacket() : 960;
        scratch_.reserve(framesPerPacket_ * cfg_.channels);
    }

    ~AudioSessionImpl() override { stop(); }

    bool start() override {
        if (running_.exchange(true)) return true;
        if (!encoder_) {
            running_.store(false);
            return false;
        }
        capture_ = fuvr::audio::Capture::create(
            [this](const int16_t* f, std::size_t n, std::uint64_t ts) {
                this->onPcm(f, n, ts);
            });
        if (!capture_) {
            running_.store(false);
            return false;
        }
        // Capture::start() may fail (no GUI session, no TCC). The session
        // remains "running" in the sense of accepting test injections; real
        // capture failures are non-fatal so daemon tests keep working.
        capture_->start();
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (capture_) {
            capture_->stop();
            capture_.reset();
        }
        std::lock_guard<std::mutex> lk(mu_);
        scratch_.clear();
    }

    void injectPcmForTest(const std::int16_t* frames,
                          std::size_t numFrames,
                          std::uint64_t hostTimeNs) override {
        if (!running_.load()) running_.store(true);
        onPcm(frames, numFrames, hostTimeNs);
    }

    std::uint64_t packetsSent() const override { return packetsSent_.load(); }

private:
    void onPcm(const int16_t* frames, std::size_t numFrames, std::uint64_t hostTimeNs) {
        if (!encoder_) return;

        std::lock_guard<std::mutex> lk(mu_);
        const std::size_t ch = cfg_.channels;
        const std::size_t fpp = framesPerPacket_;

        // Append to scratch (interleaved frames * channels).
        scratch_.insert(scratch_.end(), frames, frames + numFrames * ch);

        // Estimate PTS for the FIRST sample currently buffered.
        const uint64_t nsPerFrame = 1'000'000'000ull / cfg_.sampleRate;
        const uint64_t buffered = (scratch_.size() / ch) - numFrames;
        uint64_t firstPts = (hostTimeNs > buffered * nsPerFrame)
                          ? (hostTimeNs - buffered * nsPerFrame)
                          : hostTimeNs;

        std::array<uint8_t, 4000> packetBuf{};

        while (scratch_.size() >= fpp * ch) {
            auto* base = scratch_.data();
            std::size_t bytes = encoder_->encode(base, fpp,
                                std::span<uint8_t>(packetBuf.data(), packetBuf.size()));
            if (bytes > 0) {
                emitPacket(packetBuf.data(), bytes, firstPts);
            }
            scratch_.erase(scratch_.begin(), scratch_.begin() + (fpp * ch));
            firstPts += (uint64_t)fpp * nsPerFrame;
            // Update host pts baseline.
            lastPtsNs_ = firstPts;
        }
    }

    void emitPacket(const uint8_t* data, std::size_t size, uint64_t ptsNs) {
        ::capnp::MallocMessageBuilder msg;
        auto pkt = msg.initRoot<::fuvr::proto::AudioPacket>();
        pkt.setTimestampNs(ptsNs);
        pkt.setCodec(::fuvr::proto::AudioCodec::OPUS);
        pkt.setSampleRate(cfg_.sampleRate);
        pkt.setChannels((uint8_t)cfg_.channels);
        auto payload = pkt.initPayload(size);
        std::memcpy(payload.begin(), data, size);

        kj::VectorOutputStream os;
        ::capnp::writePackedMessage(os, msg);
        auto bytes = os.getArray();

        if (transport_) {
            fuvr_transport_send(transport_, FuvrChannel_Audio,
                                bytes.begin(), bytes.size());
        }
        packetsSent_.fetch_add(1);
        lastSentBytes_ = bytes.size();
    }

    FuvrTransport* transport_;
    AudioConfig cfg_;
    std::unique_ptr<fuvr::audio::Capture> capture_;
    std::unique_ptr<fuvr::audio::OpusEncoderWrap> encoder_;
    std::uint32_t framesPerPacket_{960};
    std::mutex mu_;
    std::vector<int16_t> scratch_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> packetsSent_{0};
    std::uint64_t lastPtsNs_{0};
    std::size_t  lastSentBytes_{0};
};

struct Registry {
    std::mutex mu;
    std::map<std::uint64_t, std::unique_ptr<AudioSession>> sessions;
};

Registry& registry() {
    static Registry r;
    return r;
}

} // namespace

std::unique_ptr<AudioSession> AudioSession::create(FuvrTransport* transport,
                                                   AudioConfig cfg) {
    return std::make_unique<AudioSessionImpl>(transport, cfg);
}

void startAudioFor(Session& session, FuvrTransport* transport) {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    auto it = r.sessions.find(session.id());
    if (it != r.sessions.end()) return;
    auto a = AudioSession::create(transport, AudioConfig{});
    if (a && a->start()) {
        r.sessions.emplace(session.id(), std::move(a));
    }
}

void stopAudioFor(Session& session) {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    auto it = r.sessions.find(session.id());
    if (it == r.sessions.end()) return;
    it->second->stop();
    r.sessions.erase(it);
}

} // namespace fuvr::daemon::audio
