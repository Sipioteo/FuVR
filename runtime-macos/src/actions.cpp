// SPDX-License-Identifier: Apache-2.0
#include <mutex>
#include <unordered_map>

#include "fuvr/runtime.hpp"

namespace fuvr::runtime {

namespace detail {
std::unordered_map<uint64_t, ActionSet*>& actionSets() noexcept;
std::unordered_map<uint64_t, Action*>& actions() noexcept;
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
}  // namespace detail

namespace {

ActionSet* lookupActionSet(XrActionSet h) noexcept {
  std::lock_guard<std::mutex> lk(detail::globalMutex());
  auto& m = detail::actionSets();
  auto it = m.find(reinterpret_cast<uint64_t>(h));
  return it == m.end() ? nullptr : it->second;
}

Action* lookupAction(XrAction h) noexcept {
  std::lock_guard<std::mutex> lk(detail::globalMutex());
  auto& m = detail::actions();
  auto it = m.find(reinterpret_cast<uint64_t>(h));
  return it == m.end() ? nullptr : it->second;
}

}  // namespace

XrResult xrCreateActionSet_impl(XrInstance instance,
                                 const XrActionSetCreateInfo* info,
                                 XrActionSet* out) noexcept {
  Instance* inst = lookupInstance(instance);
  if (inst == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  auto set = std::make_unique<ActionSet>();
  set->instance = inst;
  const uint64_t h = detail::nextHandleAlloc();
  set->handle = reinterpret_cast<XrActionSet>(h);
  ActionSet* raw = set.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::actionSets().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(inst->mutex);
    inst->actionSets.push_back(std::move(set));
  }
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrDestroyActionSet_impl(XrActionSet handle) noexcept {
  ActionSet* set = lookupActionSet(handle);
  if (set == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  Instance* inst = set->instance;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::actionSets().erase(reinterpret_cast<uint64_t>(handle));
    for (auto& a : set->actions) {
      detail::actions().erase(reinterpret_cast<uint64_t>(a->handle));
    }
  }
  std::lock_guard<std::mutex> lk(inst->mutex);
  for (auto it = inst->actionSets.begin(); it != inst->actionSets.end(); ++it) {
    if (it->get() == set) {
      inst->actionSets.erase(it);
      break;
    }
  }
  return XR_SUCCESS;
}

XrResult xrCreateAction_impl(XrActionSet setHandle,
                              const XrActionCreateInfo* info,
                              XrAction* out) noexcept {
  ActionSet* set = lookupActionSet(setHandle);
  if (set == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  auto action = std::make_unique<Action>();
  action->parent = set;
  action->type = info->actionType;
  const uint64_t h = detail::nextHandleAlloc();
  action->handle = reinterpret_cast<XrAction>(h);
  Action* raw = action.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::actions().emplace(h, raw);
  }
  set->actions.push_back(std::move(action));
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrDestroyAction_impl(XrAction handle) noexcept {
  Action* a = lookupAction(handle);
  if (a == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  ActionSet* set = a->parent;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::actions().erase(reinterpret_cast<uint64_t>(handle));
  }
  for (auto it = set->actions.begin(); it != set->actions.end(); ++it) {
    if (it->get() == a) {
      set->actions.erase(it);
      break;
    }
  }
  return XR_SUCCESS;
}

XrResult xrSuggestInteractionProfileBindings_impl(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* bindings) noexcept {
  if (lookupInstance(instance) == nullptr || bindings == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  return XR_SUCCESS;
}

XrResult xrAttachSessionActionSets_impl(
    XrSession sessionHandle,
    const XrSessionActionSetsAttachInfo* info) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  std::lock_guard<std::mutex> lk(s->mutex);
  for (uint32_t i = 0; i < info->countActionSets; ++i) {
    if (ActionSet* set = lookupActionSet(info->actionSets[i])) {
      s->attachedActionSets.push_back(set);
    }
  }
  return XR_SUCCESS;
}

XrResult xrSyncActions_impl(XrSession sessionHandle,
                             const XrActionsSyncInfo* info) noexcept {
  if (lookupSession(sessionHandle) == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  return XR_SUCCESS;
}

XrResult xrGetActionStateBoolean_impl(XrSession sessionHandle,
                                        const XrActionStateGetInfo* info,
                                        XrActionStateBoolean* state) noexcept {
  if (lookupSession(sessionHandle) == nullptr || info == nullptr ||
      state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->currentState = XR_FALSE;
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  state->isActive = XR_FALSE;
  return XR_SUCCESS;
}

XrResult xrGetActionStateFloat_impl(XrSession sessionHandle,
                                      const XrActionStateGetInfo* info,
                                      XrActionStateFloat* state) noexcept {
  if (lookupSession(sessionHandle) == nullptr || info == nullptr ||
      state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->currentState = 0.0f;
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  state->isActive = XR_FALSE;
  return XR_SUCCESS;
}

XrResult xrGetActionStateVector2f_impl(XrSession sessionHandle,
                                         const XrActionStateGetInfo* info,
                                         XrActionStateVector2f* state) noexcept {
  if (lookupSession(sessionHandle) == nullptr || info == nullptr ||
      state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->currentState = {0.0f, 0.0f};
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  state->isActive = XR_FALSE;
  return XR_SUCCESS;
}

XrResult xrGetActionStatePose_impl(XrSession, const XrActionStateGetInfo*,
                                     XrActionStatePose*) noexcept {
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

XrResult xrApplyHapticFeedback_impl(XrSession sessionHandle,
                                      const XrHapticActionInfo* info,
                                      const XrHapticBaseHeader* base) noexcept {
  if (lookupSession(sessionHandle) == nullptr || info == nullptr ||
      base == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  return XR_SUCCESS;
}

XrResult xrStopHapticFeedback_impl(XrSession sessionHandle,
                                     const XrHapticActionInfo* info) noexcept {
  if (lookupSession(sessionHandle) == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  return XR_SUCCESS;
}

}  // namespace fuvr::runtime
