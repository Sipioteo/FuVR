// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "fuvr/runtime.hpp"

namespace fuvr::runtime {

namespace detail {
std::unordered_map<uint64_t, Session*>& sessions() noexcept;
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
}  // namespace detail

XrResult xrCreateSession_impl(XrInstance instance, const XrSessionCreateInfo* info,
                               XrSession* out) noexcept {
  Instance* inst = lookupInstance(instance);
  if (inst == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  auto session = std::make_unique<Session>();
  session->instance = inst;
  session->state = XR_SESSION_STATE_IDLE;
  session->frameSink = makeDefaultFrameSink();
  const uint64_t h = detail::nextHandleAlloc();
  session->handle = reinterpret_cast<XrSession>(h);
  Session* raw = session.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::sessions().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(inst->mutex);
    inst->sessions.push_back(std::move(session));
  }
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrDestroySession_impl(XrSession sessionHandle) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::sessions().erase(reinterpret_cast<uint64_t>(sessionHandle));
  }
  Instance* inst = s->instance;
  std::lock_guard<std::mutex> lk(inst->mutex);
  for (auto it = inst->sessions.begin(); it != inst->sessions.end(); ++it) {
    if (it->get() == s) {
      inst->sessions.erase(it);
      break;
    }
  }
  return XR_SUCCESS;
}

XrResult xrBeginSession_impl(XrSession sessionHandle,
                              const XrSessionBeginInfo* info) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->state = XR_SESSION_STATE_SYNCHRONIZED;
  return XR_SUCCESS;
}

XrResult xrEndSession_impl(XrSession sessionHandle) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->state = XR_SESSION_STATE_STOPPING;
  return XR_SUCCESS;
}

XrResult xrRequestExitSession_impl(XrSession sessionHandle) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->state = XR_SESSION_STATE_EXITING;
  return XR_SUCCESS;
}

XrResult xrWaitFrame_impl(XrSession sessionHandle, const XrFrameWaitInfo*,
                           XrFrameState* state) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  using clock = std::chrono::steady_clock;
  const auto now = clock::now().time_since_epoch();
  state->predictedDisplayTime = static_cast<XrTime>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() +
      11'111'111);
  state->predictedDisplayPeriod = 11'111'111;
  state->shouldRender = XR_TRUE;
  return XR_SUCCESS;
}

XrResult xrBeginFrame_impl(XrSession sessionHandle,
                            const XrFrameBeginInfo*) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->frameId.fetch_add(1, std::memory_order_relaxed);
  return XR_SUCCESS;
}

XrResult xrEndFrame_impl(XrSession sessionHandle,
                          const XrFrameEndInfo* info) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (s->frameSink) {
    SubmittedFrame f{};
    f.frameId = s->frameId.load(std::memory_order_relaxed);
    f.targetDisplayTimeNs = static_cast<uint64_t>(info->displayTime);
    s->frameSink->submit(f);
  }
  return XR_SUCCESS;
}

XrResult xrLocateViews_impl(XrSession sessionHandle,
                             const XrViewLocateInfo* info, XrViewState* state,
                             uint32_t capacity, uint32_t* countOutput,
                             XrView* views) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || countOutput == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (views == nullptr || capacity == 0) {
    *countOutput = 2;
    return XR_SUCCESS;
  }
  if (capacity < 2) {
    *countOutput = 2;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  if (state != nullptr) {
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                            XR_VIEW_STATE_POSITION_VALID_BIT |
                            XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
                            XR_VIEW_STATE_POSITION_TRACKED_BIT;
  }
  auto predicted = s->predictor.predict(static_cast<uint64_t>(info->displayTime));
  for (uint32_t i = 0; i < 2; ++i) {
    views[i].type = XR_TYPE_VIEW;
    views[i].next = nullptr;
    views[i].fov.angleLeft = -0.95f;
    views[i].fov.angleRight = 0.95f;
    views[i].fov.angleUp = 0.95f;
    views[i].fov.angleDown = -0.95f;
    if (predicted.has_value()) {
      const Pose& p = (i == 0) ? predicted->leftEye : predicted->rightEye;
      views[i].pose.position = {p.position.x, p.position.y, p.position.z};
      views[i].pose.orientation = {p.orientation.x, p.orientation.y,
                                    p.orientation.z, p.orientation.w};
    } else {
      views[i].pose.position = {0.0f, 0.0f, 0.0f};
      views[i].pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    }
  }
  *countOutput = 2;
  return XR_SUCCESS;
}

XrResult xrCreateReferenceSpace_impl(XrSession, const XrReferenceSpaceCreateInfo*,
                                      XrSpace*) noexcept {
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

XrResult xrDestroySpace_impl(XrSpace) noexcept {
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

XrResult xrLocateSpace_impl(XrSpace, XrSpace, XrTime, XrSpaceLocation*) noexcept {
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

XrResult xrEnumerateReferenceSpaces_impl(XrSession sessionHandle,
                                          uint32_t capacity, uint32_t* count,
                                          XrReferenceSpaceType* out) noexcept {
  if (lookupSession(sessionHandle) == nullptr || count == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  static const XrReferenceSpaceType kTypes[] = {
      XR_REFERENCE_SPACE_TYPE_VIEW,
      XR_REFERENCE_SPACE_TYPE_LOCAL,
      XR_REFERENCE_SPACE_TYPE_STAGE,
  };
  const uint32_t total = 3;
  if (out == nullptr || capacity == 0) {
    *count = total;
    return XR_SUCCESS;
  }
  if (capacity < total) {
    *count = total;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  for (uint32_t i = 0; i < total; ++i) {
    out[i] = kTypes[i];
  }
  *count = total;
  return XR_SUCCESS;
}

XrResult xrCreateActionSpace_impl(XrSession, const XrActionSpaceCreateInfo*,
                                   XrSpace*) noexcept {
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

}  // namespace fuvr::runtime
