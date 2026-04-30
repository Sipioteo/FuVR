// SPDX-License-Identifier: Apache-2.0
#include "fuvr/session.hpp"

#include <chrono>
#include <cstring>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr.capnp.h"
#include "fuvr_transport.h"
#include "fuvr_vdisplay_control.h"
#include "fuvr/daemon/audio/audio_session.hpp"
#include "fuvr/logger.hpp"

namespace fuvr::daemon {

namespace {
uint64_t nowMonoNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

class Session::FragmentSink final : public fuvr::FrameSink {
public:
    FragmentSink(Session* owner, FuvrTransport* transport)
        : owner_(owner), transport_(transport) {}

    void onFragment(const fuvr::EncodedFragment& f) override {
        ::capnp::MallocMessageBuilder hdrMsg;
        auto hdr = hdrMsg.initRoot<::fuvr::proto::VideoFragmentHeader>();
        hdr.setFrameId(f.frameId);
        hdr.setRenderStartNs(f.renderStartNs);
        hdr.setTotalSizeBytes(static_cast<uint32_t>(f.size));
        hdr.setFragmentIndex(0);
        hdr.setFragmentCount(1);

        // Why: the Quest needs the exact pose used to render this frame to
        // run rotational ATW. We stamp it here; the runtime hands it to the
        // daemon on submitFrame and we squirrel it away in renderedPoses_
        // keyed by frameId. setView() builds a default Pose+Fov when called
        // with all-zero quat (w=1 below); leaving it unset would still ship
        // a default-initialized ViewState, but stamping makes intent obvious.
        Session::RenderedPose rp{};
        {
            std::lock_guard lk(owner_->renderedPosesMu_);
            auto it = owner_->renderedPoses_.find(f.frameId);
            if (it != owner_->renderedPoses_.end()) rp = it->second;
        }
        auto fillView = [](::fuvr::proto::ViewState::Builder vb,
                           const std::array<float, 7>& v,
                           const std::array<float, 4>* fov) {
            auto pose = vb.initPose();
            auto pos = pose.initPosition();
            pos.setX(v[0]); pos.setY(v[1]); pos.setZ(v[2]);
            auto rot = pose.initOrientation();
            rot.setX(v[3]); rot.setY(v[4]); rot.setZ(v[5]); rot.setW(v[6]);
            auto fb = vb.initFov();
            if (fov != nullptr) {
                fb.setAngleLeft((*fov)[0]);
                fb.setAngleRight((*fov)[1]);
                fb.setAngleUp((*fov)[2]);
                fb.setAngleDown((*fov)[3]);
            }
            // If fov is null/unset, fields default to 0 and Quest's ATW shader
            // falls back to assuming fov_render == fov_now (under-corrects but
            // no NaNs). With overscan enabled on the runtime side we MUST ship
            // fov here or the Quest won't see the rendered overscan and will
            // appear to "see the screen edge" during fast head turns.
        };
        const std::array<float, 4>* lFov = rp.fovValid ? &rp.leftFov  : nullptr;
        const std::array<float, 4>* rFov = rp.fovValid ? &rp.rightFov : nullptr;
        if (rp.valid) {
            fillView(hdr.initRenderedLeft(), rp.left, lFov);
            fillView(hdr.initRenderedRight(), rp.right, rFov);
        } else {
            // Identity pose so the wire side never reads garbage.
            std::array<float, 7> id{0,0,0, 0,0,0, 1};
            fillView(hdr.initRenderedLeft(), id, nullptr);
            fillView(hdr.initRenderedRight(), id, nullptr);
        }
        hdr.setCodec(owner_->cfg_.codec == fuvr::VideoCodec::H264
                         ? ::fuvr::proto::VideoCodec::H264
                         : ::fuvr::proto::VideoCodec::HEVC);
        uint16_t flags = 0;
        if (f.isKeyframe)  flags |= 1u << static_cast<uint16_t>(::fuvr::proto::VideoFlag::IDR);
        if (f.endOfFrame)  flags |= 1u << static_cast<uint16_t>(::fuvr::proto::VideoFlag::END_OF_FRAME);
        if (f.isCsd)       flags |= 1u << static_cast<uint16_t>(::fuvr::proto::VideoFlag::CSD_HEADER);
        hdr.setFlags(flags);
        hdr.setTargetDisplayTimeNs(0);

        kj::VectorOutputStream os;
        ::capnp::writePackedMessage(os, hdrMsg);
        auto hdrBytes = os.getArray();

        // Wire format on the Video channel: [packed VideoFragmentHeader][raw NAL bytes].
        // Per quest-app/proto_codec.hpp::decode_video_header, the receiver parses
        // the packed header from offset 0 and the codec payload follows immediately.
        // Earlier code prepended a u32 LE hdrLen which no client knew how to skip,
        // so every video frame was discarded as malformed.
        std::vector<uint8_t> wire;
        wire.reserve(hdrBytes.size() + f.size);
        wire.insert(wire.end(), hdrBytes.begin(), hdrBytes.end());
        wire.insert(wire.end(), f.data, f.data + f.size);

        if (transport_) {
            int rc = fuvr_transport_send(transport_, FuvrChannel_Video, wire.data(), wire.size());
            static thread_local uint64_t s_count = 0;
            if ((s_count % 60) == 0 || rc != 0) {
                FUVR_LOG_INFO("session",
                              "transport send video pkt frameId=%llu seq=%llu size=%zu -> %s",
                              (unsigned long long)f.frameId,
                              (unsigned long long)s_count,
                              wire.size(),
                              rc == 0 ? "ok" : "err");
            }
            ++s_count;
        }

        uint64_t startNs = owner_->lastEncodeStartNs_.load();
        uint64_t durNs = startNs ? (nowMonoNs() - startNs) : 0;
        owner_->metrics_.recordEncode(durNs, static_cast<uint32_t>(f.size));

        if (!owner_->curFrameActive_ || owner_->curFrameId_ != f.frameId) {
            owner_->curFrameId_       = f.frameId;
            owner_->curFrameBytes_    = 0;
            owner_->curFrameKeyframe_ = false;
            owner_->curFrameActive_   = true;
        }
        owner_->curFrameBytes_ += static_cast<uint32_t>(f.size);
        if (f.isKeyframe) owner_->curFrameKeyframe_ = true;

        if (f.endOfFrame) {
            std::lock_guard lk(owner_->renderedPosesMu_);
            owner_->renderedPoses_.erase(f.frameId);
            EncodeStatsEvent ev{
                .frameId          = owner_->curFrameId_,
                .encodeDurationNs = durNs,
                .encodedSizeBytes = owner_->curFrameBytes_,
                .wasKeyframe      = owner_->curFrameKeyframe_,
            };
            owner_->curFrameActive_ = false;
            if (owner_->statsSink_) owner_->statsSink_(ev);
        }
    }

private:
    Session* owner_;
    FuvrTransport* transport_;
};

Session::Session(uint64_t id, const SessionConfig& cfg, FuvrTransport* transport,
                 EncodeStatsSink statsSink)
    : id_(id), cfg_(cfg), transport_(transport), statsSink_(std::move(statsSink)) {
    sink_ = std::make_unique<FragmentSink>(this, transport_);
    fuvr::EncoderConfig ec{
        .width = cfg_.perEyeWidth * 2,
        .height = cfg_.perEyeHeight,
        .targetBitrateBps = cfg_.bitrateBps,
        .framerateHz = cfg_.refreshRateHz,
        .codec = cfg_.codec,
        .realTime = true,
        .maxKeyframeIntervalFrames = cfg_.forceIdrEveryFrames,
    };
    encoder_ = fuvr::Encoder::create(ec, sink_.get());

    if (cfg_.enableVirtualDisplay) {
        vdisplay_ = fuvr_vdisplay_spawn(cfg_.perEyeWidth * 2, cfg_.perEyeHeight,
                                        cfg_.refreshRateHz);
        if (vdisplay_) virtualDisplayId_ = fuvr_vdisplay_id(vdisplay_);
    }

    if (cfg_.audioEnabled) {
        fuvr::daemon::audio::startAudioFor(*this, transport);
    }
}

Session::~Session() {
    fuvr::daemon::audio::stopAudioFor(*this);
    if (encoder_) encoder_->flush();
    encoder_.reset();
    sink_.reset();
    if (vdisplay_) fuvr_vdisplay_kill(vdisplay_);
}

void Session::testInjectFragment(const fuvr::EncodedFragment& f) {
    if (sink_) sink_->onFragment(f);
}

bool Session::submitFrame(CVPixelBufferRef pb,
                          uint64_t frameId,
                          uint64_t renderStartNs,
                          bool forceIdr,
                          const float renderedLeft[7],
                          const float renderedRight[7],
                          const float renderedLeftFov[4],
                          const float renderedRightFov[4]) {
    if (!encoder_ || !pb) return false;
    lastEncodeStartNs_.store(nowMonoNs());

    // Stash the rendered pose so FragmentSink::onFragment can stamp it on
    // every wire header for this frame. Bound the map at 64 entries; under
    // normal flow we erase on endOfFrame, but a frame the encoder swallows
    // (e.g. dropped under back-pressure) must not leak its stash.
    {
        RenderedPose rp{};
        rp.valid = (renderedLeft != nullptr && renderedRight != nullptr);
        if (rp.valid) {
            for (int i = 0; i < 7; ++i) {
                rp.left[i]  = renderedLeft[i];
                rp.right[i] = renderedRight[i];
            }
        }
        if (renderedLeftFov != nullptr && renderedRightFov != nullptr &&
            (renderedLeftFov[0] != 0.0f || renderedLeftFov[1] != 0.0f)) {
            rp.fovValid = true;
            for (int i = 0; i < 4; ++i) {
                rp.leftFov[i]  = renderedLeftFov[i];
                rp.rightFov[i] = renderedRightFov[i];
            }
        }
        std::lock_guard lk(renderedPosesMu_);
        if (renderedPoses_.size() > 64) renderedPoses_.clear();
        renderedPoses_[frameId] = rp;
    }

    {
      static thread_local uint64_t s_count = 0;
      bool log = (s_count % 60) == 0;
      bool ok = encoder_->submit(pb, frameId, renderStartNs, forceIdr);
      if (log || !ok) {
        FUVR_LOG_INFO("session",
                      "encoder submit frameId=%llu seq=%llu -> %s",
                      (unsigned long long)frameId,
                      (unsigned long long)s_count,
                      ok ? "ok" : "err");
      }
      ++s_count;
      return ok;
    }
}

} // namespace fuvr::daemon
