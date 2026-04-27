// SPDX-License-Identifier: Apache-2.0
#include "fuvr/spaces.hpp"

#include <mutex>
#include <unordered_map>

#include "fuvr/path_registry.hpp"
#include "fuvr/runtime.hpp"

namespace fuvr::runtime {

namespace detail {
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
std::unordered_map<uint64_t, Action*>& actions() noexcept;
}  // namespace detail

namespace {

std::unordered_map<uint64_t, Space*>& spaceRegistry() noexcept {
  static std::unordered_map<uint64_t, Space*> r;
  return r;
}

Action* lookupActionLocal(XrAction h) noexcept {
  std::lock_guard<std::mutex> lk(detail::globalMutex());
  auto& m = detail::actions();
  auto it = m.find(reinterpret_cast<uint64_t>(h));
  return it == m.end() ? nullptr : it->second;
}

}  // namespace

Space* lookupSpace(XrSpace handle) noexcept {
  std::lock_guard<std::mutex> lk(detail::globalMutex());
  auto it = spaceRegistry().find(reinterpret_cast<uint64_t>(handle));
  return it == spaceRegistry().end() ? nullptr : it->second;
}

namespace {

Pose composePoseInRef(const Pose& base, const XrPosef& offset) noexcept {
  Pose out{};
  out.position = {base.position.x + offset.position.x,
                  base.position.y + offset.position.y,
                  base.position.z + offset.position.z};
  out.orientation = {offset.orientation.x, offset.orientation.y,
                     offset.orientation.z, offset.orientation.w};
  return out;
}

}  // namespace

XrResult xrCreateReferenceSpace_impl(XrSession sessionHandle,
                                      const XrReferenceSpaceCreateInfo* info,
                                      XrSpace* out) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  SpaceKind kind;
  switch (info->referenceSpaceType) {
    case XR_REFERENCE_SPACE_TYPE_VIEW:
      kind = SpaceKind::ReferenceView;
      break;
    case XR_REFERENCE_SPACE_TYPE_LOCAL:
      kind = SpaceKind::ReferenceLocal;
      break;
    case XR_REFERENCE_SPACE_TYPE_STAGE:
      kind = SpaceKind::ReferenceStage;
      break;
    default:
      return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
  }
  auto sp = std::make_unique<Space>();
  sp->session = s;
  sp->kind = kind;
  Pose base{};
  sp->poseInRef = composePoseInRef(base, info->poseInReferenceSpace);
  const uint64_t h = detail::nextHandleAlloc();
  sp->handle = reinterpret_cast<XrSpace>(h);
  Space* raw = sp.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    spaceRegistry().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(s->mutex);
    s->spaces.push_back(std::move(sp));
  }
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrCreateActionSpace_impl(XrSession sessionHandle,
                                   const XrActionSpaceCreateInfo* info,
                                   XrSpace* out) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  auto sp = std::make_unique<Space>();
  sp->session = s;
  sp->kind = SpaceKind::Action;
  sp->action = lookupActionLocal(info->action);
  sp->subactionPath = info->subactionPath;
  // Why: derive hand from the subactionPath string. If absent, fall back to
  // the action's first subactionPath. /eyes_ext/ marks the eye gaze space.
  auto resolveHand = [](XrPath p) -> ActionHand {
    if (p == XR_NULL_PATH) return ActionHand::Unknown;
    auto* str = pathRegistry().lookup(p);
    if (str == nullptr) return ActionHand::Unknown;
    if (str->find("/user/hand/left") != std::string::npos)
      return ActionHand::Left;
    if (str->find("/user/hand/right") != std::string::npos)
      return ActionHand::Right;
    return ActionHand::Unknown;
  };
  sp->actionHand = resolveHand(info->subactionPath);
  if (sp->actionHand == ActionHand::Unknown && sp->action != nullptr) {
    for (XrPath p : sp->action->subactionPaths) {
      ActionHand h = resolveHand(p);
      if (h != ActionHand::Unknown) {
        sp->actionHand = h;
        break;
      }
    }
  }
  if (info->subactionPath != XR_NULL_PATH) {
    auto* str = pathRegistry().lookup(info->subactionPath);
    if (str != nullptr && str->find("/user/eyes_ext") != std::string::npos) {
      sp->isEyeGaze = true;
    }
  }
  Pose base{};
  sp->poseInRef = composePoseInRef(base, info->poseInActionSpace);
  const uint64_t h = detail::nextHandleAlloc();
  sp->handle = reinterpret_cast<XrSpace>(h);
  Space* raw = sp.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    spaceRegistry().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(s->mutex);
    s->spaces.push_back(std::move(sp));
  }
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrDestroySpace_impl(XrSpace handle) noexcept {
  Space* sp = lookupSpace(handle);
  if (sp == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  Session* s = sp->session;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    spaceRegistry().erase(reinterpret_cast<uint64_t>(handle));
  }
  if (s != nullptr) {
    std::lock_guard<std::mutex> lk(s->mutex);
    for (auto it = s->spaces.begin(); it != s->spaces.end(); ++it) {
      if (it->get() == sp) {
        s->spaces.erase(it);
        break;
      }
    }
  }
  return XR_SUCCESS;
}

