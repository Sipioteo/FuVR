// SPDX-License-Identifier: Apache-2.0

#include "fragment_reassembler.hpp"

#include "proto_codec.hpp"

#include <cstring>

namespace fuvr {

bool FragmentReassembler::submit(uint64_t frameId,
                                 uint32_t fragmentIndex,
                                 uint32_t fragmentCount,
                                 uint16_t flags,
                                 int codec,
                                 uint64_t targetDisplayTimeNs,
                                 const uint8_t* data,
                                 size_t size) {
    if (fragmentCount == 0 || fragmentIndex >= fragmentCount) return false;

    if (frameId > highest_seen_) highest_seen_ = frameId;

    auto it = partial_.find(frameId);
    if (it == partial_.end()) {
        Partial p;
        p.fragmentCount = fragmentCount;
        p.flags = flags;
        p.codec = codec;
        p.targetDisplayTimeNs = targetDisplayTimeNs;
        p.slices.resize(fragmentCount);
        p.presence.assign(fragmentCount, 0);
        it = partial_.emplace(frameId, std::move(p)).first;
    }
    auto& p = it->second;

    if (p.fragmentCount != fragmentCount) return false;
    if (p.presence[fragmentIndex]) return true; // duplicate, ignore quietly
    p.flags |= flags;
    p.slices[fragmentIndex].assign(data, data + size);
    p.presence[fragmentIndex] = 1;
    p.received++;

    if (p.received == p.fragmentCount) {
        Completed done;
        done.frameId = frameId;
        done.targetDisplayTimeNs = p.targetDisplayTimeNs;
        done.isKeyframe = (p.flags & kFlagIdr) != 0;
        done.codec = p.codec;
        size_t total = 0;
        for (auto& s : p.slices) total += s.size();
        done.payload.reserve(total);
        for (auto& s : p.slices) done.payload.insert(done.payload.end(), s.begin(), s.end());
        ready_.push_back(std::move(done));
        partial_.erase(it);
    }

    // Bound memory: drop partial frames more than kMaxInflight behind.
    if (highest_seen_ > kMaxInflight) {
        const uint64_t cutoff = highest_seen_ - kMaxInflight;
        for (auto pit = partial_.begin(); pit != partial_.end();) {
            if (pit->first < cutoff) pit = partial_.erase(pit);
            else ++pit;
        }
    }

    return true;
}

FragmentReassembler::Completed FragmentReassembler::take_completed() {
    if (ready_.empty()) return {};
    Completed c = std::move(ready_.front());
    ready_.erase(ready_.begin());
    return c;
}

}
