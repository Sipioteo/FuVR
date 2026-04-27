// SPDX-License-Identifier: Apache-2.0
#include "fuvr/path_registry.hpp"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace fr = fuvr::runtime;

TEST(PathRegistry, SeedHasInteractionProfilePaths) {
  fr::PathRegistry r;
  XrPath a = r.intern("/interaction_profiles/oculus/touch_plus_controller");
  XrPath b = r.intern("/interaction_profiles/oculus/touch_plus_controller");
  EXPECT_EQ(a, b);
  auto* s = r.lookup(a);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(*s, "/interaction_profiles/oculus/touch_plus_controller");
}

TEST(PathRegistry, NewStringGetsNewAtom) {
  fr::PathRegistry r;
  XrPath a = r.intern("/user/hand/left");
  XrPath b = r.intern("/user/custom/foo/bar");
  EXPECT_NE(a, b);
  EXPECT_NE(a, 0u);
  EXPECT_NE(b, 0u);
}

TEST(PathRegistry, RoundTrip10kRandomStrings) {
  fr::PathRegistry r;
  std::mt19937 rng(0xc0ffeeu);
  std::unordered_map<std::string, XrPath> assigned;
  std::vector<std::string> keys;
  keys.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    std::string k = "/test/" + std::to_string(rng()) + "/" +
                    std::to_string(rng() & 0xffff);
    keys.push_back(k);
    XrPath atom = r.intern(k);
    EXPECT_NE(atom, 0u);
    auto it = assigned.find(k);
    if (it == assigned.end()) {
      assigned.emplace(k, atom);
    } else {
      EXPECT_EQ(it->second, atom);
    }
  }
  for (const auto& kv : assigned) {
    auto* s = r.lookup(kv.second);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, kv.first);
  }
}

TEST(PathRegistry, LookupOfUnknownAtomReturnsNull) {
  fr::PathRegistry r;
  EXPECT_EQ(r.lookup(0xdeadbeef), nullptr);
  EXPECT_EQ(r.lookup(0), nullptr);
}
