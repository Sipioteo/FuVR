// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Pose math helpers — Quest poses arrive as { position[3], quaternion[4] }
// (OpenXR convention: x,y,z,w; right-handed; +Y up; -Z forward) and OpenVR
// expects `HmdMatrix34_t` (row-major 3x4 transform; right-handed; +Y up;
// -Z forward) plus `HmdMatrix44_t` projection from {l,r,u,d} tangents.
//
// Both libraries use the same handedness/axes — no axis swap, just a
// quat→matrix conversion plus projection synthesis from tangent FoV.

#include <cstdint>

#include "openvr.h"

namespace fuvr::openvr_shim::posemath {

// Identity 3x4 transform (column 3 = origin).
extern const vr::HmdMatrix34_t kIdentity34;

/// Build an OpenVR row-major 3x4 from { pos.xyz, quat.xyzw }. Quaternion
/// must be unit-length; non-unit input distorts the resulting matrix.
vr::HmdMatrix34_t matrixFromPose(const float pose13[13]);

/// Build a `HmdMatrix44_t` projection from raw {l,r,u,d} tangents at 1m,
/// matching OpenVR's `IVRSystem::GetProjectionMatrix(eye, near, far)` for
/// the asymmetric off-axis frustum every modern HMD uses. Reverse-Z is
/// NOT applied — most legacy SteamVR Mac titles assume the historical
/// [near..far] depth range.
vr::HmdMatrix44_t projectionFromFovTangents(const float fov[4],
                                            float zNear,
                                            float zFar);

/// Decompose an `HmdMatrix34_t` back to { pos[3], quat[4] }, primarily so
/// the daemon's late-warp reconstruction sees the same numbers we sent.
void poseFromMatrix(const vr::HmdMatrix34_t& m, float outPos[3], float outQuat[4]);

/// Multiply two 3x4 row-major affine transforms: `out = a * b`. The bottom
/// implicit row is [0 0 0 1]. Used to compose a head-pose with an
/// eye-from-head transform to get the per-eye render pose.
vr::HmdMatrix34_t mul34(const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b);

/// Build a 3x4 from a flat 12-float row-major array (eyeFromHead style).
vr::HmdMatrix34_t matrix34FromFlat(const float m[12]);

}  // namespace fuvr::openvr_shim::posemath
