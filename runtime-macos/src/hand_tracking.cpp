// SPDX-License-Identifier: Apache-2.0
#include <openxr/openxr.h>

#include <mutex>
#include <unordered_map>

#include "fuvr/runtime.hpp"

// XR_EXT_hand_tracking definitions in case the SDK header didn't pull them in.
#ifndef XR_EXT_hand_tracking
#define XR_EXT_hand_tracking 1
#endif

namespace fuvr::runtime {

namespace detail {
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
}  // namespace detail

namespace {

std::unordered_map<uint64_t, HandTracker*>& handTrackerRegistry() noexcept {
  static std::unordered_map<uint64_t, HandTracker*> r;
  return r;
}

HandTracker* lookupHandTracker(uint64_t h) noexcept {
  std::lock_guard<std::mutex> lk(detail::globalMutex());
  auto& m = handTrackerRegistry();
  auto it = m.find(h);
  return it == m.end() ? nullptr : it->second;
}

}  // namespace

extern "C" {

// Forward struct/enum decls — XR_EXT_hand_tracking surface used by the runtime.
typedef struct XrHandTrackerCreateInfoEXT_FUVR {
  XrStructureType type;
  const void* next;
  uint32_t hand;       // XrHandEXT (1=left, 2=right)
  uint32_t handJointSet;
} XrHandTrackerCreateInfoEXT_FUVR;

typedef struct XrHandJointsLocateInfoEXT_FUVR {
  XrStructureType type;
  const void* next;
  XrSpace baseSpace;
  XrTime time;
} XrHandJointsLocateInfoEXT_FUVR;

typedef struct XrHandJointLocationEXT_FUVR {
  uint64_t locationFlags;  // XrSpaceLocationFlags
  XrPosef pose;
  float radius;
} XrHandJointLocationEXT_FUVR;

typedef struct XrHandJointVelocityEXT_FUVR {
  uint64_t velocityFlags;
  XrVector3f linearVelocity;
  XrVector3f angularVelocity;
} XrHandJointVelocityEXT_FUVR;

typedef struct XrHandJointLocationsEXT_FUVR {
  XrStructureType type;
  void* next;
  XrBool32 isActive;
  uint32_t jointCount;
  XrHandJointLocationEXT_FUVR* jointLocations;
} XrHandJointLocationsEXT_FUVR;

typedef struct XrHandJointVelocitiesEXT_FUVR {
  XrStructureType type;
  void* next;
  uint32_t jointCount;
  XrHandJointVelocityEXT_FUVR* jointVelocities;
} XrHandJointVelocitiesEXT_FUVR;

}  // extern "C"

// Constants we care about.
static constexpr uint32_t kHandJointCountExt = 26;

XrResult xrCreateHandTrackerEXT_impl(XrSession sessionHandle, const void* info,
                                      uint64_t* tracker) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || tracker == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  const auto* ci = static_cast<const XrHandTrackerCreateInfoEXT_FUVR*>(info);
  auto ht = std::make_unique<HandTracker>();
  ht->session = s;
  ht->hand = (ci->hand == 2) ? Hand::Right : Hand::Left;
  const uint64_t h = detail::nextHandleAlloc();
  ht->handle = h;
  HandTracker* raw = ht.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    handTrackerRegistry().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(s->mutex);
    s->handTrackers.push_back(std::move(ht));
  }
  *tracker = h;
  return XR_SUCCESS;
}

XrResult xrDestroyHandTrackerEXT_impl(uint64_t tracker) noexcept {
  HandTracker* ht = lookupHandTracker(tracker);
  if (ht == nullptr) return XR_ERROR_HANDLE_INVALID;
  Session* s = ht->session;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    handTrackerRegistry().erase(tracker);
  }
  if (s != nullptr) {
    std::lock_guard<std::mutex> lk(s->mutex);
    for (auto it = s->handTrackers.begin(); it != s->handTrackers.end(); ++it) {
      if (it->get() == ht) {
        s->handTrackers.erase(it);
        break;
      }
    }
  }
  return XR_SUCCESS;
}

XrResult xrLocateHandJointsEXT_impl(uint64_t tracker, const void* locateInfo,
                                     void* locations) noexcept {
  if (lookupHandTracker(tracker) == nullptr || locateInfo == nullptr ||
      locations == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  // Why: hand joint data source not yet wired (q-hand: error-arm workaround
  // arrives in pass 5+). Return inactive joints with all flags clear.
  auto* out = static_cast<XrHandJointLocationsEXT_FUVR*>(locations);
  out->isActive = XR_FALSE;
  if (out->jointLocations != nullptr) {
    const uint32_t n = out->jointCount < kHandJointCountExt
                           ? out->jointCount
                           : kHandJointCountExt;
    for (uint32_t i = 0; i < n; ++i) {
      out->jointLocations[i].locationFlags = 0;
      out->jointLocations[i].pose.position = {0.0f, 0.0f, 0.0f};
      out->jointLocations[i].pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
      out->jointLocations[i].radius = 0.0f;
    }
  }
  // Walk the next chain for velocities and clear them too.
  for (auto* p = static_cast<const XrBaseInStructure*>(out->next);
       p != nullptr; p = p->next) {
    auto* vptr = const_cast<XrBaseInStructure*>(p);
    auto* v = reinterpret_cast<XrHandJointVelocitiesEXT_FUVR*>(vptr);
    if (v->jointVelocities != nullptr) {
      const uint32_t n = v->jointCount < kHandJointCountExt
                             ? v->jointCount
                             : kHandJointCountExt;
      for (uint32_t i = 0; i < n; ++i) {
        v->jointVelocities[i].velocityFlags = 0;
        v->jointVelocities[i].linearVelocity = {0.0f, 0.0f, 0.0f};
        v->jointVelocities[i].angularVelocity = {0.0f, 0.0f, 0.0f};
      }
    }
  }
  return XR_SUCCESS;
}

}  // namespace fuvr::runtime
