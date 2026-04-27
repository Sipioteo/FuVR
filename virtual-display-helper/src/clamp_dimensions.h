// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace fuvr::vdisplay {

// Why: M4/M5 DCP firmware enforces a 6720-pixel pipe-0 limit (SPEC §3.1.1).
inline constexpr uint32_t kPipe0PixelLimit = 6720;

struct Mode {
  uint32_t width;
  uint32_t height;
  double   hz;
};

// Returns one or two modes; emits a split when (w*h) exceeds the M4/M5 ceiling.
inline std::vector<Mode> clamp_dimensions(uint32_t w, uint32_t h, double hz, bool* split_out = nullptr) {
  std::vector<Mode> out;
  const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
  const uint64_t limit  = static_cast<uint64_t>(kPipe0PixelLimit) * static_cast<uint64_t>(kPipe0PixelLimit);
  const bool over = pixels > limit || w > kPipe0PixelLimit;
  if (split_out) *split_out = over;
  if (over) {
    const uint32_t half_w = w / 2;
    out.push_back({half_w, h, hz});
    out.push_back({w - half_w, h, hz});
  } else {
    out.push_back({w, h, hz});
  }
  return out;
}

}  // namespace fuvr::vdisplay
