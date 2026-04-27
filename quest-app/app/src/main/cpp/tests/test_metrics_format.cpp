// SPDX-License-Identifier: Apache-2.0

#include "metrics_format.hpp"

#include <cmath>
#include <cstdio>
#include <random>

using namespace fuvr;

namespace {
int g_failures = 0;
#define CHECK(cond) do {                                                       \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

void test_basic_well_formed() {
    MetricsSample s;
    s.fps = 90.0f; s.decode_p95_ms = 7.5f; s.decode_avg_ms = 5.1f;
    s.frames_delivered = 12345; s.dropped_frames = 3; s.transport_loss_pct = 0.5f;
    auto line = MetricsFormatter::format(s);
    CHECK(MetricsFormatter::well_formed(line));
    CHECK(line.find("fps=90.0") != std::string::npos);
    CHECK(line.find("decode_p95_ms=7.50") != std::string::npos);
    CHECK(line.find("decode_avg_ms=5.10") != std::string::npos);
    CHECK(line.find("frames=12345") != std::string::npos);
    CHECK(line.find("dropped_frames=3") != std::string::npos);
    CHECK(line.find("transport_loss_pct=0.50") != std::string::npos);
}

void test_nan_and_negative_sanitized() {
    MetricsSample s;
    s.fps = std::nanf("");
    s.decode_p95_ms = -1.0f;
    s.decode_avg_ms = std::numeric_limits<float>::infinity();
    s.transport_loss_pct = -50.0f;
    auto line = MetricsFormatter::format(s);
    CHECK(MetricsFormatter::well_formed(line));
    CHECK(line.find("nan") == std::string::npos);
    CHECK(line.find("inf") == std::string::npos);
    // The "-" inside "q-metrics:" prefix is fine; we only forbid negatives
    // among the values, which the body-only well_formed check enforces.
    CHECK(line.find("=-") == std::string::npos);
}

void test_loss_pct_capped() {
    MetricsSample s;
    s.transport_loss_pct = 200.0f;
    auto line = MetricsFormatter::format(s);
    CHECK(line.find("transport_loss_pct=100.00") != std::string::npos);
}

void test_100_synthetic_samples_all_well_formed() {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> uf(-100.0f, 200.0f);
    std::uniform_int_distribution<uint64_t> ui(0, 1000000);
    for (int i = 0; i < 100; ++i) {
        MetricsSample s;
        s.fps = uf(rng);
        s.decode_p95_ms = uf(rng);
        s.decode_avg_ms = uf(rng);
        s.frames_delivered = ui(rng);
        s.dropped_frames = ui(rng);
        s.transport_loss_pct = uf(rng);
        if (i % 7 == 0) s.fps = std::nanf("");
        if (i % 11 == 0) s.decode_p95_ms = std::numeric_limits<float>::infinity();
        auto line = MetricsFormatter::format(s);
        if (!MetricsFormatter::well_formed(line)) {
            std::fprintf(stderr, "iter %d malformed: '%s'\n", i, line.c_str());
            ++g_failures;
            return;
        }
    }
}

}  // namespace

int main() {
    test_basic_well_formed();
    test_nan_and_negative_sanitized();
    test_loss_pct_capped();
    test_100_synthetic_samples_all_well_formed();
    if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
    std::printf("test_metrics_format: OK\n");
    return 0;
}
