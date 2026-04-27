// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>

#include "fuvr/pose_predictor.hpp"

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::runtime {

class DaemonClient;

struct SubmittedFrame {
  uint64_t frameId{0};
  uint64_t renderStartNs{0};
  uint64_t targetDisplayTimeNs{0};
  void* leftMetalTexture{nullptr};
  void* rightMetalTexture{nullptr};
  uint32_t width{0};
  uint32_t height{0};
  IOSurfaceRef ioSurface{nullptr};
  uint64_t sessionId{0};
  Pose renderedLeft{};
  Pose renderedRight{};
  bool forceIdr{false};
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

class DaemonFrameSink final : public FrameSink {
 public:
  explicit DaemonFrameSink(DaemonClient* client) noexcept : client_(client) {}
  void submit(const SubmittedFrame& frame) noexcept override;

 private:
  DaemonClient* client_{nullptr};
};

std::unique_ptr<FrameSink> makeDefaultFrameSink();
std::unique_ptr<FrameSink> makeDaemonFrameSink(DaemonClient* client);

}  // namespace fuvr::runtime
