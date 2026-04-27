// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>
#include <kj/exception.h>

#include "fuvr/iosurface_bridge.hpp"
#include "fuvr/logger.hpp"
#include "fuvr/q_metrics_parser.hpp"
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
    // Why: runtime reads daemon→runtime envelopes with FlatArrayMessageReader
    // (see runtime-macos/src/daemon_client.cpp), so we MUST emit flat (unpacked)
    // capnp here. Name kept for source diff minimization; semantics are flat.
    kj::Array<::capnp::word> flat = ::capnp::messageToFlatArray(mb);
    auto bytes = flat.asBytes();
    out.assign(bytes.begin(), bytes.end());
}
}

Daemon::Daemon() = default;
Daemon::~Daemon() { stop(); }

bool Daemon::start(const std::string& path) {
    std::string p = path.empty() ? defaultSocketPath() : path;
    running_.store(true);

    // Why: tests link runtime+daemon into one process and bypass XPC via the
    // in-process registry; skip the launchd-backed mach service in that mode.
    if (std::getenv("FUVR_INPROCESS_HANDOFF") == nullptr) {
        xpcService_ = IOSurfaceXpcService::create("com.fuvr.daemon.surface");
    }

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
    clockSyncThread_ = std::thread([this] { clockSyncLoop(); });
    return true;
}

void Daemon::stop() {
    if (!running_.exchange(false)) return;
    rpc_.stop();
    if (metricsThread_.joinable()) metricsThread_.join();
    if (clockSyncThread_.joinable()) clockSyncThread_.join();
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
    if (channel == FuvrChannel_Control) {
        d->handleControlMessage(data, len);
        return;
    }
    // [DEBUG-POSE] log any non-Video channel arrivals at 1 Hz so we can see
    // pose ingest from the Quest landing in the daemon at all.
    if (channel != FuvrChannel_Video) {
        static std::atomic<uint64_t> lastLogNs{0};
        uint64_t nowL = nowNs();
        uint64_t prev = lastLogNs.load();
        if (nowL - prev >= 1'000'000'000ull &&
            lastLogNs.compare_exchange_strong(prev, nowL)) {
            FUVR_LOG_INFO("daemon",
                          "[DEBUG-POSE] transport recv ch=%u len=%zu",
                          (unsigned)channel, len);
        }
    }
    if (channel != FuvrChannel_Pose) return;
    // Why: Blender's runtime reconnects across "Start VR Session" cycles
    // without sending stopSession; sessions_ accumulates entries with
    // increasing ids. The most recent session owns the live RPC fd and
    // active subscribers. Picking sessions_.begin() (oldest) silently routed
    // poses to dead subscribers. Dispatch to every active session — poseRouter
    // filters by sessionId so only live subscribers fire.
    std::vector<uint64_t> sids;
    {
        std::lock_guard lk(d->sessionsMu_);
        sids.reserve(d->sessions_.size());
        for (auto& [id, _] : d->sessions_) sids.push_back(id);
    }
    uint64_t now = nowNs();
    for (uint64_t sid : sids) {
        d->poseRouter_.ingestPackedUpstreamFrame(data, len, sid, now);
        d->inputRouter_.ingestPackedUpstreamFrame(data, len, sid, now);
    }
    {
        static std::atomic<uint64_t> lastPoseLogNs{0};
        // Per-second pose-frame counter: incremented on every pose frame,
        // snapshotted+reset only when the 1Hz throttle fires below.
        static std::atomic<uint32_t> poseFrameCount{0};
        poseFrameCount.fetch_add(1, std::memory_order_relaxed);
        uint64_t nowL = now;
        uint64_t prev = lastPoseLogNs.load();
        if (nowL - prev >= 1'000'000'000ull &&
            lastPoseLogNs.compare_exchange_strong(prev, nowL)) {
            FUVR_LOG_INFO("daemon",
                          "[DEBUG-POSE] pose frame: %zu bytes, sessions=%zu",
                          len, sids.size());
            // [LATENCY-DEBUG] Decode the UpstreamFrame *only* on the 1Hz tick
            // to peek hmd timestamps. Lag is across-clock (Mac mono vs Quest
            // mono); only the *trend* over a 60s session is diagnostic.
            uint32_t fps = poseFrameCount.exchange(0, std::memory_order_relaxed);
            uint64_t questTsNs = 0;
            uint64_t questPredictedNs = 0;
            auto e = kj::runCatchingExceptions([&]() {
                kj::ArrayInputStream is(kj::arrayPtr(data, len));
                ::capnp::PackedMessageReader reader(is);
                auto frame = reader.getRoot<::fuvr::proto::UpstreamFrame>();
                auto hmd = frame.getHmd();
                questTsNs = hmd.getTimestampNs();
                questPredictedNs = hmd.getPredictedDisplayTimeNs();
            });
            (void)e;
            int64_t lagMs = static_cast<int64_t>(
                (static_cast<int64_t>(nowL) -
                 static_cast<int64_t>(questPredictedNs)) / 1'000'000);
            FUVR_LOG_INFO("daemon",
                          "[LATENCY-DEBUG] onTransportRecv: pose_fps=%u "
                          "lag_ms=%lld questTs_ns=%llu predict_ns=%llu",
                          (unsigned)fps, (long long)lagMs,
                          (unsigned long long)questTsNs,
                          (unsigned long long)questPredictedNs);
        }
    }
}

