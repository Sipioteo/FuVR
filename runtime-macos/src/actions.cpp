// SPDX-License-Identifier: Apache-2.0
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "fuvr/path_registry.hpp"
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
  action->subactionPaths.reserve(info->countSubactionPaths);
  for (uint32_t i = 0; i < info->countSubactionPaths; ++i) {
    action->subactionPaths.push_back(info->subactionPaths[i]);
  }
  action->name = info->actionName;
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
  for (uint32_t i = 0; i < bindings->countSuggestedBindings; ++i) {
    const XrActionSuggestedBinding& b = bindings->suggestedBindings[i];
    if (Action* a = lookupAction(b.action)) {
      a->suggestedBindings.push_back(b.binding);
    }
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

namespace {

enum class FieldKind : uint8_t {
  None,
  TriggerValue,
  TriggerClick,
  TriggerTouch,
  SqueezeValue,
  SqueezeClick,
  Thumbstick,
  ThumbstickX,
  ThumbstickY,
  ThumbstickClick,
  ThumbstickTouch,
  Thumbrest,
  AClick,
  ATouch,
  BClick,
  BTouch,
  SystemClick,
  MenuClick,
  SelectClick,
  GripPose,
  AimPose,
  EyeGazePose,
};

struct ResolvedBinding {
  Hand hand{Hand::Left};
  FieldKind field{FieldKind::None};
  bool valid{false};
};

ResolvedBinding parseBindingPath(std::string_view p) {
  ResolvedBinding r{};
  if (p.find("/user/eyes_ext/") != std::string_view::npos) {
    r.field = FieldKind::EyeGazePose;
    r.valid = true;
    return r;
  }
  if (p.find("/user/hand/left") != std::string_view::npos) {
    r.hand = Hand::Left;
  } else if (p.find("/user/hand/right") != std::string_view::npos) {
    r.hand = Hand::Right;
  } else {
    return r;
  }
  // X/Y on left hand (Touch+ controller). Treat as A/B aliases on left.
  if (p.find("/input/trigger/value") != std::string_view::npos)
    r.field = FieldKind::TriggerValue;
  else if (p.find("/input/trigger/click") != std::string_view::npos)
    r.field = FieldKind::TriggerClick;
  else if (p.find("/input/trigger/touch") != std::string_view::npos)
    r.field = FieldKind::TriggerTouch;
  else if (p.find("/input/squeeze/value") != std::string_view::npos)
    r.field = FieldKind::SqueezeValue;
  else if (p.find("/input/squeeze/click") != std::string_view::npos)
    r.field = FieldKind::SqueezeClick;
  else if (p.find("/input/thumbstick/x") != std::string_view::npos)
    r.field = FieldKind::ThumbstickX;
  else if (p.find("/input/thumbstick/y") != std::string_view::npos)
    r.field = FieldKind::ThumbstickY;
  else if (p.find("/input/thumbstick/click") != std::string_view::npos)
    r.field = FieldKind::ThumbstickClick;
  else if (p.find("/input/thumbstick/touch") != std::string_view::npos)
    r.field = FieldKind::ThumbstickTouch;
  else if (p.find("/input/thumbstick") != std::string_view::npos)
    r.field = FieldKind::Thumbstick;
  else if (p.find("/input/thumbrest") != std::string_view::npos)
    r.field = FieldKind::Thumbrest;
  else if (p.find("/input/a/click") != std::string_view::npos ||
           p.find("/input/x/click") != std::string_view::npos)
    r.field = FieldKind::AClick;
  else if (p.find("/input/a/touch") != std::string_view::npos ||
           p.find("/input/x/touch") != std::string_view::npos)
    r.field = FieldKind::ATouch;
  else if (p.find("/input/b/click") != std::string_view::npos ||
           p.find("/input/y/click") != std::string_view::npos)
    r.field = FieldKind::BClick;
  else if (p.find("/input/b/touch") != std::string_view::npos ||
           p.find("/input/y/touch") != std::string_view::npos)
    r.field = FieldKind::BTouch;
  else if (p.find("/input/system/click") != std::string_view::npos)
    r.field = FieldKind::SystemClick;
  else if (p.find("/input/menu/click") != std::string_view::npos)
    r.field = FieldKind::MenuClick;
  else if (p.find("/input/select/click") != std::string_view::npos)
    r.field = FieldKind::SelectClick;
  else if (p.find("/input/grip/pose") != std::string_view::npos)
    r.field = FieldKind::GripPose;
  else if (p.find("/input/aim/pose") != std::string_view::npos)
    r.field = FieldKind::AimPose;
  r.valid = (r.field != FieldKind::None);
  return r;
}

ResolvedBinding resolveAction(const Action& a, XrPath subactionPath) {
  // Prefer a binding whose subaction path matches the requested one.
  Hand preferred = Hand::Left;
  bool preferSet = false;
  if (subactionPath != XR_NULL_PATH) {
    if (auto* s = pathRegistry().lookup(subactionPath)) {
      if (s->find("/user/hand/right") != std::string::npos) {
        preferred = Hand::Right;
        preferSet = true;
      } else if (s->find("/user/hand/left") != std::string::npos) {
        preferred = Hand::Left;
        preferSet = true;
      }
    }
  }
  ResolvedBinding fallback{};
  for (XrPath p : a.suggestedBindings) {
    auto* s = pathRegistry().lookup(p);
    if (s == nullptr) continue;
    ResolvedBinding rb = parseBindingPath(*s);
    if (!rb.valid) continue;
    if (rb.field == FieldKind::EyeGazePose) return rb;
    if (preferSet && rb.hand != preferred) {
      if (!fallback.valid) fallback = rb;
      continue;
    }
    return rb;
  }
  return fallback;
}

const ControllerInputState& handState(const InputSnapshot& snap, Hand h) {
  return h == Hand::Left ? snap.left : snap.right;
}

XrTime fieldChangeTime(const ActionStateCache& c, Hand h, FieldKind f) {
  switch (f) {
    case FieldKind::TriggerValue:
    case FieldKind::TriggerClick:
      return c.triggerChangeTime(h);
    case FieldKind::TriggerTouch: return c.triggerTouchChangeTime(h);
    case FieldKind::SqueezeValue:
    case FieldKind::SqueezeClick: return c.squeezeChangeTime(h);
    case FieldKind::Thumbstick:
    case FieldKind::ThumbstickX:
    case FieldKind::ThumbstickY: return c.thumbstickChangeTime(h);
    case FieldKind::ThumbstickClick: return c.thumbstickClickChangeTime(h);
    case FieldKind::ThumbstickTouch: return c.thumbstickTouchChangeTime(h);
    case FieldKind::Thumbrest: return c.thumbrestChangeTime(h);
    case FieldKind::AClick: return c.aClickChangeTime(h);
    case FieldKind::ATouch: return c.aTouchChangeTime(h);
    case FieldKind::BClick: return c.bClickChangeTime(h);
    case FieldKind::BTouch: return c.bTouchChangeTime(h);
    case FieldKind::SystemClick: return c.systemClickChangeTime(h);
    default: return 0;
  }
}

}  // namespace

XrResult xrGetActionStateBoolean_impl(XrSession sessionHandle,
                                        const XrActionStateGetInfo* info,
                                        XrActionStateBoolean* state) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->currentState = XR_FALSE;
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  state->isActive = XR_FALSE;
  Action* a = nullptr;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    auto& m = detail::actions();
    auto it = m.find(reinterpret_cast<uint64_t>(info->action));
    if (it != m.end()) a = it->second;
  }
  if (a == nullptr) return XR_SUCCESS;
  ResolvedBinding rb = resolveAction(*a, info->subactionPath);
  if (!rb.valid) return XR_SUCCESS;
  InputSnapshot snap = s->actionState.snapshot();
  const auto& cs = handState(snap, rb.hand);
  state->isActive = cs.active ? XR_TRUE : XR_FALSE;
  bool v = false;
  switch (rb.field) {
    case FieldKind::TriggerClick: v = cs.trigger > 0.5f; break;
    case FieldKind::TriggerTouch: v = cs.triggerTouch; break;
    case FieldKind::SqueezeClick: v = cs.squeeze > 0.5f; break;
    case FieldKind::ThumbstickClick: v = cs.thumbstickClick; break;
    case FieldKind::ThumbstickTouch: v = cs.thumbstickTouch; break;
    case FieldKind::AClick: v = cs.buttonAClick; break;
    case FieldKind::ATouch: v = cs.buttonATouch; break;
    case FieldKind::BClick: v = cs.buttonBClick; break;
    case FieldKind::BTouch: v = cs.buttonBTouch; break;
    case FieldKind::SystemClick: v = cs.systemClick; break;
    case FieldKind::MenuClick:
    case FieldKind::SelectClick: v = cs.buttonAClick; break;
    default: break;
  }
  state->currentState = v ? XR_TRUE : XR_FALSE;
  state->lastChangeTime = fieldChangeTime(s->actionState, rb.hand, rb.field);
  return XR_SUCCESS;
}

