// SPDX-License-Identifier: Apache-2.0
#include "fuvr/pose_router.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <capnp/serialize.h>
#include <kj/array.h>
#include <kj/io.h>

#include "fuvr.capnp.h"
#include "fuvr/logger.hpp"
#include "fuvrd.capnp.h"

namespace fuvr::daemon {

namespace {
// Read FUVR_RT_DEBUG once at startup; gate all [LATENCY-DEBUG] logs on it so
// release builds pay nothing.
bool latencyDebugEnabled() noexcept {
    static const bool on = std::getenv("FUVR_RT_DEBUG") != nullptr;
    return on;
}
inline uint64_t monoNs() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

uint64_t PoseRouter::addSubscriber(uint64_t sessionId, PoseSubscriber cb) {
    std::lock_guard lk(mu_);
    uint64_t id = nextStreamId_++;
    subs_[id] = Entry{sessionId, std::move(cb)};
    return id;
}

void PoseRouter::removeSubscriber(uint64_t streamId) {
    std::lock_guard lk(mu_);
    subs_.erase(streamId);
}

void PoseRouter::removeSubscribersForSessions(const std::vector<uint64_t>& sessionIds) {
    std::lock_guard lk(mu_);
    for (auto it = subs_.begin(); it != subs_.end(); ) {
        bool match = false;
        for (uint64_t sid : sessionIds) {
            if (it->second.sessionId == sid) { match = true; break; }
        }
        if (match) it = subs_.erase(it);
        else ++it;
    }
}

static void fillSnapshot(::fuvr::daemon::PoseSnapshot::Builder& snap,
                         uint64_t receivedAtNs,
                         uint64_t questTimestampNs,
                         uint64_t predictedDisplayTimeNs,
                         const float l[7], const float r[7],
                         const float linVel[3], const float angVel[3],
                         const ControllerSampleIn& lc,
                         const ControllerSampleIn& rc,
                         const FovIn& leftFov,
                         const FovIn& rightFov) {
    snap.setReceivedAtNs(receivedAtNs);
    snap.setQuestTimestampNs(questTimestampNs);
    snap.setPredictedDisplayTimeNs(predictedDisplayTimeNs);
    snap.setLeftPosX(l[0]); snap.setLeftPosY(l[1]); snap.setLeftPosZ(l[2]);
    snap.setLeftRotX(l[3]); snap.setLeftRotY(l[4]); snap.setLeftRotZ(l[5]); snap.setLeftRotW(l[6]);
    snap.setRightPosX(r[0]); snap.setRightPosY(r[1]); snap.setRightPosZ(r[2]);
    snap.setRightRotX(r[3]); snap.setRightRotY(r[4]); snap.setRightRotZ(r[5]); snap.setRightRotW(r[6]);
    snap.setLinVelX(linVel[0]); snap.setLinVelY(linVel[1]); snap.setLinVelZ(linVel[2]);
    snap.setAngVelX(angVel[0]); snap.setAngVelY(angVel[1]); snap.setAngVelZ(angVel[2]);

    snap.setLeftControllerActive(lc.active);
    snap.setLeftControllerPosX(lc.pos[0]);
    snap.setLeftControllerPosY(lc.pos[1]);
    snap.setLeftControllerPosZ(lc.pos[2]);
    snap.setLeftControllerRotX(lc.rot[0]);
    snap.setLeftControllerRotY(lc.rot[1]);
    snap.setLeftControllerRotZ(lc.rot[2]);
    snap.setLeftControllerRotW(lc.rot[3]);
    snap.setLeftControllerLinVelX(lc.linVel[0]);
    snap.setLeftControllerLinVelY(lc.linVel[1]);
    snap.setLeftControllerLinVelZ(lc.linVel[2]);
    snap.setLeftControllerAngVelX(lc.angVel[0]);
    snap.setLeftControllerAngVelY(lc.angVel[1]);
    snap.setLeftControllerAngVelZ(lc.angVel[2]);

    snap.setRightControllerActive(rc.active);
    snap.setRightControllerPosX(rc.pos[0]);
    snap.setRightControllerPosY(rc.pos[1]);
    snap.setRightControllerPosZ(rc.pos[2]);
    snap.setRightControllerRotX(rc.rot[0]);
    snap.setRightControllerRotY(rc.rot[1]);
    snap.setRightControllerRotZ(rc.rot[2]);
    snap.setRightControllerRotW(rc.rot[3]);
    snap.setRightControllerLinVelX(rc.linVel[0]);
    snap.setRightControllerLinVelY(rc.linVel[1]);
    snap.setRightControllerLinVelZ(rc.linVel[2]);
    snap.setRightControllerAngVelX(rc.angVel[0]);
    snap.setRightControllerAngVelY(rc.angVel[1]);
    snap.setRightControllerAngVelZ(rc.angVel[2]);

    snap.setLeftFovAngleLeft(leftFov.angleLeft);
    snap.setLeftFovAngleRight(leftFov.angleRight);
    snap.setLeftFovAngleUp(leftFov.angleUp);
    snap.setLeftFovAngleDown(leftFov.angleDown);
    snap.setRightFovAngleLeft(rightFov.angleLeft);
    snap.setRightFovAngleRight(rightFov.angleRight);
    snap.setRightFovAngleUp(rightFov.angleUp);
    snap.setRightFovAngleDown(rightFov.angleDown);
}

bool PoseRouter::latestPoseSet(LatestPoseSet& out) const {
    std::lock_guard lk(latestMu_);
    if (!latest_.valid) return false;
    out = latest_;
    return true;
}

void PoseRouter::dispatchSnapshot(uint64_t sessionId,
                                  uint64_t receivedAtNs,
                                  uint64_t questTimestampNs,
                                  uint64_t predictedDisplayTimeNs,
                                  const float left[7],
                                  const float right[7],
                                  const float linVel[3],
                                  const float angVel[3],
                                  const ControllerSampleIn& leftCtrl,
                                  const ControllerSampleIn& rightCtrl,
                                  const FovIn& leftFov,
                                  const FovIn& rightFov) {
    // Populate the OpenVR-shim pose cache. HMD = midpoint of view positions,
    // orientation from left view (see header comment). Per-controller pose
    // gets full pos+quat+vel; valid bit derived from `active`.
    {
        LatestPoseSet next{};
        next.valid = true;
        next.questTimestampNs = questTimestampNs;
        next.receivedAtNs = receivedAtNs;
        next.validMask = 0x1; // HMD always valid once we got a frame
        next.hmd[0] = 0.5f * (left[0] + right[0]);
        next.hmd[1] = 0.5f * (left[1] + right[1]);
        next.hmd[2] = 0.5f * (left[2] + right[2]);
        next.hmd[3] = left[3]; // qx
        next.hmd[4] = left[4]; // qy
        next.hmd[5] = left[5]; // qz
        next.hmd[6] = left[6]; // qw
        next.hmd[7]  = linVel[0];
        next.hmd[8]  = linVel[1];
        next.hmd[9]  = linVel[2];
        next.hmd[10] = angVel[0];
        next.hmd[11] = angVel[1];
        next.hmd[12] = angVel[2];
        if (leftCtrl.active) {
            next.validMask |= 0x2;
            next.leftCtrl[0] = leftCtrl.pos[0];
            next.leftCtrl[1] = leftCtrl.pos[1];
            next.leftCtrl[2] = leftCtrl.pos[2];
            next.leftCtrl[3] = leftCtrl.rot[0];
            next.leftCtrl[4] = leftCtrl.rot[1];
            next.leftCtrl[5] = leftCtrl.rot[2];
            next.leftCtrl[6] = leftCtrl.rot[3];
            next.leftCtrl[7]  = leftCtrl.linVel[0];
            next.leftCtrl[8]  = leftCtrl.linVel[1];
            next.leftCtrl[9]  = leftCtrl.linVel[2];
            next.leftCtrl[10] = leftCtrl.angVel[0];
            next.leftCtrl[11] = leftCtrl.angVel[1];
            next.leftCtrl[12] = leftCtrl.angVel[2];
        } else {
            next.leftCtrl[6] = 1.0f; // identity quat for safety
        }
        if (rightCtrl.active) {
            next.validMask |= 0x4;
            next.rightCtrl[0] = rightCtrl.pos[0];
            next.rightCtrl[1] = rightCtrl.pos[1];
            next.rightCtrl[2] = rightCtrl.pos[2];
            next.rightCtrl[3] = rightCtrl.rot[0];
            next.rightCtrl[4] = rightCtrl.rot[1];
            next.rightCtrl[5] = rightCtrl.rot[2];
            next.rightCtrl[6] = rightCtrl.rot[3];
            next.rightCtrl[7]  = rightCtrl.linVel[0];
            next.rightCtrl[8]  = rightCtrl.linVel[1];
            next.rightCtrl[9]  = rightCtrl.linVel[2];
            next.rightCtrl[10] = rightCtrl.angVel[0];
            next.rightCtrl[11] = rightCtrl.angVel[1];
            next.rightCtrl[12] = rightCtrl.angVel[2];
        } else {
            next.rightCtrl[6] = 1.0f;
        }
        std::lock_guard lk(latestMu_);
        latest_ = next;
    }

    const uint64_t tStartNs = latencyDebugEnabled() ? monoNs() : 0ull;
    std::vector<std::pair<uint64_t, PoseSubscriber>> targets;
    {
        std::lock_guard lk(mu_);
        targets.reserve(subs_.size());
        for (auto& [id, e] : subs_) {
            if (e.sessionId == sessionId) targets.emplace_back(id, e.cb);
        }
    }
    if (targets.empty()) return;

    for (auto& [streamId, cb] : targets) {
        ::capnp::MallocMessageBuilder out;
        auto e = out.initRoot<::fuvr::daemon::Envelope>();
        e.setSeq(0);
        e.setStreamId(streamId);
        auto snap = e.getBody().initPoseSnapshot();
        fillSnapshot(snap, receivedAtNs, questTimestampNs, predictedDisplayTimeNs,
                     left, right, linVel, angVel, leftCtrl, rightCtrl,
                     leftFov, rightFov);
        // Why: runtime reads envelopes flat (see daemon_client.cpp).
        kj::Array<::capnp::word> flat = ::capnp::messageToFlatArray(out);
        auto bytes = flat.asBytes();
        cb(bytes.begin(), bytes.size());
    }

    if (latencyDebugEnabled()) {
        // Increment a per-second dispatch counter; only snapshot it when the
        // 1Hz throttle fires. Both atomics are file-static so they hold across
        // calls without allocation.
        static std::atomic<uint32_t> dispatchCalls{0};
        static std::atomic<uint64_t> lastLogNs{0};
        dispatchCalls.fetch_add(1, std::memory_order_relaxed);
        uint64_t nowL = monoNs();
        uint64_t prev = lastLogNs.load(std::memory_order_relaxed);
        if (nowL - prev >= 1'000'000'000ull &&
            lastLogNs.compare_exchange_strong(prev, nowL)) {
            uint32_t cps = dispatchCalls.exchange(0, std::memory_order_relaxed);
            uint64_t durUs = (nowL - tStartNs) / 1000ull;
            FUVR_LOG_INFO("daemon",
                          "[LATENCY-DEBUG] poseRouter: subs=%zu duration_us=%llu "
                          "calls/s=%u",
                          targets.size(), (unsigned long long)durUs,
                          (unsigned)cps);
        }
    }
}

bool PoseRouter::ingestPackedUpstreamFrame(const uint8_t* data, std::size_t len,
                                           uint64_t sessionId, uint64_t receivedAtNs) {
    kj::ArrayInputStream is(kj::arrayPtr(data, len));
    ::capnp::PackedMessageReader reader(is);
    auto frame = reader.getRoot<::fuvr::proto::UpstreamFrame>();
    auto hmd = frame.getHmd();
    auto lvw = hmd.getLeftView();
    auto rvw = hmd.getRightView();
    auto lp = lvw.getPose();
    auto rp = rvw.getPose();
    auto lfov = lvw.getFov();
    auto rfov = rvw.getFov();
    FovIn leftFov{ lfov.getAngleLeft(), lfov.getAngleRight(),
                   lfov.getAngleUp(),   lfov.getAngleDown() };
    FovIn rightFov{ rfov.getAngleLeft(), rfov.getAngleRight(),
                    rfov.getAngleUp(),   rfov.getAngleDown() };
    auto lv = hmd.getLinearVelocity();
    auto av = hmd.getAngularVelocity();

    float left[7] = {
        lp.getPosition().getX(), lp.getPosition().getY(), lp.getPosition().getZ(),
        lp.getOrientation().getX(), lp.getOrientation().getY(),
        lp.getOrientation().getZ(), lp.getOrientation().getW(),
    };
    float right[7] = {
        rp.getPosition().getX(), rp.getPosition().getY(), rp.getPosition().getZ(),
        rp.getOrientation().getX(), rp.getOrientation().getY(),
        rp.getOrientation().getZ(), rp.getOrientation().getW(),
    };
    float linVel[3] = { lv.getX(), lv.getY(), lv.getZ() };
    float angVel[3] = { av.getX(), av.getY(), av.getZ() };

    ControllerSampleIn lc, rc;
    auto controllers = frame.getControllers();
    for (auto c : controllers) {
        ControllerSampleIn* dst = nullptr;
        if (c.getHand() == ::fuvr::proto::ControllerHand::LEFT)  dst = &lc;
        else if (c.getHand() == ::fuvr::proto::ControllerHand::RIGHT) dst = &rc;
        if (!dst) continue;
        dst->active = c.getIsActive();
        auto cp = c.getPose();
        dst->pos[0] = cp.getPosition().getX();
        dst->pos[1] = cp.getPosition().getY();
        dst->pos[2] = cp.getPosition().getZ();
        dst->rot[0] = cp.getOrientation().getX();
        dst->rot[1] = cp.getOrientation().getY();
        dst->rot[2] = cp.getOrientation().getZ();
        dst->rot[3] = cp.getOrientation().getW();
        auto cl = c.getLinearVelocity();
        dst->linVel[0] = cl.getX();
        dst->linVel[1] = cl.getY();
        dst->linVel[2] = cl.getZ();
        auto ca = c.getAngularVelocity();
        dst->angVel[0] = ca.getX();
        dst->angVel[1] = ca.getY();
        dst->angVel[2] = ca.getZ();
    }

    dispatchSnapshot(sessionId, receivedAtNs, hmd.getTimestampNs(),
                     hmd.getPredictedDisplayTimeNs(), left, right, linVel, angVel,
                     lc, rc, leftFov, rightFov);
    return true;
}

} // namespace fuvr::daemon
