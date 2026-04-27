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

#include "fuvr/pose_predictor.hpp"

namespace fuvr::runtime {

struct StartSessionParams {
  uint32_t perEyeWidth{0};
  uint32_t perEyeHeight{0};
  uint32_t refreshRateHz{72};
  uint32_t videoBitrateBps{50'000'000};
  bool useHevc{true};
};

struct StartSessionResult {
  uint64_t sessionId{0};
  int64_t clockOffsetNs{0};
  uint64_t oneWayDelayNs{0};
  uint32_t virtualDisplayId{0};
};

struct SubmitFrameArgs {
  uint64_t frameId{0};
  uint64_t renderStartNs{0};
  uint32_t machSendRight{0};  // mach_port_t for IOSurface send-right (consumed)
  bool forceIdr{false};
  Pose leftEye{};
  Pose rightEye{};
};

// Resolve the path to the daemon RPC socket. Public for tests.
std::string resolveDaemonSocketPath();

class DaemonClient {
 public:
  using PoseCallback = std::function<void(const PoseSample&)>;

  DaemonClient();
  ~DaemonClient();

  DaemonClient(const DaemonClient&) = delete;
  DaemonClient& operator=(const DaemonClient&) = delete;

  // Override the socket path (test seam). Must be called before connect().
  void setSocketPathForTesting(std::string path) noexcept;

  // Register a callback for incoming PoseSnapshot envelopes. Replaces any prior.
  void setPoseCallback(PoseCallback cb) noexcept;

  // Lazily connect (idempotent). Returns true if connected (or already was).
  bool ensureConnected() noexcept;

  // Synchronously sends StartSession; waits up to timeoutMs for the ack.
  bool startSession(const StartSessionParams& params,
                    StartSessionResult* out,
                    uint32_t timeoutMs = 2000) noexcept;

  // Subscribe to the pose stream. Idempotent.
  bool subscribePoses(uint64_t sessionId) noexcept;

  // Fire-and-forget frame submission. Sends mach send-right via SCM_RIGHTS.
  // Does NOT close machSendRight on the caller's behalf; the underlying
  // sendmsg + the kernel handle the right transfer. Caller may release the
  // local mach port allocation after this call returns.
  bool submitFrame(uint64_t sessionId, const SubmitFrameArgs& args) noexcept;

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

  std::mutex cbMutex_;
  PoseCallback poseCb_;

  uint32_t backoffMs_{20};
};

}  // namespace fuvr::runtime
