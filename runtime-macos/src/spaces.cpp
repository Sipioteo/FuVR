// SPDX-License-Identifier: Apache-2.0
#include "fuvr/spaces.hpp"

#include <mutex>
#include <unordered_map>

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

  if (sp->kind == SpaceKind::Action) {
    // Why: PoseSnapshot only carries HMD pose today; controller pose
    // forwarding is a pass 4 item. Return success with invalid bits.
    return XR_SUCCESS;
  }
  if (base->kind == SpaceKind::Action) {
    return XR_SUCCESS;
  }

  Session* s = sp->session;
  if (s == nullptr) return XR_ERROR_HANDLE_INVALID;

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