void Daemon::handleControlMessage(const uint8_t* data, std::size_t len) {
    // Why: peers may send malformed or non-capnp data (early handshake bytes,
    // framing glitches, etc.). KJ exceptions propagate through this C ABI
    // callback and would terminate the daemon. Catch via kj::runCatchingExceptions
    // because the daemon is compiled with -fno-exceptions.
    auto e = kj::runCatchingExceptions([&]() {
    kj::ArrayInputStream is(kj::arrayPtr(data, len));
    ::capnp::PackedMessageReader reader(is);
    auto cm = reader.getRoot<::fuvr::proto::ControlMessage>();
    if (cm.which() == ::fuvr::proto::ControlMessage::CLOCK_SYNC) {
        auto cs = cm.getClockSync();
        if (cs.which() != ::fuvr::proto::ClockSync::PONG) return;
        auto p = cs.getPong();
        clockSync_.onPong(p.getT0(), p.getT1(), p.getT2());
        return;
    }
    if (cm.which() == ::fuvr::proto::ControlMessage::HELLO_FROM_QUEST) {
        // Why: cache the headset's self-reported capabilities so the runtime
        // can fetch them via getDeviceCapabilities and stop hardcoding Quest 3
        // values (perEye, refresh rates, hand/eye tracking).
        auto h = cm.getHelloFromQuest();
        CachedCapabilities next;
        next.valid = true;
        next.deviceModel = h.getDeviceModel().cStr();
        next.systemVersion = h.getSystemVersion().cStr();
        next.perEyeWidth = h.getPerEyeWidth();
        next.perEyeHeight = h.getPerEyeHeight();
        auto rates = h.getRefreshRatesHz();
        next.refreshRatesHz.reserve(rates.size());
        for (auto r : rates) next.refreshRatesHz.push_back(r);
        next.hasHandTracking = h.getHasHandTracking();
        next.hasEyeTracking = h.getHasEyeTracking();
        FUVR_LOG_INFO("daemon",
                      "helloFromQuest: model='%s' perEye=%ux%u rates=%zu hand=%d eye=%d",
                      next.deviceModel.c_str(), next.perEyeWidth,
                      next.perEyeHeight, next.refreshRatesHz.size(),
                      (int)next.hasHandTracking, (int)next.hasEyeTracking);
        std::lock_guard lk(capsMu_);
        caps_ = std::move(next);
        return;
    }
    if (cm.which() == ::fuvr::proto::ControlMessage::ERROR) {
        auto txt = cm.getError();
        std::string_view sv(txt.cStr(), txt.size());
        if (auto qm = parseQMetrics(sv)) {
            std::lock_guard lk(qMetricsMu_);
            if (qm->hasFps)       qDecoderFps_         = qm->decoderFps;
            if (qm->hasDecodeP95) qDecoderDecodeMsP95_ = qm->decoderDecodeMsP95;
        }
    }
    });
    if (e != nullptr) {
        std::fprintf(stderr, "[fuvrd] handleControlMessage: drop malformed %zu bytes\n", len);
    }
}

