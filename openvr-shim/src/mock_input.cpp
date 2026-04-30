// SPDX-License-Identifier: Apache-2.0
//
// MockIVRInput — translates OpenVR's "actions API" (binding-driven input)
// into the daemon's per-handle state stream. Handles are interned client-
// side: the first time the game asks for `/actions/foo/in/bar`, we hash
// it into a 64-bit id and remember the mapping. The daemon receives the
// raw handle in `ActionUpdate` requests.
//
// We do NOT enforce the action-manifest schema (Vivecraft and most legacy
// titles ship one but the daemon already understands the underlying
// digital/analog/pose state). The shim's job is just to keep handles
// stable and shuttle state.

#include <cstring>
#include <vector>

#include "openvr.h"

#include "log.hpp"
#include "mock_state.hpp"
#include "pose_math.hpp"

namespace fuvr::openvr_shim {

namespace {

vr::InputDigitalActionData_t toDigital(const wire::ActionStateEntry& e) {
  vr::InputDigitalActionData_t d{};
  d.bActive   = e.active != 0;
  d.bChanged  = e.changed != 0;
  d.bState    = e.body[0] != 0.0f;
  d.fUpdateTime = 0.0f;
  d.activeOrigin = vr::k_ulInvalidInputValueHandle;
  return d;
}

vr::InputAnalogActionData_t toAnalog(const wire::ActionStateEntry& e) {
  vr::InputAnalogActionData_t a{};
  a.bActive  = e.active != 0;
  // OpenVR's analog action data has no `bChanged`; consumers diff against
  // the previous frame's deltaX/Y/Z themselves.
  a.x = e.body[0];
  a.y = e.body[1];
  a.z = e.body[2];
  a.deltaX = e.body[3];
  a.deltaY = e.body[4];
  a.deltaZ = e.body[5];
  a.fUpdateTime = 0.0f;
  a.activeOrigin = vr::k_ulInvalidInputValueHandle;
  return a;
}

vr::InputPoseActionData_t toPose(const wire::ActionStateEntry& e) {
  vr::InputPoseActionData_t p{};
  p.bActive  = e.active != 0;
  p.activeOrigin = vr::k_ulInvalidInputValueHandle;
  // Reuse pose13→matrix conversion: pack input into a 13-float pose
  // shape (pos+quat+zero-velocity).
  float pose13[13] = {
    e.body[0], e.body[1], e.body[2],
    e.body[3], e.body[4], e.body[5], e.body[6],
    0,0,0, 0,0,0,
  };
  p.pose.bDeviceIsConnected = (e.body[7] != 0.0f);
  p.pose.bPoseIsValid       = (e.body[7] != 0.0f);
  p.pose.eTrackingResult    = (e.body[7] != 0.0f)
      ? vr::TrackingResult_Running_OK : vr::TrackingResult_Uninitialized;
  p.pose.mDeviceToAbsoluteTracking = posemath::matrixFromPose(pose13);
  return p;
}

}  // namespace

class MockIVRInput final : public vr::IVRInput {
 public:
  vr::EVRInputError SetActionManifestPath(const char* path) override {
    if (path) FUVR_LOG("input: action manifest = %s", path);
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetActionSetHandle(const char* name, vr::VRActionSetHandle_t* h) override {
    if (!name || !h) return vr::VRInputError_InvalidParam;
    *h = MockState::shared().internActionHandle(std::string("set:") + name);
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetActionHandle(const char* name, vr::VRActionHandle_t* h) override {
    if (!name || !h) return vr::VRInputError_InvalidParam;
    *h = MockState::shared().internActionHandle(name);
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetInputSourceHandle(const char* name, vr::VRInputValueHandle_t* h) override {
    if (!name || !h) return vr::VRInputError_InvalidParam;
    *h = MockState::shared().internInputSourceHandle(name);
    return vr::VRInputError_None;
  }

  vr::EVRInputError UpdateActionState(vr::VRActiveActionSet_t* sets,
                                      uint32_t /*sizeOf*/,
                                      uint32_t setCount) override {
    // We don't track per-set activation; the daemon always streams every
    // observed device input. Snapshot is held in `lastEntries_`.
    (void)sets;
    (void)setCount;
    if (sets && setCount > 0) {
      handles_.clear();
      handles_.reserve(setCount);
      for (uint32_t i = 0; i < setCount; ++i) handles_.push_back(sets[i].ulActionSet);
    }
    return refresh();
  }

  vr::EVRInputError GetDigitalActionData(vr::VRActionHandle_t a,
                                         vr::InputDigitalActionData_t* out,
                                         uint32_t sz,
                                         vr::VRInputValueHandle_t /*restrict*/) override {
    if (!out || sz < sizeof(*out)) return vr::VRInputError_InvalidParam;
    auto* e = find(a, wire::ActionKind::Digital);
    *out = e ? toDigital(*e) : vr::InputDigitalActionData_t{};
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetAnalogActionData(vr::VRActionHandle_t a,
                                        vr::InputAnalogActionData_t* out,
                                        uint32_t sz,
                                        vr::VRInputValueHandle_t /*restrict*/) override {
    if (!out || sz < sizeof(*out)) return vr::VRInputError_InvalidParam;
    auto* e = find(a, wire::ActionKind::Analog);
    *out = e ? toAnalog(*e) : vr::InputAnalogActionData_t{};
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetPoseActionDataRelativeToNow(vr::VRActionHandle_t a,
                                                   vr::ETrackingUniverseOrigin /*origin*/,
                                                   float /*secondsFromNow*/,
                                                   vr::InputPoseActionData_t* out,
                                                   uint32_t sz,
                                                   vr::VRInputValueHandle_t /*restrict*/) override {
    if (!out || sz < sizeof(*out)) return vr::VRInputError_InvalidParam;
    auto* e = find(a, wire::ActionKind::Pose);
    *out = e ? toPose(*e) : vr::InputPoseActionData_t{};
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetPoseActionDataForNextFrame(vr::VRActionHandle_t a,
                                                  vr::ETrackingUniverseOrigin origin,
                                                  vr::InputPoseActionData_t* out,
                                                  uint32_t sz,
                                                  vr::VRInputValueHandle_t restrict) override {
    return GetPoseActionDataRelativeToNow(a, origin, 0.0f, out, sz, restrict);
  }

  vr::EVRInputError GetSkeletalActionData(vr::VRActionHandle_t,
                                          vr::InputSkeletalActionData_t* out,
                                          uint32_t sz) override {
    if (!out || sz < sizeof(*out)) return vr::VRInputError_InvalidParam;
    *out = vr::InputSkeletalActionData_t{};
    return vr::VRInputError_None;
  }

  vr::EVRInputError GetDominantHand(vr::ETrackedControllerRole* hand) override {
    if (hand) *hand = vr::TrackedControllerRole_RightHand;
    return vr::VRInputError_None;
  }
  vr::EVRInputError SetDominantHand(vr::ETrackedControllerRole) override {
    return vr::VRInputError_None;
  }

  vr::EVRInputError GetEyeTrackingDataRelativeToNow(vr::VRActionHandle_t,
                                                    vr::ETrackingUniverseOrigin,
                                                    float,
                                                    vr::VREyeTrackingData_t*,
                                                    uint32_t) override {
    return vr::VRInputError_NameNotFound;
  }
  vr::EVRInputError GetEyeTrackingDataForNextFrame(vr::VRActionHandle_t,
                                                   vr::ETrackingUniverseOrigin,
                                                   vr::VREyeTrackingData_t*,
                                                   uint32_t) override {
    return vr::VRInputError_NameNotFound;
  }

  // ---- Skeletal stubs ----
  vr::EVRInputError GetBoneCount(vr::VRActionHandle_t, uint32_t* c) override {
    if (c) *c = 0;
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetBoneHierarchy(vr::VRActionHandle_t, vr::BoneIndex_t*, uint32_t) override {
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetBoneName(vr::VRActionHandle_t, vr::BoneIndex_t, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetSkeletalReferenceTransforms(vr::VRActionHandle_t,
                                                   vr::EVRSkeletalTransformSpace,
                                                   vr::EVRSkeletalReferencePose,
                                                   vr::VRBoneTransform_t*, uint32_t) override {
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetSkeletalTrackingLevel(vr::VRActionHandle_t,
                                             vr::EVRSkeletalTrackingLevel* out) override {
    if (out) *out = vr::VRSkeletalTracking_Estimated;
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetSkeletalBoneData(vr::VRActionHandle_t,
                                        vr::EVRSkeletalTransformSpace,
                                        vr::EVRSkeletalMotionRange,
                                        vr::VRBoneTransform_t*, uint32_t) override {
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetSkeletalSummaryData(vr::VRActionHandle_t,
                                           vr::EVRSummaryType,
                                           vr::VRSkeletalSummaryData_t* out) override {
    if (out) std::memset(out, 0, sizeof(*out));
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetSkeletalBoneDataCompressed(vr::VRActionHandle_t,
                                                  vr::EVRSkeletalMotionRange,
                                                  void*, uint32_t,
                                                  uint32_t* req) override {
    if (req) *req = 0;
    return vr::VRInputError_None;
  }
  vr::EVRInputError DecompressSkeletalBoneData(const void*, uint32_t,
                                               vr::EVRSkeletalTransformSpace,
                                               vr::VRBoneTransform_t*, uint32_t) override {
    return vr::VRInputError_None;
  }

  // ---- Haptics ----
  vr::EVRInputError TriggerHapticVibrationAction(vr::VRActionHandle_t action,
                                                 float startSeconds,
                                                 float duration,
                                                 float frequency,
                                                 float amplitude,
                                                 vr::VRInputValueHandle_t restrictDevice) override {
    MockState::shared().rpc().triggerHaptic(restrictDevice ? restrictDevice : action,
                                            startSeconds, duration,
                                            frequency, amplitude);
    return vr::VRInputError_None;
  }

  // ---- Origins / bindings (stubs) ----
  vr::EVRInputError GetActionOrigins(vr::VRActionSetHandle_t, vr::VRActionHandle_t,
                                     vr::VRInputValueHandle_t* arr, uint32_t cnt) override {
    if (arr && cnt > 0) std::memset(arr, 0, cnt * sizeof(*arr));
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetOriginLocalizedName(vr::VRInputValueHandle_t, char* buf,
                                           uint32_t sz, int32_t) override {
    if (buf && sz) buf[0] = '\0';
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetOriginTrackedDeviceInfo(vr::VRInputValueHandle_t,
                                               vr::InputOriginInfo_t* out,
                                               uint32_t sz) override {
    if (out && sz >= sizeof(*out)) std::memset(out, 0, sizeof(*out));
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetActionBindingInfo(vr::VRActionHandle_t,
                                         vr::InputBindingInfo_t*, uint32_t,
                                         uint32_t, uint32_t* returned) override {
    if (returned) *returned = 0;
    return vr::VRInputError_None;
  }
  vr::EVRInputError ShowActionOrigins(vr::VRActionSetHandle_t, vr::VRActionHandle_t) override {
    return vr::VRInputError_None;
  }
  vr::EVRInputError ShowBindingsForActionSet(vr::VRActiveActionSet_t*, uint32_t,
                                             uint32_t, vr::VRInputValueHandle_t) override {
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetComponentStateForBinding(const char*, const char*,
                                                const vr::InputBindingInfo_t*,
                                                uint32_t, uint32_t,
                                                vr::RenderModel_ComponentState_t*) override {
    return vr::VRInputError_None;
  }

  bool IsUsingLegacyInput() override { return false; }

  vr::EVRInputError OpenBindingUI(const char*, vr::VRActionSetHandle_t,
                                  vr::VRInputValueHandle_t, bool) override {
    return vr::VRInputError_None;
  }
  vr::EVRInputError GetBindingVariant(vr::VRInputValueHandle_t, char* buf,
                                      uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return vr::VRInputError_None;
  }

 private:
  vr::EVRInputError refresh() {
    // We pull a single stream of "all known handles" from the daemon
    // each tick. For Vivecraft the working set is small (~20 actions),
    // so cost is in the noise.
    auto& rpc = MockState::shared().rpc();
    if (!rpc.updateActions(handles_, lastEntries_)) {
      return vr::VRInputError_IPCError;
    }
    return vr::VRInputError_None;
  }

  const wire::ActionStateEntry* find(uint64_t handle, wire::ActionKind kind) const {
    for (auto& e : lastEntries_) {
      if (e.handle == handle && e.kind == static_cast<uint8_t>(kind)) return &e;
    }
    return nullptr;
  }

  std::vector<uint64_t> handles_;
  std::vector<wire::ActionStateEntry> lastEntries_;
};

vr::IVRInput* mockIVRInput() {
  static MockIVRInput instance;
  return &instance;
}

}  // namespace fuvr::openvr_shim