XrResult xrLocateSpace_impl(XrSpace spaceHandle, XrSpace baseHandle, XrTime time,
                             XrSpaceLocation* loc) noexcept {
  Space* sp = lookupSpace(spaceHandle);
  Space* base = lookupSpace(baseHandle);
  if (sp == nullptr || base == nullptr || loc == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  loc->locationFlags = 0;
  loc->pose.position = {0.0f, 0.0f, 0.0f};
  loc->pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};

  Session* s = sp->session;
  if (s == nullptr) return XR_ERROR_HANDLE_INVALID;

  // Eye gaze: data source not yet wired; return invalid bits per spec.
  if (sp->isEyeGaze || (base->kind == SpaceKind::Action && base->isEyeGaze)) {
    return XR_SUCCESS;
  }

  auto controllerPoseFromSample = [&](const Space* a, Pose& out, bool& tracked) {
    auto latest = s->predictor.latest();
    if (!latest.has_value()) {
      tracked = false;
      return false;
    }
    if (a->actionHand == ActionHand::Left) {
      tracked = latest->leftControllerActive;
      out = latest->leftController;
      return tracked;
    }
    if (a->actionHand == ActionHand::Right) {
      tracked = latest->rightControllerActive;
      out = latest->rightController;
      return tracked;
    }
    tracked = false;
    return false;
  };

  // Action-space subject or base: blend controller pose with reference space.
  if (sp->kind == SpaceKind::Action || base->kind == SpaceKind::Action) {
    Pose subject{};
    Pose ref{};
    bool subjectTracked = true;
    bool baseTracked = true;
    if (sp->kind == SpaceKind::Action) {
      if (!controllerPoseFromSample(sp, subject, subjectTracked)) {
        return XR_SUCCESS;
      }
    }
    if (base->kind == SpaceKind::Action) {
      if (!controllerPoseFromSample(base, ref, baseTracked)) {
        return XR_SUCCESS;
      }
    }
    if (sp->kind != SpaceKind::Action) {
      if (sp->kind == SpaceKind::ReferenceLocal) subject = s->localOriginPose;
    }
    if (base->kind != SpaceKind::Action) {
      if (base->kind == SpaceKind::ReferenceLocal) ref = s->localOriginPose;
    }
    loc->pose.position = {subject.position.x - ref.position.x +
                              sp->poseInRef.position.x -
                              base->poseInRef.position.x,
                          subject.position.y - ref.position.y +
                              sp->poseInRef.position.y -
                              base->poseInRef.position.y,
                          subject.position.z - ref.position.z +
                              sp->poseInRef.position.z -
                              base->poseInRef.position.z};
    loc->pose.orientation = {subject.orientation.x, subject.orientation.y,
                             subject.orientation.z, subject.orientation.w};
    loc->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                         XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (subjectTracked && baseTracked) {
      loc->locationFlags |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    }
    return XR_SUCCESS;
  }

  Pose subjectPose{};
  Pose basePose{};

  auto resolveRef = [&](SpaceKind k, Pose& outPose) {
    switch (k) {
      case SpaceKind::ReferenceLocal:
        outPose = s->localOriginPose;
        return true;
      case SpaceKind::ReferenceStage:
        outPose = Pose{};
        return true;
      case SpaceKind::ReferenceView: {
        auto pred = s->predictor.predict(static_cast<uint64_t>(time));
        if (!pred.has_value()) {
          outPose = Pose{};
          return false;
        }
        const Pose& l = pred->leftEye;
        const Pose& r = pred->rightEye;
        outPose.position = {0.5f * (l.position.x + r.position.x),
                            0.5f * (l.position.y + r.position.y),
                            0.5f * (l.position.z + r.position.z)};
        outPose.orientation = l.orientation;
        return true;
      }
      default:
        return false;
    }
  };

  bool subjectTracked = resolveRef(sp->kind, subjectPose);
  bool baseTracked = resolveRef(base->kind, basePose);

  loc->pose.position = {subjectPose.position.x - basePose.position.x +
                            sp->poseInRef.position.x - base->poseInRef.position.x,
                        subjectPose.position.y - basePose.position.y +
                            sp->poseInRef.position.y - base->poseInRef.position.y,
                        subjectPose.position.z - basePose.position.z +
                            sp->poseInRef.position.z - base->poseInRef.position.z};
  loc->pose.orientation = {subjectPose.orientation.x, subjectPose.orientation.y,
                           subjectPose.orientation.z, subjectPose.orientation.w};

  loc->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if (subjectTracked && baseTracked) {
    loc->locationFlags |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                          XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
  }
  return XR_SUCCESS;
}

}  // namespace fuvr::runtime
