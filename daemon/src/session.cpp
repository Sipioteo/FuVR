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

        std::vector<uint8_t> wire;
        wire.reserve(hdrBytes.size() + 4 + f.size);
        uint32_t hdrLen = static_cast<uint32_t>(hdrBytes.size());
        wire.insert(wire.end(), reinterpret_cast<uint8_t*>(&hdrLen),
                    reinterpret_cast<uint8_t*>(&hdrLen) + sizeof(hdrLen));
        wire.insert(wire.end(), hdrBytes.begin(), hdrBytes.end());
        wire.insert(wire.end(), f.data, f.data + f.size);

        if (transport_) {
            fuvr_transport_send(transport_, FuvrChannel_Video, wire.data(), wire.size());
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
}

Session::~Session() {
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
                          const float[7],
                          const float[7]) {
    if (!encoder_ || !pb) return false;
    lastEncodeStartNs_.store(nowMonoNs());
    return encoder_->submit(pb, frameId, renderStartNs, forceIdr);
}

} // namespace fuvr::daemon
