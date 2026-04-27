// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fuvr::runtime {

struct Vec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct Quat {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float w{1.0f};
};

struct Pose {
  Vec3 position{};
  Quat orientation{};
};

struct PoseSample {
  uint64_t timestampNs{0};
  Pose leftEye{};
  Pose rightEye{};
  Vec3 linearVelocity{};
  Vec3 angularVelocity{};
};

class PosePredictor {
 public:
  static constexpr std::size_t kCapacity = 32;

  PosePredictor() = default;

  void push(const PoseSample& sample) noexcept;

  std::optional<PoseSample> predict(uint64_t displayTimeNs) const noexcept;

  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  std::optional<PoseSample> latest() const noexcept;

 private:
  std::array<PoseSample, kCapacity> buffer_{};
  std::size_t head_{0};
  std::size_t size_{0};

  const PoseSample& at(std::size_t indexFromOldest) const noexcept;
};

}  // namespace fuvr::runtime
