// SPDX-License-Identifier: Apache-2.0
#pragma once

#define XR_USE_GRAPHICS_API_METAL 1
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fuvr/action_state.hpp"
#include "fuvr/event_queue.hpp"
#include "fuvr/frame_sink.hpp"
#include "fuvr/iosurface_swapchain.hpp"
#include "fuvr/iosurface_xpc_client.hpp"
#include "fuvr/pose_predictor.hpp"
#include "fuvr/spaces.hpp"

namespace fuvr::runtime {

class DaemonClient;

struct EncodeStatSample {
  uint64_t frameId{0};
  uint64_t encodeDurationNs{0};
  uint32_t encodedSizeBytes{0};
  uint64_t arrivalNs{0};
  bool wasKeyframe{false};
};

struct EncoderStatsSnapshot {
  uint32_t sampleCount{0};
  double meanEncodeMs{0.0};
  double p95EncodeMs{0.0};
  double fps{0.0};
  double bitrateMbps{0.0};
};

class EncoderStats {
 public:
  static constexpr std::size_t kWindow = 256;
  void push(const EncodeStatSample& s) noexcept;
  EncoderStatsSnapshot snapshot() const noexcept;

 private:
  mutable std::mutex mu_;
  std::array<EncodeStatSample, kWindow> ring_{};
  std::size_t head_{0};
  std::size_t count_{0};
};

constexpr const char* kRuntimeName = "FuVR";
constexpr uint32_t kRuntimeVersion = XR_MAKE_VERSION(0, 1, 0);

struct Instance;
struct Session;
struct ActionSet;
struct Action;
struct Swapchain;

struct Action {
  XrAction handle{XR_NULL_HANDLE};
  ActionSet* parent{nullptr};
  XrActionType type{XR_ACTION_TYPE_BOOLEAN_INPUT};
  std::vector<XrPath> subactionPaths;
  std::string name;
  // Suggested binding paths supplied via xrSuggestInteractionProfileBindings.
  std::vector<XrPath> suggestedBindings;
};

struct HandTracker {
  uint64_t handle{0};
  Hand hand{Hand::Left};
  Session* session{nullptr};
};

struct ActionSet {
  XrActionSet handle{XR_NULL_HANDLE};
  Instance* instance{nullptr};
  std::vector<std::unique_ptr<Action>> actions;
};

struct Swapchain {
  XrSwapchain handle{XR_NULL_HANDLE};
  Session* session{nullptr};
  uint32_t width{0};
  uint32_t height{0};
  int64_t format{0};
  uint32_t arraySize{1};
  uint32_t acquiredIndex{0};
  uint32_t lastReleasedIndex{0};
  std::vector<std::unique_ptr<IOSurfaceImage>> images;
};

struct Session {
  XrSession handle{XR_NULL_HANDLE};
  Instance* instance{nullptr};
  XrSessionState state{XR_SESSION_STATE_IDLE};
  std::atomic<uint64_t> frameId{0};
  std::vector<std::unique_ptr<Swapchain>> swapchains;
  std::vector<ActionSet*> attachedActionSets;
  std::unique_ptr<FrameSink> frameSink;
  PosePredictor predictor;
  std::shared_ptr<DaemonClient> daemon;
  std::unique_ptr<IOSurfaceXpcClient> xpcClient;
  uint64_t daemonSessionId{0};
  // Forward offset (ns) added to predictedDisplayTime + pose predict() arg to
  // mask end-to-end Mac->Quest latency (encode + transport + decode + scanout).
  // Set at session start from daemon's measured oneWayDelayNs plus a fixed
  // render-budget; overridable via FUVR_RT_POSE_LOOKAHEAD_MS.
  uint64_t poseLookaheadNs{70'000'000};
  void* metalDevice{nullptr};  // id<MTLDevice>, retained
  void* metalCommandQueue{nullptr};  // id<MTLCommandQueue> from KHR binding (NOT retained — owned by app)
  std::vector<std::unique_ptr<Space>> spaces;
  Pose localOriginPose{};
  EncoderStats encoderStats{};
  ActionStateCache actionState{};
  std::vector<std::unique_ptr<HandTracker>> handTrackers;
  std::atomic<bool> daemonAlive{false};
  std::atomic<uint32_t> reconnectCount{0};
  bool beginSessionSent{false};
  bool interactionProfileEmitted{false};
  // STEREO-SPLIT: combines per-eye IOSurfaces into one side-by-side surface
  // before submitting to the daemon's encoder. Lazily initialized in
  // xrEndFrame on the first projection layer with viewCount >= 2.
  std::unique_ptr<StereoBlitter> stereoBlitter;
  std::mutex mutex;

  EncoderStatsSnapshot encoderStatsSnapshot() const noexcept {
    return encoderStats.snapshot();
  }
};

struct Instance {
  XrInstance handle{XR_NULL_HANDLE};
  std::vector<std::unique_ptr<Session>> sessions;
  std::vector<std::unique_ptr<ActionSet>> actionSets;
  EventQueue events;
  std::mutex mutex;
};

XrResult getInstanceProcAddr(XrInstance instance, const char* name,
                             PFN_xrVoidFunction* function) noexcept;

Instance* lookupInstance(XrInstance handle) noexcept;
Session* lookupSession(XrSession handle) noexcept;

}  // namespace fuvr::runtime
