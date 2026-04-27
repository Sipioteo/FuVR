// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string_view>

namespace fuvr::daemon {

// Subset of Quest-side metrics piggy-backed on `ControlMessage.error` with a
// "q-metrics: " prefix. Format: `q-metrics: key=value, key=value`.
// Known keys: `fps`, `decode_p95_ms`. Unknown keys are ignored.
struct QMetrics {
    float decoderFps         = 0.0f;
    float decoderDecodeMsP95 = 0.0f;
    bool  hasFps             = false;
    bool  hasDecodeP95       = false;
};

// Returns parsed metrics if `line` starts with the `q-metrics: ` prefix and
// contains at least one well-formed `key=number` pair. Trailing whitespace
// and unknown keys are tolerated. Returns nullopt on prefix mismatch or
// when no recognized key parsed cleanly.
std::optional<QMetrics> parseQMetrics(std::string_view line);

} // namespace fuvr::daemon
