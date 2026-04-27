// SPDX-License-Identifier: Apache-2.0

#include "input_packer.hpp"

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

void test_inactive_hand_zeroed() {
    ActionStateBundle b;
    b.hand = 1;
    b.active = false;
    b.trigger = 0.7f;
    b.thumbstickX = 0.5f;
    auto p = InputPacker::pack(b);
    CHECK(p.hand == 1);
    CHECK(p.trigger == 0.0f);
    CHECK(p.thumbstickX == 0.0f);
    CHECK(p.thumbstickClick == false);
}

void test_active_hand_full_set() {
    ActionStateBundle b;
    b.hand = 0; b.active = true;
    b.trigger = 0.5f; b.triggerTouch = true;
    b.squeeze = 0.25f;
    b.thumbstickX = -0.3f; b.thumbstickY = 0.8f;
    b.thumbstickClick = true; b.thumbstickTouch = true;
    b.buttonAClick = true; b.buttonATouch = true;
    b.buttonBClick = false; b.buttonBTouch = true;
    b.systemClick = true;
    b.thumbrest = 0.9f;

    auto p = InputPacker::pack(b);
    CHECK(p.hand == 0);
    CHECK(std::fabs(p.trigger - 0.5f) < 1e-6);
    CHECK(p.triggerTouch == true);
    CHECK(std::fabs(p.squeeze - 0.25f) < 1e-6);
    CHECK(std::fabs(p.thumbstickX - (-0.3f)) < 1e-6);
    CHECK(std::fabs(p.thumbstickY - 0.8f) < 1e-6);
    CHECK(p.thumbstickClick == true);
    CHECK(p.thumbstickTouch == true);
    CHECK(p.buttonAClick == true);
    CHECK(p.buttonAtouch == true);
    CHECK(p.buttonBClick == false);
    CHECK(p.buttonBtouch == true);
    CHECK(p.systemClick == true);
    CHECK(std::fabs(p.thumbrest - 0.9f) < 1e-6);
}

void test_clamping() {
    ActionStateBundle b;
    b.hand = 1; b.active = true;
    b.trigger = 5.0f;          // out-of-range
    b.thumbstickX = -3.0f;
    b.thumbstickY = std::nanf("");
    b.thumbrest = -1.0f;

    auto p = InputPacker::pack(b);
    CHECK(p.trigger == 1.0f);
    CHECK(p.thumbstickX == -1.0f);
    CHECK(p.thumbstickY == 0.0f);
    CHECK(p.thumbrest == 0.0f);
}

void test_system_click_only_left_hand() {
    ActionStateBundle b;
    b.hand = 1; b.active = true; b.systemClick = true;
    auto p = InputPacker::pack(b);
    CHECK(p.systemClick == false);
}

}  // namespace

int main() {
    test_inactive_hand_zeroed();
    test_active_hand_full_set();
    test_clamping();
    test_system_click_only_left_hand();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_input_packing: OK\n");
    return 0;
}
