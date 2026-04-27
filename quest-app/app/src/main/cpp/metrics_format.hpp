// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace fuvr {

struct MetricsSample {
    float fps{0.0f};
    float decode_p95_ms{0.0f};
    float decode_avg_ms{0.0f};
    uint64_t frames_delivered{0};
    uint64_t dropped_frames{0};
    float transport_loss_pct{0.0f};
};

class MetricsFormatter {
public:
    // Returns a single line of the form
    //   q-metrics: fps=NN.N, decode_p95_ms=NN.NN, decode_avg_ms=NN.NN,
    //              frames=NN, dropped_frames=NN, transport_loss_pct=NN.NN
    // The line is well-formed by construction: no NaN, no negative numbers,
    // matches the daemon's parser regex `q-metrics: k=v(?:, k=v)*$`.
    static std::string format(const MetricsSample& s);

    // Returns true if the produced line passes the daemon's parser format
    // expectation. Used by host tests; the format() output must always
    // satisfy this.
    static bool well_formed(const std::string& line);
};

}
