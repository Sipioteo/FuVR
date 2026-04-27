// SPDX-License-Identifier: Apache-2.0
//
// streamEncodeStats decoupled subscription smoke test. We don't spin the full
// daemon — just exercise the EncodeStats encoding shape that
// Daemon::dispatchEncodeStats produces, asserting one envelope per frame.

#include <gtest/gtest.h>

#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvrd.capnp.h"

namespace {
std::vector<uint8_t> makeEncodeStatsEnvelope(uint64_t streamId, uint64_t frameId) {
    capnp::MallocMessageBuilder mb;
    auto e = mb.initRoot<fuvr::daemon::Envelope>();
    e.setSeq(0);
    e.setStreamId(streamId);
    auto es = e.getBody().initEncodeStats();
    es.setFrameId(frameId);
    es.setEncodeDurationNs(1'000'000);
    es.setEncodedSizeBytes(4096);
    es.setWasKeyframe(frameId == 0);
    kj::VectorOutputStream os;
    capnp::writePackedMessage(os, mb);
    auto a = os.getArray();
    return std::vector<uint8_t>(a.begin(), a.end());
}
}

TEST(StreamEncodeStats, OneEnvelopePerFrame) {
    std::vector<std::vector<uint8_t>> sent;
    for (uint64_t f = 0; f < 5; ++f) {
        sent.push_back(makeEncodeStatsEnvelope(11, f));
    }
    ASSERT_EQ(sent.size(), 5u);
    for (uint64_t f = 0; f < 5; ++f) {
        kj::ArrayInputStream is(kj::arrayPtr(sent[f].data(), sent[f].size()));
        capnp::PackedMessageReader rd(is);
        auto env = rd.getRoot<fuvr::daemon::Envelope>();
        ASSERT_EQ(env.getBody().which(), fuvr::daemon::Envelope::Body::ENCODE_STATS);
        EXPECT_EQ(env.getStreamId(), 11u);
        EXPECT_EQ(env.getBody().getEncodeStats().getFrameId(), f);
    }
}
