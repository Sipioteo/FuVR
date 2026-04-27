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

using PoseSubscriber = std::function<void(const uint8_t*, std::size_t)>;

// Per-controller sample fed into the snapshot dispatch path. `active=false`
// means no tracking signal — the daemon still forwards zeroed pose bits so
// the runtime can answer xrLocateSpace with INVALID location flags.
struct ControllerSampleIn {
    bool  active = false;
    float pos[3]      = {0, 0, 0};
    float rot[4]      = {0, 0, 0, 1};
    float linVel[3]   = {0, 0, 0};
    float angVel[3]   = {0, 0, 0};
};

// Per-eye FOV from xrLocateViews on the Quest. {0,0,0,0} means the upstream
// frame didn't include FOV (older Quest builds) — runtime falls back to a
// hardcoded approximation in that case.
struct FovIn {
    float angleLeft  = 0.0f;
    float angleRight = 0.0f;
    float angleUp    = 0.0f;
    float angleDown  = 0.0f;
};

class PoseRouter {
public:
    uint64_t addSubscriber(uint64_t sessionId, PoseSubscriber cb);
    void removeSubscriber(uint64_t streamId);
    // Drop every subscriber whose sessionId is in the provided set.
    // Used by daemon to GC subscribers when their owning session is destroyed.
    void removeSubscribersForSessions(const std::vector<uint64_t>& sessionIds);

    bool ingestPackedUpstreamFrame(const uint8_t* data, std::size_t len,
                                   uint64_t sessionId, uint64_t receivedAtNs);

    void dispatchSnapshot(uint64_t sessionId,
                          uint64_t receivedAtNs,
                          uint64_t questTimestampNs,
                          uint64_t predictedDisplayTimeNs,
                          const float left[7],
                          const float right[7],
                          const float linVel[3],
                          const float angVel[3],
                          const ControllerSampleIn& leftCtrl,
                          const ControllerSampleIn& rightCtrl,
                          const FovIn& leftFov = {},
                          const FovIn& rightFov = {});

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
