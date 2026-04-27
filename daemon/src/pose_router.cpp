// SPDX-License-Identifier: Apache-2.0
#include "fuvr/pose_router.hpp"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/array.h>
#include <kj/io.h>

#include "fuvr.capnp.h"
#include "fuvrd.capnp.h"

namespace fuvr::daemon {

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

static void fillSnapshot(::fuvr::daemon::PoseSnapshot::Builder& snap,
                         uint64_t receivedAtNs,
                         uint64_t questTimestampNs,
                         uint64_t predictedDisplayTimeNs,
                         const float l[7], const float r[7],
                         const float linVel[3], const float angVel[3]) {
    snap.setReceivedAtNs(receivedAtNs);
    snap.setQuestTimestampNs(questTimestampNs);
    snap.setPredictedDisplayTimeNs(predictedDisplayTimeNs);
    snap.setLeftPosX(l[0]); snap.setLeftPosY(l[1]); snap.setLeftPosZ(l[2]);
    snap.setLeftRotX(l[3]); snap.setLeftRotY(l[4]); snap.setLeftRotZ(l[5]); snap.setLeftRotW(l[6]);
    snap.setRightPosX(r[0]); snap.setRightPosY(r[1]); snap.setRightPosZ(r[2]);
    snap.setRightRotX(r[3]); snap.setRightRotY(r[4]); snap.setRightRotZ(r[5]); snap.setRightRotW(r[6]);
    snap.setLinVelX(linVel[0]); snap.setLinVelY(linVel[1]); snap.setLinVelZ(linVel[2]);
    snap.setAngVelX(angVel[0]); snap.setAngVelY(angVel[1]); snap.setAngVelZ(angVel[2]);
}

void PoseRouter::dispatchSnapshot(uint64_t sessionId,
                                  uint64_t receivedAtNs,
                                  uint64_t questTimestampNs,
                                  uint64_t predictedDisplayTimeNs,
                                  const float left[7],
                                  const float right[7],
                                  const float linVel[3],
                                  const float angVel[3]) {
    ::capnp::MallocMessageBuilder msg;
    auto env = msg.initRoot<::fuvr::daemon::Envelope>();
    env.setSeq(0);
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
                     left, right, linVel, angVel);
        kj::VectorOutputStream os;
        ::capnp::writePackedMessage(os, out);
        auto bytes = os.getArray();
        cb(bytes.begin(), bytes.size());
    }
}

bool PoseRouter::ingestPackedUpstreamFrame(const uint8_t* data, std::size_t len,
                                           uint64_t sessionId, uint64_t receivedAtNs) {
    kj::ArrayInputStream is(kj::arrayPtr(data, len));
    ::capnp::PackedMessageReader reader(is);
    auto frame = reader.getRoot<::fuvr::proto::UpstreamFrame>();
    auto hmd = frame.getHmd();
    auto lp = hmd.getLeftView().getPose();
    auto rp = hmd.getRightView().getPose();
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

    dispatchSnapshot(sessionId, receivedAtNs, hmd.getTimestampNs(),
                     hmd.getPredictedDisplayTimeNs(), left, right, linVel, angVel);
    return true;
}

} // namespace fuvr::daemon
