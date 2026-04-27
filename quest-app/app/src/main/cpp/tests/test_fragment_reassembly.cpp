// SPDX-License-Identifier: Apache-2.0

#include "fragment_reassembler.hpp"
#include "proto_codec.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace fuvr;

namespace {

int g_failures = 0;

#define CHECK(cond) do {                                                       \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

std::vector<uint8_t> make_payload(uint8_t seed, size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (uint8_t)(seed + i);
    return v;
}

void test_single_fragment_completes_immediately() {
    FragmentReassembler r;
    auto p = make_payload(0x10, 8);
    r.submit(/*frameId*/ 1, /*idx*/ 0, /*count*/ 1, kFlagIdr | kFlagEndOfFrame,
             /*codec*/ 0, /*tdt*/ 1234, p.data(), p.size());
    CHECK(r.has_completed());
    auto au = r.take_completed();
    CHECK(au.frameId == 1);
    CHECK(au.isKeyframe == true);
    CHECK(au.payload == p);
    CHECK(!r.has_completed());
}

void test_multi_fragment_in_order() {
    FragmentReassembler r;
    auto a = make_payload(0x20, 4);
    auto b = make_payload(0x30, 5);
    auto c = make_payload(0x40, 6);
    r.submit(7, 0, 3, 0, 0, 100, a.data(), a.size());
    CHECK(!r.has_completed());
    r.submit(7, 1, 3, 0, 0, 100, b.data(), b.size());
    CHECK(!r.has_completed());
    r.submit(7, 2, 3, kFlagEndOfFrame, 0, 100, c.data(), c.size());
    CHECK(r.has_completed());
    auto au = r.take_completed();
    CHECK(au.frameId == 7);
    CHECK(au.isKeyframe == false);
    CHECK(au.payload.size() == a.size() + b.size() + c.size());
    CHECK(std::memcmp(au.payload.data(),                a.data(), a.size()) == 0);
    CHECK(std::memcmp(au.payload.data() + a.size(),     b.data(), b.size()) == 0);
    CHECK(std::memcmp(au.payload.data() + a.size() + b.size(), c.data(), c.size()) == 0);
}

void test_multi_fragment_out_of_order() {
    FragmentReassembler r;
    auto a = make_payload(0x50, 3);
    auto b = make_payload(0x60, 3);
    auto c = make_payload(0x70, 3);
    r.submit(9, 2, 3, 0, 0, 0, c.data(), c.size());
    r.submit(9, 0, 3, kFlagIdr, 0, 0, a.data(), a.size());
    CHECK(!r.has_completed());
    r.submit(9, 1, 3, 0, 0, 0, b.data(), b.size());
    CHECK(r.has_completed());
    auto au = r.take_completed();
    CHECK(au.isKeyframe == true);
    CHECK(std::memcmp(au.payload.data(),     a.data(), 3) == 0);
    CHECK(std::memcmp(au.payload.data() + 3, b.data(), 3) == 0);
    CHECK(std::memcmp(au.payload.data() + 6, c.data(), 3) == 0);
}

void test_duplicate_fragments_are_idempotent() {
    FragmentReassembler r;
    auto a = make_payload(0x80, 2);
    auto b = make_payload(0x90, 2);
    r.submit(3, 0, 2, 0, 0, 0, a.data(), a.size());
    r.submit(3, 0, 2, 0, 0, 0, a.data(), a.size()); // duplicate
    CHECK(!r.has_completed());
    r.submit(3, 1, 2, 0, 0, 0, b.data(), b.size());
    CHECK(r.has_completed());
    auto au = r.take_completed();
    CHECK(au.payload.size() == 4);
}

void test_interleaved_frames() {
    FragmentReassembler r;
    auto p10 = make_payload(0x01, 2);
    auto p11 = make_payload(0x02, 2);
    auto p20 = make_payload(0x03, 2);
    auto p21 = make_payload(0x04, 2);
    r.submit(10, 0, 2, 0, 0, 0, p10.data(), p10.size());
    r.submit(20, 1, 2, 0, 0, 0, p21.data(), p21.size());
    r.submit(10, 1, 2, 0, 0, 0, p11.data(), p11.size());
    CHECK(r.has_completed());
    auto au10 = r.take_completed();
    CHECK(au10.frameId == 10);
    r.submit(20, 0, 2, 0, 0, 0, p20.data(), p20.size());
    CHECK(r.has_completed());
    auto au20 = r.take_completed();
    CHECK(au20.frameId == 20);
}

void test_stale_partial_eviction() {
    FragmentReassembler r;
    auto p = make_payload(0xAA, 1);
    // Frame 1 starts but never finishes.
    r.submit(1, 0, 2, 0, 0, 0, p.data(), p.size());
    // Submit many fragments far ahead in frameId; the inflight bound (8)
    // should evict frame 1's partial entry.
    for (uint64_t f = 100; f < 110; ++f) {
        r.submit(f, 0, 1, kFlagEndOfFrame, 0, 0, p.data(), p.size());
        if (r.has_completed()) (void)r.take_completed();
    }
    // Now belatedly send frame 1's second fragment. It should NOT complete
    // a frame because the partial was evicted (it gets re-created as a new
    // partial with only 1 of 2 fragments present).
    auto p2 = make_payload(0xBB, 1);
    r.submit(1, 1, 2, 0, 0, 0, p2.data(), p2.size());
    CHECK(!r.has_completed());
}

}  // namespace

int main() {
    test_single_fragment_completes_immediately();
    test_multi_fragment_in_order();
    test_multi_fragment_out_of_order();
    test_duplicate_fragments_are_idempotent();
    test_interleaved_frames();
    test_stale_partial_eviction();

    if (g_failures == 0) {
        std::printf("OK: all fragment reassembly tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "FAILED: %d assertions\n", g_failures);
    return 1;
}
