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

// Exact quaternion exponential of an angular-velocity-times-dt rotation
// vector (axis = ω/|ω|, angle = |ω|·dt). Carmack ("Latency Mitigation
// Strategies") and ALVR both rely on the headset runtime's IMU-Kalman ω
// applied this way. Unlike a finite-difference slerp(prev, last, 1+t), this
// has zero amplification of per-sample sensor jitter and zero catch-up lag at
// motion start: the predicted Δq tracks the IMU velocity directly.
Quat quat_exp_omega_dt(const Vec3& angVel, float dt) noexcept {
  const float wx = angVel.x * dt;
  const float wy = angVel.y * dt;
  const float wz = angVel.z * dt;
  const float halfAngle = 0.5f * std::sqrt(wx * wx + wy * wy + wz * wz);
  if (halfAngle < 1e-6f) {
    // Small-angle approximation: q ≈ (ω·dt/2, 1).
    return Quat{0.5f * wx, 0.5f * wy, 0.5f * wz, 1.0f};
  }
  const float s = std::sin(halfAngle) / (2.0f * halfAngle);
  return Quat{wx * s, wy * s, wz * s, std::cos(halfAngle)};
}

inline Quat quat_mul(const Quat& a, const Quat& b) noexcept {
  return Quat{
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
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
  // World-space rotation: q_new = q_delta * q_base. Order matters — applying
  // the delta on the left rotates the base orientation by the world-frame
  // angular velocity, which is the convention OpenXR's XrSpaceVelocity
  // produces (vel expressed in the reference space, not the body frame).
  const Quat dq = quat_exp_omega_dt(angVel, dt);
  out.orientation = normalize(quat_mul(dq, base.orientation));
  return out;
}

}  // namespace

