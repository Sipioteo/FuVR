// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fuvr/action_state.hpp"
#include "fuvr/iosurface_xpc_client.hpp"
#include "fuvr/pose_predictor.hpp"

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::runtime {

struct EncodeStatSample;

struct StartSessionParams {
  uint32_t perEyeWidth{0};
  uint32_t perEyeHeight{0};
  uint32_t refreshRateHz{72};
  // 50 Mbps is a sensible HEVC ceiling for stereo over wired USB / strong
  // 5 GHz Wi-Fi. Intentionally headset-agnostic — bitrate is bounded by the
  // network, not the device. TODO: adapt at runtime from transport RTT/loss;
  // the daemon's bitrate-request control message already nudges it down.
  uint32_t videoBitrateBps{50'000'000};
  bool useHevc{true};
};

struct StartSessionResult {
  uint64_t sessionId{0};
  int64_t clockOffsetNs{0};
  uint64_t oneWayDelayNs{0};
  uint32_t virtualDisplayId{0};
};

// Snapshot of headset-reported capabilities, fetched via
// `DaemonClient::getDeviceCapabilities`. `valid` is false when no Quest has
// connected yet; callers fall back to internal defaults in that case.
struct DeviceCapabilities {
  bool valid{false};
  std::string deviceModel;
  std::string systemVersion;
  uint32_t perEyeWidth{0};
  uint32_t perEyeHeight{0};
  std::vector<uint32_t> refreshRatesHz;
  bool hasHandTracking{false};
  bool hasEyeTracking{false};
};

struct SubmitFrameArgs {
  uint64_t frameId{0};
  uint64_t renderStartNs{0};
  // Correlation token paired with the IOSurface that arrived (or will arrive)
  // on the XPC mach service `com.fuvr.daemon.surface`. See ADR-0007.
  uint64_t surfaceToken{0};
  bool forceIdr{false};
  Pose leftEye{};
  Pose rightEye{};
  // Per-eye FOV the runtime told the app to render with. Carried through to
  // the wire VideoFragmentHeader so the Quest's ATW shader knows the actual
  // rendered FOV (typically wider than the headset's native fov_now thanks to
  // overscan). Default-zero means "unset, fall back to fov_now".
  Fov leftFov{};
  Fov rightFov{};
};

// Resolve the path to the daemon RPC socket. Public for tests.
std::string resolveDaemonSocketPath();

class DaemonClient {
 public:
  using PoseCallback = std::function<void(const PoseSample&)>;
  using EncodeStatsCallback = std::function<void(const EncodeStatSample&)>;
  using DisconnectCallback = std::function<void()>;
  using InputCallback = std::function<void(const InputSnapshot&)>;
  using ReconnectCallback = std::function<void()>;

  DaemonClient();
  ~DaemonClient();

  DaemonClient(const DaemonClient&) = delete;
  DaemonClient& operator=(const DaemonClient&) = delete;

  // Override the socket path (test seam). Must be called before connect().
  void setSocketPathForTesting(std::string path) noexcept;

  // Register a callback for incoming PoseSnapshot envelopes. Replaces any prior.
  void setPoseCallback(PoseCallback cb) noexcept;
  void setEncodeStatsCallback(EncodeStatsCallback cb) noexcept;
  void setDisconnectCallback(DisconnectCallback cb) noexcept;
  void setInputCallback(InputCallback cb) noexcept;
  void setReconnectCallback(ReconnectCallback cb) noexcept;

  // Cap on the per-attempt sleep before reconnect.
  void setMaxBackoffMs(uint32_t ms) noexcept { maxBackoffMs_ = ms; }

  // Total elapsed reconnect attempts since last successful connect.
  uint32_t reconnectAttempts() const noexcept { return reconnectAttempts_.load(); }

  // Lazily connect (idempotent). Returns true if connected (or already was).
  bool ensureConnected() noexcept;

  // Synchronously sends StartSession; waits up to timeoutMs for the ack.
  bool startSession(const StartSessionParams& params,
                    StartSessionResult* out,
                    uint32_t timeoutMs = 2000) noexcept;

  // Fetch latest headset capability snapshot from the daemon (forwarded from
  // the Quest's helloFromQuest). Synchronous; returns false on timeout or
  // when the daemon connection is unavailable. `out->valid` reflects whether
  // the daemon has a real snapshot or the call merely round-tripped.
  bool getDeviceCapabilities(DeviceCapabilities* out,
                             uint32_t timeoutMs = 500) noexcept;

  // Subscribe to the pose stream. Idempotent.
  bool subscribePoses(uint64_t sessionId) noexcept;

  // Subscribe to the input stream and the encode-stats stream.
  bool subscribeInputs(uint64_t sessionId) noexcept;
  bool subscribeEncodeStats() noexcept;

  // Fire-and-forget frame submission. The IOSurface itself is shipped over
  // the parallel XPC mach service ahead of this call; this carries only the
  // correlation token and pose metadata. See ADR-0007.
  bool submitFrame(uint64_t sessionId, const SubmitFrameArgs& args) noexcept;

  // Optionally hand the XPC client to the frame sink for IOSurface delivery.
  void setXpcClient(IOSurfaceXpcClient* client) noexcept { xpc_ = client; }
  IOSurfaceXpcClient* xpcClient() const noexcept { return xpc_; }

  // Disconnect and stop the reader thread.
  void shutdown() noexcept;

  bool isConnected() const noexcept { return fd_.load() >= 0; }

 private:
  bool connectLocked() noexcept;
  void readerLoop() noexcept;
  void scheduleReconnect() noexcept;

  std::string socketPath_;
  std::atomic<int> fd_{-1};
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> nextSeq_{1};

  std::thread reader_;
  std::mutex sendMutex_;

  std::mutex respMutex_;
  std::condition_variable respCv_;
  uint64_t pendingSeq_{0};
  bool gotResponse_{false};
  StartSessionResult lastStartAck_{};
  DeviceCapabilities lastCapsResponse_{};
  bool gotCapsResponse_{false};

  std::mutex cbMutex_;
  PoseCallback poseCb_;
  EncodeStatsCallback statsCb_;
  DisconnectCallback disconnectCb_;
  InputCallback inputCb_;
  ReconnectCallback reconnectCb_;
  bool disconnectFired_{false};

  uint32_t backoffMs_{20};
  uint32_t maxBackoffMs_{5000};
  std::atomic<uint32_t> reconnectAttempts_{0};
  IOSurfaceXpcClient* xpc_{nullptr};
};

}  // namespace fuvr::runtime
