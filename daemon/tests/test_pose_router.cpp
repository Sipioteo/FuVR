// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr/pose_router.hpp"
#include "fuvr.capnp.h"
#include "fuvrd.capnp.h"

using fuvr::daemon::PoseRouter;

TEST(PoseRouter, RoutesUpstreamFrameToSubscriber) {
    PoseRouter router;
    std::atomic<int> hits{0};
    std::vector<uint8_t> last;

    auto streamId = router.addSubscriber(7, [&](const uint8_t* d, std::size_t n) {
        last.assign(d, d + n);
        hits.fetch_add(1);
    });
    EXPECT_GT(streamId, 0u);

    capnp::MallocMessageBuilder mb;
    auto frame = mb.initRoot<fuvr::proto::UpstreamFrame>();
    frame.setCorrelationFrameId(99);
    auto hmd = frame.initHmd();
    hmd.setTimestampNs(123'456'789);
    hmd.setPredictedDisplayTimeNs(987'654'321);
    auto lp = hmd.initLeftView().initPose();
    lp.initPosition().setX(1.0f);
    lp.getPosition().setY(2.0f);
    lp.getPosition().setZ(3.0f);
    auto lq = lp.initOrientation();
    lq.setX(0.0f); lq.setY(0.0f); lq.setZ(0.0f); lq.setW(1.0f);
    auto rp = hmd.initRightView().initPose();
    rp.initPosition().setX(4.0f);
    rp.getPosition().setY(5.0f);
    rp.getPosition().setZ(6.0f);
    auto rq = rp.initOrientation();
    rq.setX(0.0f); rq.setY(0.0f); rq.setZ(0.0f); rq.setW(1.0f);
    hmd.initLinearVelocity().setX(0.5f);
    hmd.initAngularVelocity().setY(0.25f);

    kj::VectorOutputStream os;
    capnp::writePackedMessage(os, mb);
    auto bytes = os.getArray();

    ASSERT_TRUE(router.ingestPackedUpstreamFrame(bytes.begin(), bytes.size(), 7, 1000));
    EXPECT_EQ(hits.load(), 1);
    ASSERT_FALSE(last.empty());

    kj::ArrayInputStream is(kj::arrayPtr(last.data(), last.size()));
    capnp::PackedMessageReader rd(is);
    auto env = rd.getRoot<fuvr::daemon::Envelope>();
    ASSERT_EQ(env.getBody().which(), fuvr::daemon::Envelope::Body::POSE_SNAPSHOT);
    auto snap = env.getBody().getPoseSnapshot();
    EXPECT_EQ(snap.getReceivedAtNs(), 1000u);
    EXPECT_EQ(snap.getQuestTimestampNs(), 123'456'789u);
    EXPECT_FLOAT_EQ(snap.getLeftPosX(), 1.0f);
    EXPECT_FLOAT_EQ(snap.getLeftPosY(), 2.0f);
    EXPECT_FLOAT_EQ(snap.getRightPosZ(), 6.0f);
    EXPECT_FLOAT_EQ(snap.getLinVelX(), 0.5f);
    EXPECT_FLOAT_EQ(snap.getAngVelY(), 0.25f);

    router.removeSubscriber(streamId);
    ASSERT_TRUE(router.ingestPackedUpstreamFrame(bytes.begin(), bytes.size(), 7, 2000));
    EXPECT_EQ(hits.load(), 1);
}