void PosePredictor::push(const PoseSample& sample) noexcept {
  PoseSample s = sample;
  // Why: the Quest pose forwarder ticks at 1 kHz but the underlying
  // xrLocateViews only refreshes at the headset display rate (~72-90 Hz),
  // so ~9 of every 10 samples we receive are bit-identical duplicates of
  // the previous one. Storing them all turns a "4-sample lookback" into a
  // ~4 ms baseline whose width fluctuates frame-to-frame depending on where
  // a fresh OpenXR update falls in the duplicate run — that fluctuation is
  // exactly what made the rendered image tremble even when the head was
  // stationary or moving smoothly. Drop bit-identical eye samples here so
  // the buffer contains only one entry per fresh OpenXR update; the 4-sample
  // lookback in predict() then stays a stable ~44 ms window at 90 Hz.
  if (size_ > 0) {
    const std::size_t prevIdx = (head_ + kCapacity - 1) % kCapacity;
    const PoseSample& prev = buffer_[prevIdx];
    const auto& pl = prev.leftEye;
    const auto& nl = s.leftEye;
    const auto& pr = prev.rightEye;
    const auto& nr = s.rightEye;
    if (pl.position.x == nl.position.x && pl.position.y == nl.position.y &&
        pl.position.z == nl.position.z && pl.orientation.x == nl.orientation.x &&
        pl.orientation.y == nl.orientation.y &&
        pl.orientation.z == nl.orientation.z &&
        pl.orientation.w == nl.orientation.w &&
        pr.position.x == nr.position.x && pr.position.y == nr.position.y &&
        pr.position.z == nr.position.z && pr.orientation.x == nr.orientation.x &&
        pr.orientation.y == nr.orientation.y &&
        pr.orientation.z == nr.orientation.z &&
        pr.orientation.w == nr.orientation.w) {
      // Update only timestamp+controllers/inputs path is not worth the
      // bookkeeping — controllers move at 90 Hz too. Just skip the push.
      return;
    }
  }
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

std::optional<PoseSample> PosePredictor::smoothedLatest(
    std::size_t window) const noexcept {
  if (size_ == 0) return std::nullopt;
  if (window == 0) window = 1;
  if (window > size_) window = size_;
  if (window == 1) return at(size_ - 1);

  PoseSample out = at(size_ - 1);  // start from latest (keeps ts/fov/etc.)
  // Quaternion mean by sign-aligned arithmetic average + renormalise.
  // Within ~1e-4 of the true Karcher mean for nearby orientations —
  // orders of magnitude below human-perceptible head jitter.
  const Quat refL = out.leftEye.orientation;
  const Quat refR = out.rightEye.orientation;
  float lx = 0, ly = 0, lz = 0, lw = 0;
  float rx = 0, ry = 0, rz = 0, rw = 0;
  Vec3 lp{}, rp{};
  for (std::size_t i = 0; i < window; ++i) {
    const PoseSample& s = at(size_ - 1 - i);
    lp.x += s.leftEye.position.x;
    lp.y += s.leftEye.position.y;
    lp.z += s.leftEye.position.z;
    rp.x += s.rightEye.position.x;
    rp.y += s.rightEye.position.y;
    rp.z += s.rightEye.position.z;
    const Quat& ql = s.leftEye.orientation;
    const float dotL = ql.x*refL.x + ql.y*refL.y + ql.z*refL.z + ql.w*refL.w;
    const float sl = (dotL < 0.0f) ? -1.0f : 1.0f;
    lx += sl * ql.x; ly += sl * ql.y; lz += sl * ql.z; lw += sl * ql.w;
    const Quat& qr = s.rightEye.orientation;
    const float dotR = qr.x*refR.x + qr.y*refR.y + qr.z*refR.z + qr.w*refR.w;
    const float sr = (dotR < 0.0f) ? -1.0f : 1.0f;
    rx += sr * qr.x; ry += sr * qr.y; rz += sr * qr.z; rw += sr * qr.w;
  }
  const float inv = 1.0f / static_cast<float>(window);
  out.leftEye.position  = {lp.x * inv, lp.y * inv, lp.z * inv};
  out.rightEye.position = {rp.x * inv, rp.y * inv, rp.z * inv};
  const float nl = std::sqrt(lx*lx + ly*ly + lz*lz + lw*lw);
  if (nl > 1e-6f) out.leftEye.orientation  = {lx/nl, ly/nl, lz/nl, lw/nl};
  const float nr = std::sqrt(rx*rx + ry*ry + rz*rz + rw*rw);
  if (nr > 1e-6f) out.rightEye.orientation = {rx/nr, ry/nr, rz/nr, rw/nr};
  return out;
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
  // Algorithm (Carmack "Latency Mitigation Strategies" + ALVR client_openxr):
  //
  //   q(t+Δ)  ≈  exp(½·ω·Δ) · q(t)
  //   p(t+Δ)  ≈  p(t) + v·Δ
  //
  // where ω and v are the IMU-derived angular/linear velocities reported by
  // the headset's runtime (chained XrSpaceVelocity on xrLocateSpace). This
  // is the canonical "rotate by ω·Δt" integrator that every production VR
  // streamer uses: ω comes off the IMU's Kalman filter at sub-ms latency
  // and is dramatically cleaner than any finite-difference computed from
  // resampled view poses.
  //
  // Previously we computed a finite-difference slerp(prev, last, 1+t) over a
  // 4-sample (~44 ms) window. That had two failure modes the user reported:
  //
  //   • catch-up lag at motion start: the wide baseline includes pre-motion
  //     samples whose slope is ~0, so the predicted Δq stays small until the
  //     baseline fully fills with moving samples (~44 ms × 2 ≈ 90 ms);
  //   • tremor on micro-motions: the 1+t extrapolation factor (~2.6×)
  //     amplifies any per-sample jitter in `last` directly into the Quest's
  //     q_render, and since q_render lurches frame-to-frame the ATW Δq does
  //     too.
  //
  // Both vanish with the IMU ω path: zero baseline, zero amplification.
  //
  // Fallback: older Quest builds (pre-this-pass) and the daemon-side router
  // that doesn't fill velocity will deliver linVel=angVel=0. In that case we
  // compute a single-sample finite-difference linear velocity (rotational
  // tracking degrades to "hold last orientation" — better than amplifying
  // noise). Once the new Quest APK lands the fallback is dormant.

  const float dtPredict =
      static_cast<float>(static_cast<double>(displayTimeNs - last.timestampNs) *
                         1e-9);

  // Why: cap predicted dt at 60 ms. Pipeline stalls (e.g. a paused
  // GPU on Mac) can push displayTime arbitrarily far into the future; we'd
  // rather render with a slightly stale pose than extrapolate ω for hundreds
  // of ms and fly off into the world. 60 ms ≈ encode + 1 frame slack.
  constexpr float kMaxPredictSec = 0.060f;
  const float dt = dtPredict > kMaxPredictSec ? kMaxPredictSec : dtPredict;

  Vec3 linVel = last.linearVelocity;
  Vec3 angVel = last.angularVelocity;

  // Why: deadband on angular velocity. The IMU Kalman delivers a clean ω,
  // but it's never *exactly* zero — there's always sub-degree-per-second
  // residual noise around true zero. Multiplying that by a 70 ms lookahead
  // amplifies it into a sub-degree warp angle that ATW applies every frame,
  // visible as tremor when the user holds their head still. Real conscious
  // head motion is well above 5°/s, so zero out ω whose magnitude is below
  // a small threshold. The user's "hypersensitive to micromovement" symptom
  // disappears, and motion-onset response is unaffected (ω ramps past the
  // deadband within ~1 sample of any real movement).
  // Linear velocity isn't deadbanded: position drift over 70 ms is below
  // sub-mm even with noisy IMU, and the parallax effect of a wrong position
  // delta at typical interaction distance is below a pixel.
  // Default 0 = no deadband. The Meta IMU Kalman already produces a clean ω;
  // a hard deadband introduces a discontinuity at zero-crossings (e.g. fast
  // back-and-forth head turns) that ATW perceives as accumulated drift.
  // Tunable via FUVR_RT_POSE_ANGVEL_DEADBAND_RAD_S if a specific headset
  // turns out to need filtering.
  static const float kAngVelDeadband = []() {
    if (const char* env = std::getenv("FUVR_RT_POSE_ANGVEL_DEADBAND_RAD_S")) {
      float v = std::strtof(env, nullptr);
      if (v >= 0.0f && v <= 1.0f) return v;
    }
    return 0.0f;
  }();
  {
    const float magSq =
        angVel.x * angVel.x + angVel.y * angVel.y + angVel.z * angVel.z;
    if (magSq < kAngVelDeadband * kAngVelDeadband) {
      angVel = Vec3{0.0f, 0.0f, 0.0f};
    }
  }

  // Detect "no IMU velocity provided" — fall back to one-sample finite
  // difference for linear velocity only. Angular velocity stays zero (we
  // refuse to amplify finite-difference noise into a wild rotation).
  const bool hasImuVel =
      (linVel.x != 0.0f || linVel.y != 0.0f || linVel.z != 0.0f ||
       angVel.x != 0.0f || angVel.y != 0.0f || angVel.z != 0.0f);
  if (!hasImuVel && size_ >= 2) {
    const PoseSample& prev = at(size_ - 2);
    const double dtBase =
        static_cast<double>(last.timestampNs - prev.timestampNs) * 1e-9;
    if (dtBase > 1e-6) {
      const float invDt = static_cast<float>(1.0 / dtBase);
      linVel = Vec3{
          (last.leftEye.position.x - prev.leftEye.position.x) * invDt,
          (last.leftEye.position.y - prev.leftEye.position.y) * invDt,
          (last.leftEye.position.z - prev.leftEye.position.z) * invDt,
      };
    }
  }

  PoseSample out = last;
  out.timestampNs = displayTimeNs;
  out.leftEye = extrapolatePose(last.leftEye, linVel, angVel, dt);
  out.rightEye = extrapolatePose(last.rightEye, linVel, angVel, dt);
  return out;
}

}  // namespace fuvr::runtime
