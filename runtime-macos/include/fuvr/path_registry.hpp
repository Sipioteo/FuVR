// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fuvr::runtime {

// Bidirectional atom registry for OpenXR path strings. The hot interaction
// profile paths are pre-seeded so they hash to fixed atoms without grow
// contention.
class PathRegistry {
 public:
  PathRegistry();

  // Returns a stable atom for `str`. Same string -> same atom across calls.
  XrPath intern(std::string_view str) noexcept;

  // Look up the string for `path`. Returns nullptr if unknown.
  const std::string* lookup(XrPath path) const noexcept;

  // Number of interned strings (for tests).
  std::size_t size() const noexcept;

 private:
  void seed(std::string_view s);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, XrPath> stringToAtom_;
  std::vector<std::string> atomToString_;  // index = atom-1
};

PathRegistry& pathRegistry() noexcept;

}  // namespace fuvr::runtime
