// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Thin synchronous client for the FuVR daemon, speaking the wire protocol
// defined in `wire.hpp`. One connection per game process.
//
// All public methods are thread-safe via a single send/recv mutex — game
// engines typically only hit this from one thread per category (render
// thread for Submit, simulation thread for poses) and the round-trips are
// sub-millisecond on a Unix socket, so contention is negligible.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "wire.hpp"

namespace fuvr::openvr_shim {

struct DeviceCaps {
  uint32_t perEyeWidth{0};
  uint32_t perEyeHeight{0};
  uint32_t refreshRateHz{72};
  float    leftFov[4]{0};
  float    rightFov[4]{0};
  float    eyeFromHeadLeft[12]{0};
  float    eyeFromHeadRight[12]{0};
  uint32_t controllerMask{0};
};

struct PoseSet {
  uint32_t validMask{0};
  // 3 pos + 4 quat + 3 linVel + 3 angVel = 13 floats.
  float    hmd[13]{0};
  float    leftCtrl[13]{0};
  float    rightCtrl[13]{0};
};

struct WaitFrameInfo {
  uint64_t targetDisplayTimeNs{0};
  uint64_t cpuFrameStartNs{0};
  uint64_t frameIndex{0};
};

class DaemonRpc {
 public:
  DaemonRpc();
  ~DaemonRpc();

  DaemonRpc(const DaemonRpc&) = delete;
  DaemonRpc& operator=(const DaemonRpc&) = delete;

  /// Connect to the daemon's openvr socket. `socketPath` defaults to the
  /// spec-defined `/tmp/fuvr_openvr.sock`; override via `FUVR_OPENVR_SOCKET`.
  /// On success, `caps` is populated from the `HelloOk` reply.
  bool connect(const std::string& appKey, uint32_t appType, DeviceCaps& caps);

  void disconnect();

  bool isConnected() const { return fd_ >= 0; }

  // ---- Tracking ----
  bool queryPoses(float predictedSecondsFromNow,
                  uint32_t universeOrigin,
                  PoseSet& out);
  bool waitFrame(WaitFrameInfo& info);

  // ---- Frame submit ----
  /// `surfaceToken` is a fresh 64-bit id; the caller must have already
  /// shipped the matching IOSurface over the XPC service before calling
  /// this. Pose is the 3x4 row-major matrix the eye texture was rendered
  /// from (used by the daemon for late-warp).
  bool submitFrame(uint32_t eye,
                   uint64_t surfaceToken,
                   uint32_t flags,
                   const float boundsUVMinMax[4],
                   const float renderPose[12],
                   const float renderPoseLeft[7],
                   const float renderPoseRight[7],
                   const float leftFov[4],
                   const float rightFov[4]);

  // ---- Input ----
  bool updateActions(const std::vector<uint64_t>& handles,
                     std::vector<wire::ActionStateEntry>& out);
  bool triggerHaptic(uint64_t deviceHandle,
                     float startSecondsFromNow,
                     float durationSeconds,
                     float frequency,
                     float amplitude);

  bool ping();

 private:
  bool sendAll(const void* buf, size_t n);
  bool recvAll(void* buf, size_t n);
  bool sendMessage(wire::MessageType type,
                   const void* payload,
                   uint32_t payloadLen,
                   uint32_t* outRequestId = nullptr);
  bool recvMessage(wire::MessageType expectedType,
                   uint32_t expectedRequestId,
                   std::vector<uint8_t>& payload);

  int fd_{-1};
  uint32_t nextRequestId_{1};
  std::mutex io_;
};

}  // namespace fuvr::openvr_shim