void Daemon::clockSyncLoop() {
    using namespace std::chrono_literals;
    while (running_.load()) {
        std::this_thread::sleep_for(1000ms);
        if (!running_.load()) break;
        bool hasSession;
        {
            std::lock_guard lk(sessionsMu_);
            hasSession = !sessions_.empty();
        }
        if (!hasSession || !transport_) continue;
        clockSync_.issuePing([this](const uint8_t* d, std::size_t n) {
#ifndef FUVR_DAEMON_NO_TRANSPORT
            if (transport_) fuvr_transport_send(transport_, FuvrChannel_Control, d, n);
#else
            (void)this; (void)d; (void)n;
#endif
        });
    }
}

void Daemon::dispatchEncodeStats(const EncodeStatsEvent& ev) {
    auto encodeFor = [&](uint64_t streamId, std::vector<uint8_t>& out) {
        ::capnp::MallocMessageBuilder mb;
        auto e = mb.initRoot<::fuvr::daemon::Envelope>();
        e.setSeq(0);
        e.setStreamId(streamId);
        auto es = e.getBody().initEncodeStats();
        es.setFrameId(ev.frameId);
        es.setEncodeDurationNs(ev.encodeDurationNs);
        es.setEncodedSizeBytes(ev.encodedSizeBytes);
        es.setWasKeyframe(ev.wasKeyframe);
        writePacked(mb, out);
    };

    std::vector<EncodeStatsSubscriber> dedicated;
    {
        std::lock_guard lk(encodeStatsSubsMu_);
        dedicated = encodeStatsSubs_;
    }
    for (auto& sub : dedicated) {
        std::vector<uint8_t> out;
        encodeFor(sub.streamId, out);
        rpc_.send(sub.fd, out.data(), out.size());
    }

    std::vector<MetricsSubscriber> piggy;
    {
        std::lock_guard lk(metricsSubsMu_);
        piggy = metricsSubs_;
    }
    if (!piggy.empty()) {
        // Why: legacy subscribers (pre pass 4) consumed encodeStats on the
        // metrics stream. Keep dual-emitting until those clients move; warn
        // exactly once per process so the noise stays manageable.
        if (!piggybackWarnLogged_.exchange(true)) {
            FUVR_LOG_WARN("daemon",
                "encodeStats piggy-back on streamMetrics is deprecated; subscribe to streamEncodeStats");
        }
        for (auto& sub : piggy) {
            std::vector<uint8_t> out;
            encodeFor(sub.streamId, out);
            rpc_.send(sub.fd, out.data(), out.size());
        }
    }
}

