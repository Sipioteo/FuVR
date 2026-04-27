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

struct Fov {
  float angleLeft{0.0f};
  float angleRight{0.0f};
  float angleUp{0.0f};
  float angleDown{0.0f};
};

struct PoseSample {
  uint64_t timestampNs{0};
  Pose leftEye{};
  Pose rightEye{};
  Fov leftFov{};
  Fov rightFov{};
  Vec3 linearVelocity{};
  Vec3 angularVelocity{};

  bool leftControllerActive{false};
  Pose leftController{};
  Vec3 leftControllerLinVel{};
  Vec3 leftControllerAngVel{};

  bool rightControllerActive{false};
  Pose rightController{};
  Vec3 rightControllerLinVel{};
  Vec3 rightControllerAngVel{};
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

  // Average the most recent N samples (orientation via quaternion mean
  // with sign alignment, position/velocity via arithmetic mean). Used by
  // xrLocateViews_impl / xrEndFrame_impl to feed Blender a pose that is
  // free of per-sample IMU jitter — at 1 kHz upstream every Mac frame
  // would otherwise pick a single noisy sample as q_render, making the
  // streamed image visibly wobble even when the head is moving smoothly.
  // N is clamped to [1, size()]. The most recent sample's timestamp,
  // FOV, and controller-active flags are kept verbatim (smoothing those
  // would erase real edge transitions).
  std::optional<PoseSample> smoothedLatest(std::size_t window) const noexcept;

 private:
  std::array<PoseSample, kCapacity> buffer_{};
  std::size_t head_{0};
  std::size_t size_{0};

  const PoseSample& at(std::size_t indexFromOldest) const noexcept;
};

}  // namespace fuvr::runtime