XrResult xrGetActionStateFloat_impl(XrSession sessionHandle,
                                      const XrActionStateGetInfo* info,
                                      XrActionStateFloat* state) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->currentState = 0.0f;
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  state->isActive = XR_FALSE;
  Action* a = nullptr;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    auto& m = detail::actions();
    auto it = m.find(reinterpret_cast<uint64_t>(info->action));
    if (it != m.end()) a = it->second;
  }
  if (a == nullptr) return XR_SUCCESS;
  ResolvedBinding rb = resolveAction(*a, info->subactionPath);
  if (!rb.valid) return XR_SUCCESS;
  InputSnapshot snap = s->actionState.snapshot();
  const auto& cs = handState(snap, rb.hand);
  state->isActive = cs.active ? XR_TRUE : XR_FALSE;
  switch (rb.field) {
    case FieldKind::TriggerValue: state->currentState = cs.trigger; break;
    case FieldKind::SqueezeValue: state->currentState = cs.squeeze; break;
    case FieldKind::ThumbstickX: state->currentState = cs.thumbstickX; break;
    case FieldKind::ThumbstickY: state->currentState = cs.thumbstickY; break;
    case FieldKind::Thumbrest: state->currentState = cs.thumbrest; break;
    default: break;
  }
  state->lastChangeTime = fieldChangeTime(s->actionState, rb.hand, rb.field);
  return XR_SUCCESS;
}

