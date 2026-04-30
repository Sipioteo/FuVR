// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Singleton "mock state": owns the daemon RPC client, the IOSurface XPC
// client, the texture bridges, and the cached device caps. Every mock
// interface (IVRSystem, IVRCompositor, IVRInput…) reads from / writes to
// the same instance so we have a single source of truth for the
// translated session.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "daemon_rpc.hpp"
#include "iosurface_xpc.hpp"
#include "texture_bridge.hpp"

namespace fuvr::openvr_shim {

class MockState {
 public:
  static MockState& shared();

  bool initialize(const std::string& appKey, uint32_t appType);
  void shutdown();
  bool isInitialized() const { return initialized_.load(std::memory_order_acquire); }

  DaemonRpc& rpc() { return rpc_; }
  IOSurfaceXpc& xpc() { return xpc_; }
  const DeviceCaps& caps() const { return caps_; }

  // Pose cache shared between WaitGetPoses (Compositor) and the per-frame
  // GetDeviceToAbsoluteTrackingPose (System) / Pose action data (Input)
  // calls. Refreshed once per WaitGetPoses tick.
  void cachePoses(const PoseSet& poses) {
    std::lock_guard<std::mutex> lk(poseMu_);
    cached_ = poses;
    poseValid_ = true;
  }
  bool latestPoses(PoseSet& out) const {
    std::lock_guard<std::mutex> lk(poseMu_);
    if (!poseValid_) return false;
    out = cached_;
    return true;
  }

  // Lazily-allocated per-texture-type bridges. The OpenGL bridge requires
  // a current GL context to call `prepare`, so creation happens on the
  // first Submit() call from the game thread.
  TextureBridge* getOrCreateBridge(uint32_t textureType);

  // Action handle map (string → 64-bit handle). The shim hashes the
  // requested action path so the daemon can persist them across runs.
  uint64_t internActionHandle(const std::string& path);
  uint64_t internInputSourceHandle(const std::string& path);

 private:
  MockState() = default;

  std::atomic<bool> initialized_{false};
  DaemonRpc rpc_;
  IOSurfaceXpc xpc_;
  DeviceCaps caps_{};
  std::unique_ptr<TextureBridge> glBridge_;
  std::unique_ptr<TextureBridge> metalBridge_;
  std::mutex bridgeMu_;

  mutable std::mutex poseMu_;
  PoseSet cached_{};
  bool poseValid_{false};

  std::mutex handleMu_;
  std::unordered_map<std::string, uint64_t> actionHandles_;
  std::unordered_map<std::string, uint64_t> sourceHandles_;
  uint64_t nextActionHandle_{1};
  uint64_t nextSourceHandle_{1};
};

}  // namespace fuvr::openvr_shim
