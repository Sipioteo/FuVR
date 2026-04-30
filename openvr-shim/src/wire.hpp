// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Wire format for the FuVR ↔ OpenVR-shim Unix-domain-socket protocol.
//
// Why a hand-rolled binary protocol (and not Cap'n Proto like the rest of
// the daemon RPC): the shim must build as a Universal Binary (x86_64+arm64)
// so legacy SteamVR Mac titles running through Rosetta 2 can dlopen() it.
// Homebrew's Cap'n Proto is arm64-only; pulling in capnp via FetchContent
// to build it for both arches roughly doubles the shim's link time and
// drags ~6 MB of static runtime per slice. Since the message surface is
// tiny (one connection, fixed-shape POD messages), a hand-rolled framing
// pays back in seconds and dependency-graph simplicity.
//
// All numeric fields are LITTLE-ENDIAN (the shim and daemon always run on
// the same Apple Silicon / x86_64 host — both are LE — so this is mostly a
// future-proofing choice, not active byte-swapping). Floats follow the
// host IEEE-754 layout.
//
// Message framing:
//   ┌────────────────┐
//   │  WireHeader    │  16 bytes
//   ├────────────────┤
//   │  payload       │  exactly header.payloadLen bytes
//   └────────────────┘
//
// IOSurface handoff is OUT-OF-BAND: the shim ships the `IOSurfaceRef` over
// the existing `com.fuvr.daemon.surface` XPC mach service, keyed by the
// `surfaceToken` field carried in the SubmitFrame message. The daemon
// pairs the two streams by token. See ADR-0007 for the rationale.

#include <cstdint>

namespace fuvr::openvr_shim::wire {

// ABI sentinel — bumped on any incompatible message layout change.
constexpr uint32_t kMagic        = 0x46565244; // 'FVRD'
// Bumped to 2: SubmitFramePayload gained per-eye render poses + FOV so the
// daemon can forward them through to the Quest projection layer instead of
// reusing the HMD-center pose for both eyes (which double-counted IPD via
// runtime reprojection).
constexpr uint16_t kProtocolVer  = 2;

// Default socket path. Spec-defined.
constexpr const char* kSocketPath = "/tmp/fuvr_openvr.sock";

enum class MessageType : uint16_t {
  // ---- Lifecycle ----
  Hello         = 0x0001, // shim → daemon: pid, app key, requested type
  HelloOk       = 0x0002, // daemon → shim: device caps + per-eye dims
  Goodbye       = 0x0003, // either direction: clean disconnect

  // ---- Tracking ----
  PoseQuery     = 0x0010, // shim → daemon: predictedSecondsFromNow
  PoseSnapshot  = 0x0011, // daemon → shim: hmd + 2 controller poses
  WaitFrame     = 0x0012, // shim → daemon: blocks until next swap deadline
  WaitFrameOk   = 0x0013, // daemon → shim: targetDisplayTimeNs

  // ---- Frames ----
  SubmitFrame   = 0x0020, // shim → daemon: eye + surfaceToken + bounds + pose

  // ---- Input ----
  ActionUpdate  = 0x0030, // shim → daemon: list of action handles to refresh
  ActionState   = 0x0031, // daemon → shim: digital/analog/pose state per handle
  Haptic        = 0x0032, // shim → daemon: trigger vibration

