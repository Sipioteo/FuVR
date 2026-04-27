// SPDX-License-Identifier: Apache-2.0
#include "fuvr/pose_predictor.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

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
  PoseSample s = sample;
  // Quaternion double-cover: q and -q represent the same rotation, but the
  // Quest may emit consecutive samples with opposite signs (OpenXR makes no
  // continuity guarantee). Without canonicalization, finite-difference and
  // slerp/lerp on adjacent samples treat a near-zero rotation as ~360°,
  // producing a transient view spin until a fresh sample lands. Flip any
  // incoming quaternion that is antipodal to the previous sample so the
  // history is monotonic in quat space.
  if (size_ > 0) {
    const std::size_t prevIdx = (head_ + kCapacity - 1) % kCapacity;
    const PoseSample& prev = buffer_[prevIdx];
    auto fixSign = [](const Quat& ref, Quat& q) {
      if (dot(ref, q) < 0.0f) {
        q = Quat{-q.x, -q.y, -q.z, -q.w};
      }
    };
    fixSign(prev.leftEye.orientation, s.leftEye.orientation);
    fixSign(prev.rightEye.orientation, s.rightEye.orientation);
    if (s.leftControllerActive && prev.leftControllerActive) {
      fixSign(prev.leftController.orientation, s.leftController.orientation);
    }
    if (s.rightControllerActive && prev.rightControllerActive) {
      fixSign(prev.rightController.orientation, s.rightController.orientation);
    }
  }
  // [QUAT-DEBUG] 1 Hz log of dot(q_n, q_n-1). Post-canonicalization this
  // should always be >= 0; a print of a near-zero or negative value here
  // would indicate a bug in the sign-fix above or a violently large rotation
  // between samples.
  if (size_ > 0) {
    static std::atomic<uint64_t> last_log_ns{0};
    const uint64_t now_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    uint64_t prev_log = last_log_ns.load(std::memory_order_relaxed);
    if (now_ns - prev_log > 1'000'000'000ull &&
        last_log_ns.compare_exchange_strong(prev_log, now_ns,
                                            std::memory_order_relaxed)) {
      const std::size_t prevIdx = (head_ + kCapacity - 1) % kCapacity;
      const float dl = dot(buffer_[prevIdx].leftEye.orientation,
                           s.leftEye.orientation);
      const float dr = dot(buffer_[prevIdx].rightEye.orientation,
                           s.rightEye.orientation);
      std::fprintf(stderr,
                   "[QUAT-DEBUG] predictor.push dot(q_n,q_n-1) L=%.4f R=%.4f "
                   "size=%zu\n",
                   dl, dr, size_);
    }
  }

  buffer_[head_] = s;
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
  // Why: the Quest pose forwarder ticks at 1 kHz but the underlying
  // xrLocateViews only refreshes at the headset display rate (~72-90 Hz),
  // so ~9 of every 10 samples we receive are bit-identical duplicates.
  // A fixed N-sample lookback (e.g. 4 samples = ~4 ms) frequently lands
  // entirely inside one duplicate run, producing either zero velocity or
  // a sudden jump when the run changes. Walk back until we cover at least
  // ~20 ms of wall time, which guarantees we span ≥1 fresh OpenXR update
  // and the velocity estimate averages over real motion, not jitter.
  std::size_t lookback = size_ >= 2 ? 2 : 1;
  for (std::size_t k = 2; k <= size_; ++k) {
    const PoseSample& cand = at(size_ - k);
    const double dt =
        static_cast<double>(last.timestampNs - cand.timestampNs) * 1e-9;
    lookback = k;
    if (dt >= 0.020) break;
  }
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
    // Why: use the same wide baseline as linear velocity (4 samples back at
    // 1 kHz ≈ 3 ms) instead of the immediate previous sample (~1 ms). With a
    // 70 ms lookahead, a 1 ms baseline produces an extrapolation factor of
    // ~70× — any sub-millimeter sensor jitter in `last` blows up into a huge
    // angular delta and the Quest's ATW snaps when q_render lurches frame
    // to frame. A 3 ms baseline cuts the amplification to ~23× and the cap
    // below clamps any remaining outliers to a reasonable max step.
    const PoseSample& prev = (size_ >= 4) ? at(size_ - 4) : at(size_ - 2);
    const double dtPrev =
        static_cast<double>(last.timestampNs - prev.timestampNs) * 1e-9;
    if (dtPrev > 1e-6) {
      float t = static_cast<float>(
          static_cast<double>(displayTimeNs - last.timestampNs) / dtPrev);
      // Cap extrapolation factor: at most 8× the baseline. Beyond that we
      // hold the most-recent rotation rather than predicting through noise.
      if (t > 8.0f) t = 8.0f;
      out.leftEye.orientation =
          slerp(prev.leftEye.orientation, last.leftEye.orientation, 1.0f + t);
      out.rightEye.orientation =
          slerp(prev.rightEye.orientation, last.rightEye.orientation, 1.0f + t);
      out.leftEye.orientation = normalize(out.leftEye.orientation);
      out.rightEye.orientation = normalize(out.rightEye.orientation);
      // Slerp with t > 1 (extrapolation) can return a quat antipodal to
      // `last` even though `last` is canonical w.r.t. the buffer. Pin the
      // predicted output onto the same sign sheet as `last` so the wire
      // q_render the Quest receives is monotonic across frames.
      if (dot(last.leftEye.orientation, out.leftEye.orientation) < 0.0f) {
        out.leftEye.orientation = Quat{-out.leftEye.orientation.x,
                                       -out.leftEye.orientation.y,
                                       -out.leftEye.orientation.z,
                                       -out.leftEye.orientation.w};
      }
      if (dot(last.rightEye.orientation, out.rightEye.orientation) < 0.0f) {
        out.rightEye.orientation = Quat{-out.rightEye.orientation.x,
                                        -out.rightEye.orientation.y,
                                        -out.rightEye.orientation.z,
                                        -out.rightEye.orientation.w};
      }
    }
  }
  return out;
}

}  // namespace fuvr::runtime
