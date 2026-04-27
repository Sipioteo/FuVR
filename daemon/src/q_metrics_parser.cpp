// SPDX-License-Identifier: Apache-2.0
#include "fuvr/q_metrics_parser.hpp"

#include <cctype>
#include <charconv>
#include <string>

namespace fuvr::daemon {

namespace {
constexpr std::string_view kPrefix = "q-metrics:";

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

bool parseFloat(std::string_view s, float& out) {
    s = trim(s);
    if (s.empty()) return false;
    // Why: std::from_chars for float is available in libc++ on macOS 13.4+.
    // Fallback through std::string + strtof avoids portability surprises.
    std::string buf(s);
    const char* c = buf.c_str();
    char* end = nullptr;
    float v = std::strtof(c, &end);
    if (end == c) return false;
    out = v;
    return true;
}
} // namespace

std::optional<QMetrics> parseQMetrics(std::string_view line) {
    line = trim(line);
    if (line.size() < kPrefix.size()) return std::nullopt;
    if (line.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
    line.remove_prefix(kPrefix.size());

    QMetrics m;
    bool any = false;
    while (!line.empty()) {
        auto comma = line.find(',');
        std::string_view tok = (comma == std::string_view::npos)
                                   ? line : line.substr(0, comma);
        line = (comma == std::string_view::npos)
                   ? std::string_view{} : line.substr(comma + 1);
        tok = trim(tok);
        if (tok.empty()) continue;
        auto eq = tok.find('=');
        if (eq == std::string_view::npos) continue;
        auto key = trim(tok.substr(0, eq));
        auto val = trim(tok.substr(eq + 1));
        float fv = 0.0f;
        if (key == "fps") {
            if (parseFloat(val, fv)) { m.decoderFps = fv; m.hasFps = true; any = true; }
        } else if (key == "decode_p95_ms") {
            if (parseFloat(val, fv)) { m.decoderDecodeMsP95 = fv; m.hasDecodeP95 = true; any = true; }
        }
        // Unknown keys: ignore.
    }
    if (!any) return std::nullopt;
    return m;
}

} // namespace fuvr::daemon
