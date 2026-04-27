// SPDX-License-Identifier: Apache-2.0

#include "input_packer.hpp"

#include <algorithm>
#include <cmath>

namespace fuvr {

namespace {
float clamp01(float v) {
    if (std::isnan(v) || std::isinf(v)) return 0.0f;
    return std::max(0.0f, std::min(1.0f, v));
}
float clamp_axis(float v) {
    if (std::isnan(v) || std::isinf(v)) return 0.0f;
    return std::max(-1.0f, std::min(1.0f, v));
}
}

PlainTouchInput InputPacker::pack(const ActionStateBundle& s) {
    PlainTouchInput o;
    o.hand = (s.hand == 0) ? 0 : 1;
    if (!s.active) return o;

    o.trigger          = clamp01(s.trigger);
    o.triggerTouch     = s.triggerTouch;
    o.squeeze          = clamp01(s.squeeze);
    o.thumbstickX      = clamp_axis(s.thumbstickX);
    o.thumbstickY      = clamp_axis(s.thumbstickY);
    o.thumbstickClick  = s.thumbstickClick;
    o.thumbstickTouch  = s.thumbstickTouch;
    o.buttonAClick     = s.buttonAClick;
    o.buttonAtouch     = s.buttonATouch;
    o.buttonBClick     = s.buttonBClick;
    o.buttonBtouch     = s.buttonBTouch;
    o.systemClick      = (s.hand == 0) ? s.systemClick : false;
    o.thumbrest        = clamp01(s.thumbrest);
    return o;
}

}
