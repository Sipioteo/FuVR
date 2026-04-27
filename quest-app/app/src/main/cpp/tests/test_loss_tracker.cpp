// SPDX-License-Identifier: Apache-2.0

#include "loss_tracker.hpp"

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

void test_no_request_below_threshold() {
    LossTracker t(5);
    uint64_t now = 1'000'000'000ULL;
    for (int i = 0; i < 5; ++i) { t.note_loss(now); now += 1'000'000ULL; }
    auto r = t.poll_bitrate_request(now);
    CHECK(!r.has_value());
}

void test_one_bitrate_req_per_second_under_sustained_loss() {
    LossTracker t(5);
    uint64_t now = 1'000'000'000ULL;
    int emits = 0;
    for (int second = 0; second < 5; ++second) {
        for (int i = 0; i < 50; ++i) {
            t.note_loss(now);
            if (auto r = t.poll_bitrate_request(now)) {
                ++emits;
                CHECK(r->find("bitrate-req: kbps=") == 0);
            }
            now += 20'000'000ULL; // 20 ms steps -> 50 events per second
        }
    }
    // 5 seconds of sustained loss should produce ~5 emits, never more.
    CHECK(emits >= 4);
    CHECK(emits <= 5);
}

void test_keyframe_req_on_decode_failure() {
    LossTracker t;
    uint64_t now = 1'000'000'000ULL;
    t.note_decode_failure(now);
    auto r = t.poll_keyframe_request(now);
    CHECK(r.has_value());
    CHECK(r && *r == "keyframe-req: now");
    auto r2 = t.poll_keyframe_request(now);
    CHECK(!r2.has_value());
}

void test_loss_window_pruning() {
    LossTracker t(5);
    uint64_t now = 1'000'000'000ULL;
    for (int i = 0; i < 10; ++i) { t.note_loss(now); now += 50'000'000ULL; }
    // Advance 3 s past last loss; everything pruned.
    now += 3'000'000'000ULL;
    CHECK(t.loss_events_in_window(now) == 0);
    CHECK(!t.poll_bitrate_request(now).has_value());
}

}  // namespace

int main() {
    test_no_request_below_threshold();
    test_one_bitrate_req_per_second_under_sustained_loss();
    test_keyframe_req_on_decode_failure();
    test_loss_window_pruning();
    if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
    std::printf("test_loss_tracker: OK\n");
    return 0;
}
