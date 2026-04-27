// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <openxr/openxr.h>

#include "fuvr/runtime.hpp"

namespace fuvr::runtime::diag {

// Test/diagnostic back-door. Returns a copy so callers can read the rolling
// encoder window without locking internal mutexes.
EncoderStatsSnapshot encoderStatsForSession(XrSession session) noexcept;

}  // namespace fuvr::runtime::diag
