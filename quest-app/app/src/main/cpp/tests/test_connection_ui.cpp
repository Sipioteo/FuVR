// SPDX-License-Identifier: Apache-2.0

#include "connection_ui.hpp"

#include <cstdio>
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

bool has_non_background(const std::vector<uint8_t>& b) {
    for (size_t i = 0; i < b.size(); i += 4) {
        if (b[i] != 20 || b[i+1] != 20 || b[i+2] != 20) return true;
    }
    return false;
}

void test_each_state_renders_distinct_pixels() {
    std::vector<uint8_t> a, b;
    CHECK(ConnectionUi::render(ConnectionState::Discovering, a));
    CHECK(ConnectionUi::render(ConnectionState::Connecting, b));
    CHECK(a.size() == (size_t)ConnectionUi::kQuadW * ConnectionUi::kQuadH * 4);
    CHECK(b.size() == a.size());
    CHECK(has_non_background(a));
    CHECK(has_non_background(b));
    CHECK(a != b);
}

void test_state_text_is_stable() {
    CHECK(std::string(state_text(ConnectionState::Discovering)) == "Discovering daemon...");
    CHECK(std::string(state_text(ConnectionState::Connecting)) == "Connecting...");
    CHECK(std::string(state_text(ConnectionState::Negotiating)) == "Negotiating session...");
    CHECK(std::string(state_text(ConnectionState::WaitingForFrames)) == "Waiting for frames...");
    CHECK(std::string(state_text(ConnectionState::Connected)) == "Connected");
}

}  // namespace

int main() {
    test_state_text_is_stable();
    test_each_state_renders_distinct_pixels();
    if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
    std::printf("test_connection_ui: OK\n");
    return 0;
}
