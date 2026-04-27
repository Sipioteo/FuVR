// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon.hpp"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr/iosurface_bridge.hpp"
#include "fuvr.capnp.h"
#include "fuvr_transport.h"
#include "fuvrd.capnp.h"

namespace fuvr::daemon {

namespace {
uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void writePacked(::capnp::MessageBuilder& mb, std::vector<uint8_t>& out) {
    kj::VectorOutputStream os;
    ::capnp::writePackedMessage(os, mb);
    auto a = os.getArray();
    out.assign(a.begin(), a.end());
}
}

Daemon::Daemon() = default;
Daemon::~Daemon() { stop(); }

bool Daemon::start(const std::string& path) {
    std::string p = path.empty() ? defaultSocketPath() : path;
    running_.store(true);

#ifndef FUVR_DAEMON_NO_TRANSPORT
    transport_ = fuvr_transport_create(FuvrTransportKind_UsbServer, "");
    if (transport_) {
        fuvr_transport_set_recv_callback(transport_, &Daemon::onTransportRecv, this);
    }
#endif

    if (!rpc_.start(p, [this](const InboundRpc& r) { onEnvelope(r); })) {
        running_.store(false);
        return false;
    }
    metricsThread_ = std::thread([this] { metricsLoop(); });
    return true;
}

void Daemon::stop() {
    if (!running_.exchange(false)) return;
    rpc_.stop();
    if (metricsThread_.joinable()) metricsThread_.join();
    {
        std::lock_guard lk(sessionsMu_);
        sessions_.clear();
    }
    if (transport_) {
#ifndef FUVR_DAEMON_NO_TRANSPORT
        fuvr_transport_destroy(transport_);
#endif
        transport_ = nullptr;
    }
}

void Daemon::onTransportRecv(void* user, uint8_t channel,
                             const uint8_t* data, std::size_t len) {
    auto* d = static_cast<Daemon*>(user);
    if (channel != FuvrChannel_Pose) return;
    uint64_t sid = 0;
    {
        std::lock_guard lk(d->sessionsMu_);
        if (!d->sessions_.empty()) sid = d->sessions_.begin()->first;
    }
    d->poseRouter_.ingestPackedUpstreamFrame(data, len, sid, nowNs());
}

void Daemon::onEnvelope(const InboundRpc& rpc) {
    kj::ArrayInputStream is(kj::arrayPtr(rpc.envelope.data(), rpc.envelope.size()));
    ::capnp::PackedMessageReader reader(is);
    auto env = reader.getRoot<::fuvr::daemon::Envelope>();
    auto body = env.getBody();
    uint64_t seq = env.getSeq();

    auto reply = [&](auto fillFn) {
        ::capnp::MallocMessageBuilder mb;
        auto e = mb.initRoot<::fuvr::daemon::Envelope>();
        e.setSeq(seq);
        fillFn(e);
        std::vector<uint8_t> out;
        writePacked(mb, out);
        rpc_.send(rpc.clientFd, out.data(), out.size());
    };

    switch (body.which()) {
    case ::fuvr::daemon::Envelope::Body::START_SESSION: {
        auto req = body.getStartSession();
        SessionConfig cfg;
        cfg.perEyeWidth = req.getPerEyeWidth();
        cfg.perEyeHeight = req.getPerEyeHeight();
        cfg.refreshRateHz = req.getRefreshRateHz();
        cfg.codec = req.getVideoCodec() == ::fuvr::daemon::VideoCodec::H264
                        ? fuvr::VideoCodec::H264
                        : fuvr::VideoCodec::Hevc;
        cfg.bitrateBps = req.getVideoBitrateBps();
        cfg.forceIdrEveryFrames = req.getForceIdrEveryFrames();
        cfg.enableVirtualDisplay = req.getEnableVirtualDisplay();

        uint64_t id;
        uint32_t vid = 0;
        {
            std::lock_guard lk(sessionsMu_);
            id = nextSessionId_++;
            auto s = std::make_unique<Session>(id, cfg, transport_);
            vid = s->virtualDisplayId();
            sessions_[id] = std::move(s);
        }
        reply([&](auto e) {
            auto ack = e.getBody().initStartSessionAck();
            ack.setSessionId(id);
            ack.setClockOffsetNs(0);
            ack.setOneWayDelayNs(0);
            ack.setVirtualDisplayId(vid);
        });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::STOP_SESSION: {
        auto req = body.getStopSession();
        {
            std::lock_guard lk(sessionsMu_);
            sessions_.erase(req.getSessionId());
        }
        reply([&](auto e) { e.getBody().setOk(); });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::SUBMIT_FRAME: {
        auto req = body.getSubmitFrame();
        Session* s = nullptr;
        {
            std::lock_guard lk(sessionsMu_);
            auto it = sessions_.find(req.getSessionId());
            if (it != sessions_.end()) s = it->second.get();
        }
        if (!s) { reply([&](auto e) { e.getBody().setError("no session"); }); break; }

        uint32_t tok = req.getSurfaceToken();
        if (tok >= rpc.machPorts.size()) {
            reply([&](auto e) { e.getBody().setError("missing mach port"); });
            break;
        }
        CVPixelBufferRef pb = pixelBufferFromMachPort(rpc.machPorts[tok]);
        if (!pb) { reply([&](auto e) { e.getBody().setError("iosurface lookup failed"); }); break; }

        float left[7] = {
            req.getRenderedLeftPosX(), req.getRenderedLeftPosY(), req.getRenderedLeftPosZ(),
            req.getRenderedLeftRotX(), req.getRenderedLeftRotY(),
            req.getRenderedLeftRotZ(), req.getRenderedLeftRotW(),
        };
        float right[7] = {
            req.getRenderedRightPosX(), req.getRenderedRightPosY(), req.getRenderedRightPosZ(),
            req.getRenderedRightRotX(), req.getRenderedRightRotY(),
            req.getRenderedRightRotZ(), req.getRenderedRightRotW(),
        };
        s->submitFrame(pb, req.getFrameId(), req.getRenderStartNs(),
                       req.getForceIdr(), left, right);
        CFRelease(pb);
        reply([&](auto e) { e.getBody().setOk(); });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::STREAM_POSES: {
        auto req = body.getStreamPoses();
        int fd = rpc.clientFd;
        uint64_t sid = req.getSessionId();
        uint64_t streamId = poseRouter_.addSubscriber(sid,
            [this, fd](const uint8_t* d, std::size_t n) { rpc_.send(fd, d, n); });
        reply([&](auto e) {
            e.setStreamId(streamId);
            e.getBody().setOk();
        });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::STREAM_METRICS: {
        std::lock_guard lk(metricsSubsMu_);
        uint64_t sid = static_cast<uint64_t>(metricsSubs_.size()) + 1;
        metricsSubs_.push_back({rpc.clientFd, sid});
        reply([&](auto e) {
            e.setStreamId(sid);
            e.getBody().setOk();
        });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::PING:
        reply([&](auto e) { e.getBody().setPong(); });
        break;
    default:
        reply([&](auto e) { e.getBody().setError("unsupported"); });
        break;
    }
}

void Daemon::metricsLoop() {
    using namespace std::chrono_literals;
    while (running_.load()) {
        std::this_thread::sleep_for(100ms);
        MetricsSnapshot agg{};
        {
            std::lock_guard lk(sessionsMu_);
            for (auto& [_, s] : sessions_) {
                auto sn = s->metrics().snapshot();
                agg.encoderFps = std::max(agg.encoderFps, sn.encoderFps);
                agg.encoderEncodeMsAvg = std::max(agg.encoderEncodeMsAvg, sn.encoderEncodeMsAvg);
                agg.encoderEncodeMsP95 = std::max(agg.encoderEncodeMsP95, sn.encoderEncodeMsP95);
                agg.videoBitrateMbps = std::max(agg.videoBitrateMbps, sn.videoBitrateMbps);
            }
        }
        auto gs = globalMetrics_.snapshot();
        agg.transportRttMs = gs.transportRttMs;
        agg.transportLossPct = gs.transportLossPct;

        std::vector<MetricsSubscriber> subs;
        {
            std::lock_guard lk(metricsSubsMu_);
            subs = metricsSubs_;
        }
        for (auto& sub : subs) {
            ::capnp::MallocMessageBuilder mb;
            auto e = mb.initRoot<::fuvr::daemon::Envelope>();
            e.setSeq(0);
            e.setStreamId(sub.streamId);
            auto m = e.getBody().initMetrics();
            m.setCapturedAtNs(nowNs());
            m.setEncoderFps(agg.encoderFps);
            m.setEncoderEncodeMsAvg(agg.encoderEncodeMsAvg);
            m.setEncoderEncodeMsP95(agg.encoderEncodeMsP95);
            m.setTransportRttMs(agg.transportRttMs);
            m.setTransportLossPct(agg.transportLossPct);
            m.setDecoderFps(0.0f);
            m.setDecoderDecodeMsP95(0.0f);
            m.setVideoBitrateMbps(agg.videoBitrateMbps);
            std::vector<uint8_t> out;
            writePacked(mb, out);
            rpc_.send(sub.fd, out.data(), out.size());
        }
    }
}

} // namespace fuvr::daemon
