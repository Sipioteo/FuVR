// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "fuvr/frame_sink.hpp"
#include "fuvr/iosurface_swapchain.hpp"
#include "fuvr/pose_predictor.hpp"

namespace fuvr::runtime {

class DaemonClient;

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
  uint64_t daemonSessionId{0};
  void* metalDevice{nullptr};  // id<MTLDevice>, retained
  std::mutex mutex;
};

struct Instance {
  XrInstance handle{XR_NULL_HANDLE};
  std::vector<std::unique_ptr<Session>> sessions;
  std::vector<std::unique_ptr<ActionSet>> actionSets;
  std::mutex mutex;
};

XrResult getInstanceProcAddr(XrInstance instance, const char* name,
                             PFN_xrVoidFunction* function) noexcept;

Instance* lookupInstance(XrInstance handle) noexcept;
Session* lookupSession(XrSession handle) noexcept;

}  // namespace fuvr::runtime
