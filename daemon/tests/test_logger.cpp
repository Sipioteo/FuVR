// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr/logger.hpp"
#include "fuvrd.capnp.h"

using fuvr::daemon::Logger;
using fuvr::daemon::LogLevel;

TEST(LoggerSmoke, SubscribeReceivesReplayAndLiveEntries) {
    auto& log = Logger::instance();
    log.log(LogLevel::Info, "test", "session start: %d", 42);

    std::atomic<int> hits{0};
    std::vector<uint8_t> last;
    log.subscribe(99, [&](const uint8_t* d, std::size_t n) {
        last.assign(d, d + n);
        hits.fetch_add(1);
    });
    EXPECT_GE(hits.load(), 1);

    int before = hits.load();
    log.log(LogLevel::Warn, "test", "live entry");
    EXPECT_GT(hits.load(), before);

    ASSERT_FALSE(last.empty());
    kj::ArrayInputStream is(kj::arrayPtr(last.data(), last.size()));
    capnp::PackedMessageReader rd(is);
    auto env = rd.getRoot<fuvr::daemon::Envelope>();
    ASSERT_EQ(env.getBody().which(), fuvr::daemon::Envelope::Body::LOG);

    log.unsubscribe(99);
}
