// SPDX-License-Identifier: Apache-2.0

#include "clock_sync.hpp"

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

void test_pong_preserves_t0_and_orders_timestamps() {
    auto p = ClockSyncResponder::build_pong(/*ping_t0=*/1000,
                                            /*receive_ns=*/2000,
                                            /*send_ns=*/3000);
    CHECK(p.t0 == 1000);
    CHECK(p.t1 == 2000);
    CHECK(p.t2 == 3000);
    CHECK(p.t2 >= p.t1);
}

void test_pong_clamps_send_below_receive() {
    auto p = ClockSyncResponder::build_pong(/*ping_t0=*/42,
                                            /*receive_ns=*/5000,
                                            /*send_ns=*/4000);
    CHECK(p.t0 == 42);
    CHECK(p.t1 == 5000);
    CHECK(p.t2 == 5000);
    CHECK(p.t2 >= p.t1);
}

void test_pong_now_is_monotonic_and_positive() {
    auto p = ClockSyncResponder::build_pong_now(/*ping_t0=*/1000);
    CHECK(p.t0 == 1000);
    CHECK(p.t1 > 0);
    CHECK(p.t2 >= p.t1);
}

void test_now_ns_advances() {
    const uint64_t a = ClockSyncResponder::now_ns();
    uint64_t b = a;
    for (int i = 0; i < 1000 && b == a; ++i) b = ClockSyncResponder::now_ns();
    CHECK(b >= a);
}

}  // namespace

int main() {
    test_pong_preserves_t0_and_orders_timestamps();
    test_pong_clamps_send_below_receive();
    test_pong_now_is_monotonic_and_positive();
    test_now_ns_advances();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_clock_sync: OK\n");
    return 0;
}
