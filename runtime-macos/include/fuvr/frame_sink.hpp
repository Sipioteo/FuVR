// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>

namespace fuvr::runtime {

struct SubmittedFrame {
  uint64_t frameId{0};
  uint64_t renderStartNs{0};
  uint64_t targetDisplayTimeNs{0};
  void* leftMetalTexture{nullptr};
  void* rightMetalTexture{nullptr};
  uint32_t width{0};
  uint32_t height{0};
};

class FrameSink {
 public:
  virtual ~FrameSink() = default;
  virtual void submit(const SubmittedFrame& frame) noexcept = 0;
};

class NullFrameSink final : public FrameSink {
 public:
  void submit(const SubmittedFrame&) noexcept override {}
};

std::unique_ptr<FrameSink> makeDefaultFrameSink();

}  // namespace fuvr::runtime