  // ---- Health ----
  Ping          = 0x00f0,
  Pong          = 0x00f1,
};

#pragma pack(push, 1)

struct WireHeader {
  uint32_t magic;        // = kMagic
  uint16_t version;      // = kProtocolVer
  uint16_t type;         // MessageType
  uint32_t payloadLen;   // bytes that follow this header
  uint32_t requestId;    // shim-incremented; daemon echoes for replies
};
static_assert(sizeof(WireHeader) == 16, "WireHeader must be 16 bytes");

// ---- Lifecycle payloads ----

struct HelloPayload {
  uint32_t pid;
  uint32_t appType;       // mirrors vr::EVRApplicationType
  char     appKey[64];    // null-terminated; truncated if oversize
};

struct HelloOkPayload {
  uint32_t perEyeWidth;
  uint32_t perEyeHeight;
  uint32_t refreshRateHz;
  // Tangents at 1m for each eye: { left, right, up, down } in radians,
  // matching OpenVR's GetProjectionRaw convention (NOT GetProjectionMatrix
  // — the matrix is computed shim-side from these tangents so the game's
  // near/far clipping plane is honoured).
  float    leftFov[4];
  float    rightFov[4];
  // Eye-to-head transforms (3x4 row-major), millimetres along x for IPD.
  float    eyeFromHeadLeft[12];
  float    eyeFromHeadRight[12];
  // Connected-controller bitmask — bit 0 = left, bit 1 = right.
  uint32_t controllerMask;
};

// ---- Tracking payloads ----

struct PoseQueryPayload {
  float    predictedSecondsFromNow;
  uint32_t universeOrigin;   // 0 = seated, 1 = standing, 2 = raw
};

struct PosePayload {
  // Pose validity flags (bit 0 hmd, bit 1 left, bit 2 right).
  uint32_t validMask;
  uint32_t reserved;
  // Each pose: 3 floats position (m), 4 floats quaternion (x,y,z,w),
  // 3 floats linear velocity (m/s), 3 floats angular velocity (rad/s).
  float    hmd[13];
  float    leftCtrl[13];
  float    rightCtrl[13];
};

struct WaitFrameOkPayload {
  uint64_t targetDisplayTimeNs;
  uint64_t cpuFrameStartNs;
  // Bumped by 1 each time the daemon thinks we should redraw. A jump
  // larger than 1 between successive waits indicates we missed a vsync.
  uint64_t frameIndex;
};

// ---- Frame submission payload ----

struct SubmitFramePayload {
  uint64_t surfaceToken;     // pairs with XPC IOSurface send-right
  uint32_t eye;              // 0 = left, 1 = right
  uint32_t flags;            // mirrors vr::EVRSubmitFlags
  // Texture sub-rect inside the IOSurface (0..1).
  float    boundsUMin, boundsUMax, boundsVMin, boundsVMax;
  // The HMD-center pose used to render this submission (3x4 row-major).
  // Retained for telemetry / late-warp baseline. Per-eye poses below are
  // what the Quest's projection layer actually consumes.
  float    renderPoseHmd[12];
  // Per-eye render pose: { pos.xyz, quat.xyzw } with the eye-from-head
  // offset already composed in (i.e. world ← head ← eye). The Quest
  // projection-layer compositor uses these as `views[i].pose` so its
  // scan-out reprojection delta is computed from the camera the texture
  // was actually rendered from. Identity quaternion (and zero pos) means
  // "uninitialised, fall back to per-eye xrLocateViews".
  float    renderPoseLeft[7];
  float    renderPoseRight[7];
  // Per-eye FOV tangents { left, right, up, down } matching OpenVR's
  // GetProjectionRaw sign convention (left & down negative). Forwarded to
  // `views[i].fov` on the Quest. All zeros means "fall back to current
  // xrLocateViews fov".
  float    leftFov[4];
  float    rightFov[4];
};

// ---- Input payloads ----

// Shim sends the list of action handles it's interested in this tick;
// the daemon replies with the consolidated state for all of them.
struct ActionUpdatePayload {
  uint32_t handleCount;
  // followed by `handleCount` × uint64_t handles.
};

enum class ActionKind : uint8_t {
  Digital = 1,
  Analog  = 2,
  Pose    = 3,
};

struct ActionStateEntry {
  uint64_t handle;
  uint8_t  kind;       // ActionKind
  uint8_t  active;
  uint8_t  changed;
  uint8_t  _pad;
  // Polymorphic body — interpret by `kind`:
  // Digital: state = bool (in body[0])
  // Analog : x/y/z/delta (4 floats)
  // Pose   : 3 pos + 4 quat + valid flag
  float    body[8];
  uint64_t timestampNs;
};

struct ActionStatePayload {
  uint32_t entryCount;
  // followed by `entryCount` × ActionStateEntry.
};

struct HapticPayload {
  uint64_t deviceHandle;
  float    startSecondsFromNow;
  float    durationSeconds;
  float    frequency;
  float    amplitude;
};

#pragma pack(pop)

}  // namespace fuvr::openvr_shim::wire
