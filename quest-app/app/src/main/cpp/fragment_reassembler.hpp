// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "proto_codec.hpp"

namespace fuvr {

// Reassembles VideoFragmentHeader-described fragments into complete access
// units keyed by frameId. Fragments may arrive out of order; once all
// fragmentCount slices for a frameId are present, take_completed() returns
// the concatenated payload (in fragmentIndex order).
//
// Stale partial frames are evicted when newer frameIds advance more than
// kMaxInflight ahead, to bound memory under packet loss.
class FragmentReassembler {
public:
    struct Completed {
        uint64_t frameId{0};
        uint64_t targetDisplayTimeNs{0};
        bool isKeyframe{false};
        int codec{0};
        PlainViewState renderedLeft{};
        PlainViewState renderedRight{};
        std::vector<uint8_t> payload;
    };

    // Submit one fragment. Returns true if it was accepted (regardless of
    // whether the frame is now complete).
    bool submit(uint64_t frameId,
                uint32_t fragmentIndex,
                uint32_t fragmentCount,
                uint16_t flags,
                int codec,
                uint64_t targetDisplayTimeNs,
                const uint8_t* data,
                size_t size) {
        return submit(frameId, fragmentIndex, fragmentCount, flags, codec,
                      targetDisplayTimeNs, PlainViewState{}, PlainViewState{},
                      data, size);
    }

    bool submit(uint64_t frameId,
                uint32_t fragmentIndex,
                uint32_t fragmentCount,
                uint16_t flags,
                int codec,
                uint64_t targetDisplayTimeNs,
                const PlainViewState& renderedLeft,
                const PlainViewState& renderedRight,
                const uint8_t* data,
                size_t size);

    // Returns the next completed access unit (in arrival-completion order),
    // or empty payload if none ready. Pops it from the internal table.
    Completed take_completed();

    bool has_completed() const { return !ready_.empty(); }

    void reset() { partial_.clear(); ready_.clear(); highest_seen_ = 0; }

private:
    struct Partial {
        uint32_t fragmentCount{0};
        uint16_t flags{0};
        int codec{0};
        uint64_t targetDisplayTimeNs{0};
        PlainViewState renderedLeft{};
        PlainViewState renderedRight{};
        uint32_t received{0};
        std::vector<std::vector<uint8_t>> slices; // sized to fragmentCount
        std::vector<uint8_t> presence; // 0/1 per slice
    };

    static constexpr uint64_t kMaxInflight = 8;

    std::unordered_map<uint64_t, Partial> partial_;
    std::vector<Completed> ready_;
    uint64_t highest_seen_{0};
};

}
