// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fuvr/q_metrics_parser.hpp"

using fuvr::daemon::parseQMetrics;

TEST(QMetricsParser, WellFormedLine) {
    auto m = parseQMetrics("q-metrics: fps=89.7, decode_p95_ms=4.2, frames=1234");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->hasFps);
    EXPECT_TRUE(m->hasDecodeP95);
    EXPECT_FLOAT_EQ(m->decoderFps, 89.7f);
    EXPECT_FLOAT_EQ(m->decoderDecodeMsP95, 4.2f);
}

TEST(QMetricsParser, TrailingWhitespaceTolerated) {
    auto m = parseQMetrics("  q-metrics: fps=72   ,  decode_p95_ms = 5.0   ");
    ASSERT_TRUE(m.has_value());
    EXPECT_FLOAT_EQ(m->decoderFps, 72.0f);
    EXPECT_FLOAT_EQ(m->decoderDecodeMsP95, 5.0f);
}

TEST(QMetricsParser, UnknownKeysIgnored) {
    auto m = parseQMetrics("q-metrics: foo=1, fps=60, bar=baz");
    ASSERT_TRUE(m.has_value());
    EXPECT_FLOAT_EQ(m->decoderFps, 60.0f);
    EXPECT_FALSE(m->hasDecodeP95);
}

TEST(QMetricsParser, MalformedDropped) {
    EXPECT_FALSE(parseQMetrics("not-q-metrics: fps=60").has_value());
    EXPECT_FALSE(parseQMetrics("q-metrics: nokeyvalue").has_value());
    EXPECT_FALSE(parseQMetrics("").has_value());
}

TEST(QMetricsParser, MalformedValueSkippedButOthersKept) {
    auto m = parseQMetrics("q-metrics: fps=abc, decode_p95_ms=3.5");
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->hasFps);
    EXPECT_TRUE(m->hasDecodeP95);
    EXPECT_FLOAT_EQ(m->decoderDecodeMsP95, 3.5f);
}
