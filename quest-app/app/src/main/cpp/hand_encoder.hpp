// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fuvr {

// XR_EXT_hand_tracking exposes 26 joints per hand (XR_HAND_JOINT_COUNT_EXT).
// We pack (px,py,pz, ox,oy,oz,ow) per joint; both hands → 2 * 26 * 7 = 364
// floats. For wire size, every float is encoded as 16-bit half (IEEE 754
// binary16) and the resulting 728 bytes are base64-encoded behind the
// stable prefix `q-hand: ` on the ControlMessage.error arm — workaround
// pending a wire-schema bump (TODO).
constexpr size_t kHandJointsPerHand = 26;
constexpr size_t kHandFloatsPerJoint = 7;
constexpr size_t kHandFloatsPerHand  = kHandJointsPerHand * kHandFloatsPerJoint;
constexpr size_t kHandFloatsTotal    = 2 * kHandFloatsPerHand;

struct HandJointSet {
    // Concatenated [left[0..25], right[0..25]] with each joint as 7 floats.
    std::array<float, kHandFloatsTotal> floats{};
};

class HandEncoder {
public:
    // Encode a HandJointSet into the control-channel string payload.
    static std::string encode(const HandJointSet& joints);

    // Decode a previously-encoded string back into floats. Returns true on
    // success; only used by the round-trip host test. The Mac-side parser
    // is ALPHA's responsibility (see TODO).
    static bool decode(const std::string& wire, HandJointSet& out);

    // Float ↔ half (binary16) helpers, exposed for test reuse.
    static uint16_t f32_to_f16(float v);
    static float    f16_to_f32(uint16_t h);
};

}
