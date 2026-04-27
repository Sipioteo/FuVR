// SPDX-License-Identifier: Apache-2.0

#include "metrics_format.hpp"

#include <cmath>
#include <cstdio>

namespace fuvr {

namespace {
float sanitize_f(float v) {
    if (std::isnan(v) || std::isinf(v) || v < 0.0f) return 0.0f;
    return v;
}
float sanitize_pct(float v) {
    if (std::isnan(v) || std::isinf(v) || v < 0.0f) return 0.0f;
    if (v > 100.0f) return 100.0f;
    return v;
}
}

std::string MetricsFormatter::format(const MetricsSample& s) {
    char buf[256];
    int n = std::snprintf(buf, sizeof(buf),
        "q-metrics: fps=%.1f, decode_p95_ms=%.2f, decode_avg_ms=%.2f, "
        "frames=%llu, dropped_frames=%llu, transport_loss_pct=%.2f",
        (double)sanitize_f(s.fps),
        (double)sanitize_f(s.decode_p95_ms),
        (double)sanitize_f(s.decode_avg_ms),
        (unsigned long long)s.frames_delivered,
        (unsigned long long)s.dropped_frames,
        (double)sanitize_pct(s.transport_loss_pct));
    if (n <= 0) return {};
    return std::string(buf, (size_t)n);
}

bool MetricsFormatter::well_formed(const std::string& line) {
    // q-metrics: k=v(, k=v)*
    static constexpr const char* kPrefix = "q-metrics: ";
    const size_t pl = std::char_traits<char>::length(kPrefix);
    if (line.size() <= pl) return false;
    if (line.compare(0, pl, kPrefix) != 0) return false;
    // Sanity rejects: no NaN/Inf, no negative numeric values. We only scan
    // the value region (after the prefix) so the literal "-" inside the
    // "q-metrics:" key itself does not trigger.
    const std::string body = line.substr(pl);
    if (body.find("nan") != std::string::npos) return false;
    if (body.find("NaN") != std::string::npos) return false;
    if (body.find("inf") != std::string::npos) return false;
    if (body.find('-') != std::string::npos) return false;

    size_t i = pl;
    bool first = true;
    while (i < line.size()) {
        if (!first) {
            if (line.compare(i, 2, ", ") != 0) return false;
            i += 2;
        }
        first = false;
        // key: [a-zA-Z_][a-zA-Z0-9_]*
        size_t kstart = i;
        while (i < line.size()) {
            char c = line[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
            if (!ok) break;
            ++i;
        }
        if (i == kstart) return false;
        if (i >= line.size() || line[i] != '=') return false;
        ++i;
        size_t vstart = i;
        while (i < line.size()) {
            char c = line[i];
            if (c == ',') break;
            ++i;
        }
        if (i == vstart) return false;
    }
    return !first;
}

}