XrResult xrGetActionStateVector2f_impl(XrSession sessionHandle,
                                         const XrActionStateGetInfo* info,
                                         XrActionStateVector2f* state) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->currentState = {0.0f, 0.0f};
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  state->isActive = XR_FALSE;
  Action* a = nullptr;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    auto& m = detail::actions();
    auto it = m.find(reinterpret_cast<uint64_t>(info->action));
    if (it != m.end()) a = it->second;
  }
  if (a == nullptr) return XR_SUCCESS;
  ResolvedBinding rb = resolveAction(*a, info->subactionPath);
  if (!rb.valid) return XR_SUCCESS;
  InputSnapshot snap = s->actionState.snapshot();
  const auto& cs = handState(snap, rb.hand);
  state->isActive = cs.active ? XR_TRUE : XR_FALSE;
  if (rb.field == FieldKind::Thumbstick || rb.field == FieldKind::ThumbstickX ||
      rb.field == FieldKind::ThumbstickY) {
    state->currentState = {cs.thumbstickX, cs.thumbstickY};
  }
  state->lastChangeTime = fieldChangeTime(s->actionState, rb.hand, rb.field);
  return XR_SUCCESS;
}

XrResult xrGetActionStatePose_impl(XrSession sessionHandle,
                                     const XrActionStateGetInfo* info,
                                     XrActionStatePose* state) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  state->isActive = XR_FALSE;
  Action* a = nullptr;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    auto& m = detail::actions();
    auto it = m.find(reinterpret_cast<uint64_t>(info->action));
    if (it != m.end()) a = it->second;
  }
  if (a == nullptr) return XR_SUCCESS;
  ResolvedBinding rb = resolveAction(*a, info->subactionPath);
  if (!rb.valid) return XR_SUCCESS;
  if (rb.field == FieldKind::EyeGazePose) {
    // Eye gaze data source not wired yet. isActive=false.
    return XR_SUCCESS;
  }
  if (rb.field == FieldKind::GripPose || rb.field == FieldKind::AimPose) {
    auto latest = s->predictor.latest();
    if (!latest.has_value()) return XR_SUCCESS;
    state->isActive = (rb.hand == Hand::Left ? latest->leftControllerActive
                                              : latest->rightControllerActive)
                          ? XR_TRUE
                          : XR_FALSE;
  }
  return XR_SUCCESS;
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
