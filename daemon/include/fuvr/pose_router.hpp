// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "fuvrd.capnp.h"

namespace fuvr::daemon {

// Subscriber receives raw bytes representing a packed Envelope carrying a
// PoseSnapshot. The router fans out one envelope per inbound UpstreamFrame.
using PoseSubscriber = std::function<void(const uint8_t*, std::size_t)>;

class PoseRouter {
public:
    uint64_t addSubscriber(uint64_t sessionId, PoseSubscriber cb);
    void removeSubscriber(uint64_t streamId);

    // Parse an inbound packed proto::UpstreamFrame and dispatch to all
    // subscribers. Returns true on successful parse.
    bool ingestPackedUpstreamFrame(const uint8_t* data, std::size_t len,
                                   uint64_t sessionId, uint64_t receivedAtNs);

    // Test hook: build a PoseSnapshot envelope from a Cap'n Proto Reader of
    // proto::UpstreamFrame and dispatch.
    void dispatchSnapshot(uint64_t sessionId,
                          uint64_t receivedAtNs,
                          uint64_t questTimestampNs,
                          uint64_t predictedDisplayTimeNs,
                          const float left[7],
                          const float right[7],
                          const float linVel[3],
                          const float angVel[3]);

private:
    struct Entry {
        uint64_t sessionId;
        PoseSubscriber cb;
    };
    std::mutex mu_;
    uint64_t nextStreamId_ = 1;
    std::unordered_map<uint64_t, Entry> subs_;
};

} // namespace fuvr::daemon
