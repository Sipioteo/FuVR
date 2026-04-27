// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace fuvr::daemon {

using InputSubscriber = std::function<void(const uint8_t*, std::size_t)>;

// Parses TouchInputState entries out of `proto::UpstreamFrame.inputs`,
// converts to daemon `InputSnapshot`, and fans out one envelope per
// subscriber per inbound frame.
class InputRouter {
public:
    uint64_t addSubscriber(uint64_t sessionId, InputSubscriber cb);
    void removeSubscriber(uint64_t streamId);
    void removeSubscribersForSessions(const std::vector<uint64_t>& sessionIds);

    bool ingestPackedUpstreamFrame(const uint8_t* data, std::size_t len,
                                   uint64_t sessionId, uint64_t receivedAtNs);

private:
    struct Entry {
        uint64_t sessionId;
        InputSubscriber cb;
    };
    std::mutex mu_;
    uint64_t nextStreamId_ = 1;
    std::unordered_map<uint64_t, Entry> subs_;
};

} // namespace fuvr::daemon
