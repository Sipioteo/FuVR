// SPDX-License-Identifier: Apache-2.0
#include "pose_math.hpp"

#include <cmath>

namespace fuvr::openvr_shim::posemath {

const vr::HmdMatrix34_t kIdentity34 = {{
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
}};

// pose13 = { px, py, pz, qx, qy, qz, qw, vx, vy, vz, wx, wy, wz } — only the
// first 7 elements participate in the orientation/position matrix.
vr::HmdMatrix34_t matrixFromPose(const float p[13]) {
  const float px = p[0], py = p[1], pz = p[2];
  const float qx = p[3], qy = p[4], qz = p[5], qw = p[6];

  // Standard quaternion → rotation matrix (right-handed).
  const float xx = qx * qx, yy = qy * qy, zz = qz * qz;
  const float xy = qx * qy, xz = qx * qz, yz = qy * qz;
  const float wx = qw * qx, wy = qw * qy, wz = qw * qz;

  vr::HmdMatrix34_t m{};
  m.m[0][0] = 1.0f - 2.0f * (yy + zz);
  m.m[0][1] = 2.0f * (xy - wz);
  m.m[0][2] = 2.0f * (xz + wy);
  m.m[0][3] = px;

  m.m[1][0] = 2.0f * (xy + wz);
  m.m[1][1] = 1.0f - 2.0f * (xx + zz);
  m.m[1][2] = 2.0f * (yz - wx);
  m.m[1][3] = py;

  m.m[2][0] = 2.0f * (xz - wy);
  m.m[2][1] = 2.0f * (yz + wx);
  m.m[2][2] = 1.0f - 2.0f * (xx + yy);
  m.m[2][3] = pz;
  return m;
}

vr::HmdMatrix44_t projectionFromFovTangents(const float fov[4],
                                            float zNear,
                                            float zFar) {
  // OpenVR's GetProjectionRaw returns tangents at the eye plane — i.e.
  // angles in radians where `tan(angle)` ≈ angle for small values, but
  // commonly stored already as tangent ratios on Quest/HMDs.
  //
  // We treat the input as tangent ratios (left, right, up, down) and
  // build the standard OpenGL-style asymmetric projection matrix used by
  // OpenVR's reference implementation.
  const float left   = fov[0] * zNear;
  const float right  = fov[1] * zNear;
  const float up     = fov[2] * zNear;
  const float down   = fov[3] * zNear;

  vr::HmdMatrix44_t m{};
  // OpenVR matrices are row-major (`m[row][col]` in the public header).
  // The frustum is built so that an object at +z=zFar (in eye space)
  // ends up at NDC z=+1 with -Z-forward convention used by OpenVR.
  m.m[0][0] = 2.0f * zNear / (right - left);
  m.m[0][1] = 0.0f;
  m.m[0][2] = (right + left) / (right - left);
  m.m[0][3] = 0.0f;

  m.m[1][0] = 0.0f;
  m.m[1][1] = 2.0f * zNear / (up - down);
  m.m[1][2] = (up + down) / (up - down);
  m.m[1][3] = 0.0f;

  m.m[2][0] = 0.0f;
  m.m[2][1] = 0.0f;
  // Standard non-reverse-Z OpenGL frustum.
  m.m[2][2] = -(zFar + zNear) / (zFar - zNear);
  m.m[2][3] = -(2.0f * zFar * zNear) / (zFar - zNear);

  m.m[3][0] = 0.0f;
  m.m[3][1] = 0.0f;
  m.m[3][2] = -1.0f;
  m.m[3][3] = 0.0f;
  return m;
}

void poseFromMatrix(const vr::HmdMatrix34_t& m, float outPos[3], float outQuat[4]) {
  outPos[0] = m.m[0][3];
  outPos[1] = m.m[1][3];
  outPos[2] = m.m[2][3];

  // Shepperd's method for numerically stable matrix→quat conversion.
  const float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
  if (trace > 0.0f) {
    float s = std::sqrt(trace + 1.0f) * 2.0f;
    outQuat[3] = 0.25f * s;
    outQuat[0] = (m.m[2][1] - m.m[1][2]) / s;
    outQuat[1] = (m.m[0][2] - m.m[2][0]) / s;
    outQuat[2] = (m.m[1][0] - m.m[0][1]) / s;
  } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
    float s = std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
    outQuat[3] = (m.m[2][1] - m.m[1][2]) / s;
    outQuat[0] = 0.25f * s;
    outQuat[1] = (m.m[0][1] + m.m[1][0]) / s;
    outQuat[2] = (m.m[0][2] + m.m[2][0]) / s;
  } else if (m.m[1][1] > m.m[2][2]) {
    float s = std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
    outQuat[3] = (m.m[0][2] - m.m[2][0]) / s;
    outQuat[0] = (m.m[0][1] + m.m[1][0]) / s;
    outQuat[1] = 0.25f * s;
    outQuat[2] = (m.m[1][2] + m.m[2][1]) / s;
  } else {
    float s = std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
    outQuat[3] = (m.m[1][0] - m.m[0][1]) / s;
    outQuat[0] = (m.m[0][2] + m.m[2][0]) / s;
    outQuat[1] = (m.m[1][2] + m.m[2][1]) / s;
    outQuat[2] = 0.25f * s;
  }
}

vr::HmdMatrix34_t mul34(const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b) {
  // Treat both as 4x4 with implicit bottom row [0 0 0 1]. For each row r in
  // a, multiply by each column of b's 4x4 representation.
  vr::HmdMatrix34_t out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
      float v = a.m[r][0] * b.m[0][c]
              + a.m[r][1] * b.m[1][c]
              + a.m[r][2] * b.m[2][c];
      // Bottom-row contribution (implicit 1 in column 3 only):
      if (c == 3) v += a.m[r][3];
      out.m[r][c] = v;
    }
  }
  return out;
}

vr::HmdMatrix34_t matrix34FromFlat(const float m[12]) {
  vr::HmdMatrix34_t out{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c)
      out.m[r][c] = m[r * 4 + c];
  return out;
}

}  // namespace fuvr::openvr_shim::posemath