void Daemon::onEnvelope(const InboundRpc& rpc) {
    auto exc = kj::runCatchingExceptions([&]() {
    // Why: runtime sendEnvelope uses messageToFlatArray (unpacked); daemon
    // must therefore decode as a FlatArrayMessageReader. PackedMessageReader
    // would corrupt the first word and trigger "Message did not contain a
    // root pointer".
    const auto* words = reinterpret_cast<const ::capnp::word*>(rpc.envelope.data());
    const std::size_t wordCount = rpc.envelope.size() / sizeof(::capnp::word);
    ::capnp::FlatArrayMessageReader reader(kj::arrayPtr(words, wordCount));
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
        FUVR_LOG_INFO("daemon", "session start request: %ux%u @ %u Hz",
                      req.getPerEyeWidth(), req.getPerEyeHeight(),
                      req.getRefreshRateHz());
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

        // Why: Blender (and other XR apps) reconnect across "Start VR Session"
        // cycles without sending stopSession. Without cleanup, sessions_ grows
        // and so do all the per-session subscriber lists (poseRouter,
        // inputRouter, metricsSubs, encodeStatsSubs). Every encoded frame then
        // fans out to all stale subscribers — most pointing at dead RPC fds —
        // and the daemon visibly slows down after ~30 s. Drop every prior
        // session and its subs whenever a new client starts up. Single-client
        // assumption is fine for now (only one runtime per Mac).
        std::vector<uint64_t> staleSessionIds;
        {
            std::lock_guard lk(sessionsMu_);
            staleSessionIds.reserve(sessions_.size());
            for (auto& [sid, _] : sessions_) staleSessionIds.push_back(sid);
            sessions_.clear();
        }
        if (!staleSessionIds.empty()) {
            poseRouter_.removeSubscribersForSessions(staleSessionIds);
            inputRouter_.removeSubscribersForSessions(staleSessionIds);
            FUVR_LOG_INFO("daemon",
                          "evicted %zu stale session(s) on new startSession",
                          staleSessionIds.size());
        }
        {
            std::lock_guard lk(metricsSubsMu_);
            metricsSubs_.clear();
        }
        {
            std::lock_guard lk(encodeStatsSubsMu_);
            encodeStatsSubs_.clear();
        }
        {
            std::lock_guard lk(logSubsMu_);
            logSubs_.clear();
        }

        uint64_t id;
        uint32_t vid = 0;
        {
            std::lock_guard lk(sessionsMu_);
            id = nextSessionId_++;
            auto s = std::make_unique<Session>(id, cfg, transport_,
                [this](const EncodeStatsEvent& ev) { dispatchEncodeStats(ev); });
            vid = s->virtualDisplayId();
            sessions_[id] = std::move(s);
        }

        // Why: kick a ping immediately so the response can carry a real
        // offset; wait briefly for the first pong before falling back to 0.
        if (transport_) {
            clockSync_.issuePing([this](const uint8_t* d, std::size_t n) {
#ifndef FUVR_DAEMON_NO_TRANSPORT
                if (transport_) fuvr_transport_send(transport_, FuvrChannel_Control, d, n);
#else
                (void)d; (void)n;
#endif
            });
        }
        auto cs = clockSync_.waitForFirst(std::chrono::milliseconds(200));

        reply([&](auto e) {
            auto ack = e.getBody().initStartSessionAck();
            ack.setSessionId(id);
            ack.setClockOffsetNs(cs.offsetNs);
            ack.setOneWayDelayNs(cs.oneWayDelayNs);
            ack.setVirtualDisplayId(vid);
        });
        if (cs.samples == 0) {
            // Why: surface the failure as an error message on the runtime's
            // log/error channel rather than silently returning zeros.
            ::capnp::MallocMessageBuilder mb;
            auto e2 = mb.initRoot<::fuvr::daemon::Envelope>();
            e2.setSeq(0);
            e2.setStreamId(0);
            e2.getBody().setError("clock sync: no pong within 200 ms; offset=0");
            std::vector<uint8_t> out;
            writePacked(mb, out);
            rpc_.send(rpc.clientFd, out.data(), out.size());
        }
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

        uint64_t tok = req.getSurfaceToken();
        CVPixelBufferRef pb = pixelBufferFromToken(xpcService_.get(), tok);
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
        float leftFov[4] = {
            req.getRenderedLeftFovAngleLeft(),  req.getRenderedLeftFovAngleRight(),
            req.getRenderedLeftFovAngleUp(),    req.getRenderedLeftFovAngleDown(),
        };
        float rightFov[4] = {
            req.getRenderedRightFovAngleLeft(),  req.getRenderedRightFovAngleRight(),
            req.getRenderedRightFovAngleUp(),    req.getRenderedRightFovAngleDown(),
        };
        s->submitFrame(pb, req.getFrameId(), req.getRenderStartNs(),
                       req.getForceIdr(), left, right, leftFov, rightFov);
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
        FUVR_LOG_INFO("daemon",
                      "[DEBUG-POSE] streamPoses subscribe: sessionId=%llu fd=%d streamId=%llu",
                      (unsigned long long)sid, fd, (unsigned long long)streamId);
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
    case ::fuvr::daemon::Envelope::Body::STREAM_INPUTS: {
        auto req = body.getStreamInputs();
        int fd = rpc.clientFd;
        uint64_t sid = req.getSessionId();
        uint64_t streamId = inputRouter_.addSubscriber(sid,
            [this, fd](const uint8_t* d, std::size_t n) { rpc_.send(fd, d, n); });
        reply([&](auto e) {
            e.setStreamId(streamId);
            e.getBody().setOk();
        });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::STREAM_ENCODE_STATS: {
        std::lock_guard lk(encodeStatsSubsMu_);
        uint64_t sid = static_cast<uint64_t>(encodeStatsSubs_.size()) + 1;
        encodeStatsSubs_.push_back({rpc.clientFd, sid});
        reply([&](auto e) {
            e.setStreamId(sid);
            e.getBody().setOk();
        });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::STREAM_LOGS: {
        int fd = rpc.clientFd;
        uint64_t sid;
        {
            std::lock_guard lk(logSubsMu_);
            sid = static_cast<uint64_t>(logSubs_.size()) + 1;
            logSubs_.push_back({fd, sid});
        }
        Logger::instance().subscribe(sid,
            [this, fd](const uint8_t* d, std::size_t n) { rpc_.send(fd, d, n); });
        reply([&](auto e) {
            e.setStreamId(sid);
            e.getBody().setOk();
        });
        break;
    }
    case ::fuvr::daemon::Envelope::Body::GET_DEVICE_CAPABILITIES: {
        // Why: lets the runtime fetch the latest helloFromQuest snapshot so
        // it can replace its hardcoded Quest 3 defaults with values reflecting
        // the actual headset (perEye dims, supported refresh rates, hand/eye
        // tracking presence). If no Quest has connected yet, the response
        // carries `valid = false` and the runtime keeps its safe defaults.
        CachedCapabilities snap;
        {
            std::lock_guard lk(capsMu_);
            snap = caps_;
        }
        reply([&](auto e) {
            auto r = e.getBody().initDeviceCapabilitiesResponse();
            r.setValid(snap.valid);
            r.setDeviceModel(snap.deviceModel);
            r.setSystemVersion(snap.systemVersion);
            r.setPerEyeWidth(snap.perEyeWidth);
            r.setPerEyeHeight(snap.perEyeHeight);
            auto rates = r.initRefreshRatesHz(static_cast<unsigned>(snap.refreshRatesHz.size()));
            for (size_t i = 0; i < snap.refreshRatesHz.size(); ++i)
                rates.set(static_cast<unsigned>(i), snap.refreshRatesHz[i]);
            r.setHasHandTracking(snap.hasHandTracking);
            r.setHasEyeTracking(snap.hasEyeTracking);
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
    });
    if (exc != nullptr) {
        std::fprintf(stderr, "[fuvrd] onEnvelope: drop malformed RPC (%zu bytes)\n",
                     rpc.envelope.size());
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
            float qFps, qP95;
            {
                std::lock_guard lk(qMetricsMu_);
                qFps = qDecoderFps_;
                qP95 = qDecoderDecodeMsP95_;
            }
            m.setDecoderFps(qFps);
            m.setDecoderDecodeMsP95(qP95);
            m.setVideoBitrateMbps(agg.videoBitrateMbps);
            std::vector<uint8_t> out;
            writePacked(mb, out);
            rpc_.send(sub.fd, out.data(), out.size());
        }
    }
}

} // namespace fuvr::daemon
