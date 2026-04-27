// SPDX-License-Identifier: Apache-2.0

#include "hand_encoder.hpp"

#include <cmath>
#include <cstdio>

using namespace fuvr;

namespace {
int g_failures = 0;
#define CHECK(cond) do {                                                       \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

void test_round_trip_synthetic() {
    HandJointSet in;
    for (size_t i = 0; i < kHandFloatsTotal; ++i) {
        in.floats[i] = (float)((int)i - 100) * 0.01f;
    }
    auto wire = HandEncoder::encode(in);
    CHECK(wire.compare(0, 8, "q-hand: ") == 0);
    HandJointSet out;
    CHECK(HandEncoder::decode(wire, out));
    for (size_t i = 0; i < kHandFloatsTotal; ++i) {
        const float diff = std::fabs(out.floats[i] - in.floats[i]);
        // half-precision tolerance: abs of input * 2^-10 + tiny epsilon
        const float tol = std::fabs(in.floats[i]) * (1.0f / 1024.0f) + 1e-3f;
        CHECK(diff <= tol);
    }
}

void test_decode_rejects_bad_prefix() {
    HandJointSet out;
    CHECK(!HandEncoder::decode("nope", out));
    CHECK(!HandEncoder::decode("q-hand:", out));
}

void test_f16_specials() {
    CHECK(HandEncoder::f16_to_f32(HandEncoder::f32_to_f16(0.0f)) == 0.0f);
    CHECK(HandEncoder::f16_to_f32(HandEncoder::f32_to_f16(1.0f)) == 1.0f);
    CHECK(HandEncoder::f16_to_f32(HandEncoder::f32_to_f16(-2.5f)) == -2.5f);
}

}  // namespace

int main() {
    test_round_trip_synthetic();
    test_decode_rejects_bad_prefix();
    test_f16_specials();
    if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
    std::printf("test_hand_encoder: OK\n");
    return 0;
}
