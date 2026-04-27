// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr/input_router.hpp"
#include "fuvr.capnp.h"
#include "fuvrd.capnp.h"

using fuvr::daemon::InputRouter;

TEST(InputRouter, RoutesUpstreamFrameInputsToSubscriber) {
    InputRouter router;
    std::atomic<int> hits{0};
    std::vector<uint8_t> last;

    auto streamId = router.addSubscriber(7, [&](const uint8_t* d, std::size_t n) {
        last.assign(d, d + n);
        hits.fetch_add(1);
    });
    EXPECT_GT(streamId, 0u);

    capnp::MallocMessageBuilder mb;
    auto frame = mb.initRoot<fuvr::proto::UpstreamFrame>();
    frame.initHmd().setTimestampNs(42);
    auto inputs = frame.initInputs(2);
    auto l = inputs[0];
    l.setHand(fuvr::proto::ControllerHand::LEFT);
    l.setTrigger(0.5f);
    l.setSqueeze(0.25f);
    l.setThumbstickX(-0.5f);
    l.setThumbstickClick(true);
    l.setButtonAClick(true);
    auto r = inputs[1];
    r.setHand(fuvr::proto::ControllerHand::RIGHT);
    r.setTrigger(1.0f);
    r.setThumbstickY(0.75f);
    r.setButtonBClick(true);
    r.setSystemClick(true);

    kj::VectorOutputStream os;
    capnp::writePackedMessage(os, mb);
    auto bytes = os.getArray();

    ASSERT_TRUE(router.ingestPackedUpstreamFrame(bytes.begin(), bytes.size(), 7, 1000));
    EXPECT_EQ(hits.load(), 1);
    ASSERT_FALSE(last.empty());

    kj::ArrayInputStream is(kj::arrayPtr(last.data(), last.size()));
    capnp::PackedMessageReader rd(is);
    auto env = rd.getRoot<fuvr::daemon::Envelope>();
    ASSERT_EQ(env.getBody().which(), fuvr::daemon::Envelope::Body::INPUT_SNAPSHOT);
    auto snap = env.getBody().getInputSnapshot();
    EXPECT_EQ(snap.getReceivedAtNs(), 1000u);
    EXPECT_EQ(snap.getQuestClockNs(), 42u);
    auto lc = snap.getLeft();
    EXPECT_TRUE(lc.getActive());
    EXPECT_FLOAT_EQ(lc.getTrigger(), 0.5f);
    EXPECT_FLOAT_EQ(lc.getSqueeze(), 0.25f);
    EXPECT_FLOAT_EQ(lc.getThumbstickX(), -0.5f);
    EXPECT_TRUE(lc.getThumbstickClick());
    EXPECT_TRUE(lc.getButtonAClick());
    auto rc = snap.getRight();
    EXPECT_TRUE(rc.getActive());
    EXPECT_FLOAT_EQ(rc.getTrigger(), 1.0f);
    EXPECT_FLOAT_EQ(rc.getThumbstickY(), 0.75f);
    EXPECT_TRUE(rc.getButtonBClick());
    EXPECT_TRUE(rc.getSystemClick());

    // Exactly one envelope per subscriber per frame.
    router.removeSubscriber(streamId);
    ASSERT_TRUE(router.ingestPackedUpstreamFrame(bytes.begin(), bytes.size(), 7, 2000));
    EXPECT_EQ(hits.load(), 1);
}
