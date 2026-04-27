// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvrd.capnp.h"

TEST(RpcEnvelope, RoundTripStartSession) {
    capnp::MallocMessageBuilder mb;
    auto env = mb.initRoot<fuvr::daemon::Envelope>();
    env.setSeq(42);
    env.setStreamId(0);
    auto req = env.getBody().initStartSession();
    req.setPerEyeWidth(2064);
    req.setPerEyeHeight(2208);
    req.setRefreshRateHz(90);
    req.setVideoCodec(fuvr::daemon::VideoCodec::HEVC);
    req.setVideoBitrateBps(60'000'000);
    req.setForceIdrEveryFrames(120);
    req.setAudioEnabled(true);
    req.setEnableVirtualDisplay(false);

    kj::VectorOutputStream os;
    capnp::writePackedMessage(os, mb);
    auto bytes = os.getArray();
    ASSERT_GT(bytes.size(), 0u);

    kj::ArrayInputStream is(bytes);
    capnp::PackedMessageReader reader(is);
    auto e = reader.getRoot<fuvr::daemon::Envelope>();
    EXPECT_EQ(e.getSeq(), 42u);
    auto r = e.getBody().getStartSession();
    EXPECT_EQ(r.getPerEyeWidth(), 2064u);
    EXPECT_EQ(r.getPerEyeHeight(), 2208u);
    EXPECT_EQ(r.getRefreshRateHz(), 90u);
    EXPECT_EQ(r.getVideoCodec(), fuvr::daemon::VideoCodec::HEVC);
    EXPECT_EQ(r.getVideoBitrateBps(), 60'000'000u);
    EXPECT_EQ(r.getForceIdrEveryFrames(), 120u);
    EXPECT_TRUE(r.getAudioEnabled());
    EXPECT_FALSE(r.getEnableVirtualDisplay());
}
