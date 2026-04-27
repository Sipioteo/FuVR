// SPDX-License-Identifier: Apache-2.0
#include "fuvr/pose_predictor.hpp"

#include <cmath>

namespace fuvr::runtime {

namespace {

Quat normalize(Quat q) noexcept {
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n <= 1e-8f) {
    return Quat{0.0f, 0.0f, 0.0f, 1.0f};
  }
  const float inv = 1.0f / n;
  return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

float dot(const Quat& a, const Quat& b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

Quat slerp(Quat a, Quat b, float t) noexcept {
  float d = dot(a, b);
  if (d < 0.0f) {
    b = Quat{-b.x, -b.y, -b.z, -b.w};
    d = -d;
  }
  if (d > 0.9995f) {
    return normalize(Quat{
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z),
        a.w + t * (b.w - a.w),
    });
  }
  const float theta0 = std::acos(d);
  const float theta = theta0 * t;
  const float sinTheta = std::sin(theta);
  const float sinTheta0 = std::sin(theta0);
  const float s0 = std::cos(theta) - d * sinTheta / sinTheta0;
  const float s1 = sinTheta / sinTheta0;
  return Quat{
      s0 * a.x + s1 * b.x,
      s0 * a.y + s1 * b.y,
      s0 * a.z + s1 * b.z,
      s0 * a.w + s1 * b.w,
  };
}

Pose extrapolatePose(const Pose& base, const Vec3& linVel, const Vec3& angVel,
                     float dt) noexcept {
  Pose out;
  out.position = Vec3{
      base.position.x + linVel.x * dt,
      base.position.y + linVel.y * dt,
      base.position.z + linVel.z * dt,
  };
  const float halfDt = 0.5f * dt;
  Quat omega{angVel.x * halfDt, angVel.y * halfDt, angVel.z * halfDt, 0.0f};
  Quat dq{
      omega.w * base.orientation.x + omega.x * base.orientation.w +
          omega.y * base.orientation.z - omega.z * base.orientation.y,
      omega.w * base.orientation.y - omega.x * base.orientation.z +
          omega.y * base.orientation.w + omega.z * base.orientation.x,
      omega.w * base.orientation.z + omega.x * base.orientation.y -
          omega.y * base.orientation.x + omega.z * base.orientation.w,
      omega.w * base.orientation.w - omega.x * base.orientation.x -
          omega.y * base.orientation.y - omega.z * base.orientation.z,
  };
  out.orientation = normalize(Quat{
      base.orientation.x + dq.x,
      base.orientation.y + dq.y,
      base.orientation.z + dq.z,
      base.orientation.w + dq.w,
  });
  return out;
}

}  // namespace

void PosePredictor::push(const PoseSample& sample) noexcept {
  buffer_[head_] = sample;
  head_ = (head_ + 1) % kCapacity;
  if (size_ < kCapacity) {
    ++size_;
  }
}

const PoseSample& PosePredictor::at(std::size_t indexFromOldest) const noexcept {
  const std::size_t start = (head_ + kCapacity - size_) % kCapacity;
  return buffer_[(start + indexFromOldest) % kCapacity];
}

std::optional<PoseSample> PosePredictor::latest() const noexcept {
  if (size_ == 0) {
    return std::nullopt;
  }
  return at(size_ - 1);
}

std::optional<PoseSample> PosePredictor::predict(
    uint64_t displayTimeNs) const noexcept {
  if (size_ == 0) {
    return std::nullopt;
  }
  const PoseSample& last = at(size_ - 1);
  if (size_ == 1 || displayTimeNs <= last.timestampNs) {
    return last;
  }
  const std::size_t lookback = size_ >= 4 ? 4 : size_;
  const PoseSample& earlier = at(size_ - lookback);
  const double dtBase =
      static_cast<double>(last.timestampNs - earlier.timestampNs) * 1e-9;
  const float dtPredict =
      static_cast<float>(static_cast<double>(displayTimeNs - last.timestampNs) *
                         1e-9);

  Vec3 linVel = last.linearVelocity;
  Vec3 angVel = last.angularVelocity;
  if (dtBase > 1e-6) {
    const float invDt = static_cast<float>(1.0 / dtBase);
    linVel = Vec3{
        (last.leftEye.position.x - earlier.leftEye.position.x) * invDt,
        (last.leftEye.position.y - earlier.leftEye.position.y) * invDt,
        (last.leftEye.position.z - earlier.leftEye.position.z) * invDt,
    };
  }

  PoseSample out = last;
  out.timestampNs = displayTimeNs;
  out.leftEye = extrapolatePose(last.leftEye, linVel, angVel, dtPredict);
  out.rightEye = extrapolatePose(last.rightEye, linVel, angVel, dtPredict);

  if (size_ >= 2) {
    const PoseSample& prev = at(size_ - 2);
    const double dtPrev =
        static_cast<double>(last.timestampNs - prev.timestampNs) * 1e-9;
    if (dtPrev > 1e-6) {
      const float t = static_cast<float>(
          static_cast<double>(displayTimeNs - last.timestampNs) / dtPrev);
      out.leftEye.orientation =
          slerp(prev.leftEye.orientation, last.leftEye.orientation, 1.0f + t);
      out.rightEye.orientation =
          slerp(prev.rightEye.orientation, last.rightEye.orientation, 1.0f + t);
      out.leftEye.orientation = normalize(out.leftEye.orientation);
      out.rightEye.orientation = normalize(out.rightEye.orientation);
    }
  }
  return out;
}

}  // namespace fuvr::runtime
