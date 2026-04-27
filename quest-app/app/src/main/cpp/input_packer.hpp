// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "proto_codec.hpp"

#include <cstdint>

namespace fuvr {

// Pure-logic projection of OpenXR action state into a PlainTouchInputState.
//
// The OpenXR-touching code in pose_forwarder/openxr_session reads action
// state with xrGetActionState{Float,Boolean,Vector2f} into this struct, and
// then InputPacker::pack() flattens it into the wire-bound PlainTouchInputState
// for one hand. Keeping the projection pure makes the host test possible
// without linking OpenXR.
struct ActionStateBundle {
    int hand{0}; // 0=left, 1=right
    bool active{false};

    float trigger{0.0f};       // /input/trigger/value
    bool  triggerTouch{false}; // /input/trigger/touch
    float squeeze{0.0f};       // /input/squeeze/value

    float thumbstickX{0.0f};   // /input/thumbstick/x
    float thumbstickY{0.0f};   // /input/thumbstick/y
    bool  thumbstickClick{false};
    bool  thumbstickTouch{false};

    bool  buttonAClick{false}; // a/x/click
    bool  buttonATouch{false}; // a/x/touch
    bool  buttonBClick{false}; // b/y/click
    bool  buttonBTouch{false}; // b/y/touch
    bool  systemClick{false};  // left only

    float thumbrest{0.0f};     // capacitive thumbrest
};

class InputPacker {
public:
    // Project an ActionStateBundle into the wire struct. Inactive hands
    // produce a zeroed entry with the correct hand id, which the Mac side
    // is required to treat as "no input".
    static PlainTouchInput pack(const ActionStateBundle& s);
};

}
