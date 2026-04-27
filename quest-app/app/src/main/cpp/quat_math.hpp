// SPDX-License-Identifier: Apache-2.0
#pragma once

// Tiny header-only quaternion helpers shared between the runtime compositor
// and the host-side ATW math test. Kept dependency-free (raw float arrays /
// a thin Quat POD) so the test target can include this on the Mac without
// pulling in OpenGL, Android, or any glm-style external library.
//
// The compositor used to keep these inline in compositor.cpp; they were
// hoisted here so test_atw_math can exercise the *exact same* quat math the
// runtime uses. Do not duplicate these — always re-include this header.

#include <cmath>

namespace fuvr {

struct Quat { float x{0}, y{0}, z{0}, w{1}; };

inline static Quat quat_normalize(Quat q) {
    float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (n <= 0.0f) return {0,0,0,1};
    float inv = 1.0f / n;
    return { q.x*inv, q.y*inv, q.z*inv, q.w*inv };
}

inline static Quat quat_conjugate(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }

// Hamilton product: a then b -> b * a (apply a first when rotating a vector).
inline static Quat quat_mul(Quat a, Quat b) {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

// Dot product helper used to detect quaternion double-cover sign flips.
// Overload for raw float[4] (compositor sign-fix path) and Quat (predictor).
inline static float qdot(const float a[4], const Quat& b) {
    return a[0]*b.x + a[1]*b.y + a[2]*b.z + a[3]*b.w;
}
inline static float qdot(const Quat& a, const Quat& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}

// Negate all four components in place. q and -q encode the same rotation, so
// this is used to canonicalize sign across consecutive frames before any
// finite-difference / slerp / conjugate-product step.
inline static void qneg(Quat& q) { q.x=-q.x; q.y=-q.y; q.z=-q.z; q.w=-q.w; }

// Convert a unit quaternion to a row-major 3x3 in m[9].
inline static void quat_to_mat3_rowmajor(Quat q, float m[9]) {
    q = quat_normalize(q);
    const float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    const float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    const float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    m[0] = 1 - 2*(yy + zz); m[1] = 2*(xy - wz);     m[2] = 2*(xz + wy);
    m[3] = 2*(xy + wz);     m[4] = 1 - 2*(xx + zz); m[5] = 2*(yz - wx);
    m[6] = 2*(xz - wy);     m[7] = 2*(yz + wx);     m[8] = 1 - 2*(xx + yy);
}

}  // namespace fuvr
