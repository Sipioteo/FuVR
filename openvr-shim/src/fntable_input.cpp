// SPDX-License-Identifier: Apache-2.0
//
// LWJGL FnTable wrappers for IVRInput. Order MUST match openvr.h
// `class IVRInput` declaration order.

#include "openvr.h"

namespace fuvr::openvr_shim {
vr::IVRInput* mockIVRInput();
}

extern "C" {

static vr::EVRInputError fnt_Input_SetActionManifestPath(const char* pchActionManifestPath) {
  return fuvr::openvr_shim::mockIVRInput()->SetActionManifestPath(pchActionManifestPath);
}

static vr::EVRInputError fnt_Input_GetActionSetHandle(const char* pchActionSetName, vr::VRActionSetHandle_t* pHandle) {
  return fuvr::openvr_shim::mockIVRInput()->GetActionSetHandle(pchActionSetName, pHandle);
}

static vr::EVRInputError fnt_Input_GetActionHandle(const char* pchActionName, vr::VRActionHandle_t* pHandle) {
  return fuvr::openvr_shim::mockIVRInput()->GetActionHandle(pchActionName, pHandle);
}

static vr::EVRInputError fnt_Input_GetInputSourceHandle(const char* pchInputSourcePath, vr::VRInputValueHandle_t* pHandle) {
  return fuvr::openvr_shim::mockIVRInput()->GetInputSourceHandle(pchInputSourcePath, pHandle);
}

static vr::EVRInputError fnt_Input_UpdateActionState(vr::VRActiveActionSet_t* pSets, uint32_t unSizeOfVRSelectedActionSet_t, uint32_t unSetCount) {
  return fuvr::openvr_shim::mockIVRInput()->UpdateActionState(pSets, unSizeOfVRSelectedActionSet_t, unSetCount);
}

static vr::EVRInputError fnt_Input_GetDigitalActionData(vr::VRActionHandle_t action, vr::InputDigitalActionData_t* pActionData, uint32_t unActionDataSize, vr::VRInputValueHandle_t ulRestrictToDevice) {
  return fuvr::openvr_shim::mockIVRInput()->GetDigitalActionData(action, pActionData, unActionDataSize, ulRestrictToDevice);
}

static vr::EVRInputError fnt_Input_GetAnalogActionData(vr::VRActionHandle_t action, vr::InputAnalogActionData_t* pActionData, uint32_t unActionDataSize, vr::VRInputValueHandle_t ulRestrictToDevice) {
  return fuvr::openvr_shim::mockIVRInput()->GetAnalogActionData(action, pActionData, unActionDataSize, ulRestrictToDevice);
}

static vr::EVRInputError fnt_Input_GetPoseActionDataRelativeToNow(vr::VRActionHandle_t action, vr::ETrackingUniverseOrigin eOrigin, float fPredictedSecondsFromNow, vr::InputPoseActionData_t* pActionData, uint32_t unActionDataSize, vr::VRInputValueHandle_t ulRestrictToDevice) {
  return fuvr::openvr_shim::mockIVRInput()->GetPoseActionDataRelativeToNow(action, eOrigin, fPredictedSecondsFromNow, pActionData, unActionDataSize, ulRestrictToDevice);
}

static vr::EVRInputError fnt_Input_GetPoseActionDataForNextFrame(vr::VRActionHandle_t action, vr::ETrackingUniverseOrigin eOrigin, vr::InputPoseActionData_t* pActionData, uint32_t unActionDataSize, vr::VRInputValueHandle_t ulRestrictToDevice) {
  return fuvr::openvr_shim::mockIVRInput()->GetPoseActionDataForNextFrame(action, eOrigin, pActionData, unActionDataSize, ulRestrictToDevice);
}

static vr::EVRInputError fnt_Input_GetSkeletalActionData(vr::VRActionHandle_t action, vr::InputSkeletalActionData_t* pActionData, uint32_t unActionDataSize) {
  return fuvr::openvr_shim::mockIVRInput()->GetSkeletalActionData(action, pActionData, unActionDataSize);
}

static vr::EVRInputError fnt_Input_GetDominantHand(vr::ETrackedControllerRole* peDominantHand) {
  return fuvr::openvr_shim::mockIVRInput()->GetDominantHand(peDominantHand);
}

static vr::EVRInputError fnt_Input_SetDominantHand(vr::ETrackedControllerRole eDominantHand) {
  return fuvr::openvr_shim::mockIVRInput()->SetDominantHand(eDominantHand);
}

static vr::EVRInputError fnt_Input_GetBoneCount(vr::VRActionHandle_t action, uint32_t* pBoneCount) {
  return fuvr::openvr_shim::mockIVRInput()->GetBoneCount(action, pBoneCount);
}

static vr::EVRInputError fnt_Input_GetBoneHierarchy(vr::VRActionHandle_t action, vr::BoneIndex_t* pParentIndices, uint32_t unIndexArayCount) {
  return fuvr::openvr_shim::mockIVRInput()->GetBoneHierarchy(action, pParentIndices, unIndexArayCount);
}

static vr::EVRInputError fnt_Input_GetBoneName(vr::VRActionHandle_t action, vr::BoneIndex_t nBoneIndex, char* pchBoneName, uint32_t unNameBufferSize) {
  return fuvr::openvr_shim::mockIVRInput()->GetBoneName(action, nBoneIndex, pchBoneName, unNameBufferSize);
}

static vr::EVRInputError fnt_Input_GetSkeletalReferenceTransforms(vr::VRActionHandle_t action, vr::EVRSkeletalTransformSpace eTransformSpace, vr::EVRSkeletalReferencePose eReferencePose, vr::VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount) {
  return fuvr::openvr_shim::mockIVRInput()->GetSkeletalReferenceTransforms(action, eTransformSpace, eReferencePose, pTransformArray, unTransformArrayCount);
}

static vr::EVRInputError fnt_Input_GetSkeletalTrackingLevel(vr::VRActionHandle_t action, vr::EVRSkeletalTrackingLevel* pSkeletalTrackingLevel) {
  return fuvr::openvr_shim::mockIVRInput()->GetSkeletalTrackingLevel(action, pSkeletalTrackingLevel);
}

static vr::EVRInputError fnt_Input_GetSkeletalBoneData(vr::VRActionHandle_t action, vr::EVRSkeletalTransformSpace eTransformSpace, vr::EVRSkeletalMotionRange eMotionRange, vr::VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount) {
  return fuvr::openvr_shim::mockIVRInput()->GetSkeletalBoneData(action, eTransformSpace, eMotionRange, pTransformArray, unTransformArrayCount);
}

static vr::EVRInputError fnt_Input_GetSkeletalSummaryData(vr::VRActionHandle_t action, vr::EVRSummaryType eSummaryType, vr::VRSkeletalSummaryData_t* pSkeletalSummaryData) {
  return fuvr::openvr_shim::mockIVRInput()->GetSkeletalSummaryData(action, eSummaryType, pSkeletalSummaryData);
}

static vr::EVRInputError fnt_Input_GetSkeletalBoneDataCompressed(vr::VRActionHandle_t action, vr::EVRSkeletalMotionRange eMotionRange, void* pvCompressedData, uint32_t unCompressedSize, uint32_t* punRequiredCompressedSize) {
  return fuvr::openvr_shim::mockIVRInput()->GetSkeletalBoneDataCompressed(action, eMotionRange, pvCompressedData, unCompressedSize, punRequiredCompressedSize);
}

static vr::EVRInputError fnt_Input_DecompressSkeletalBoneData(const void* pvCompressedBuffer, uint32_t unCompressedBufferSize, vr::EVRSkeletalTransformSpace eTransformSpace, vr::VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount) {
  return fuvr::openvr_shim::mockIVRInput()->DecompressSkeletalBoneData(pvCompressedBuffer, unCompressedBufferSize, eTransformSpace, pTransformArray, unTransformArrayCount);
}

static vr::EVRInputError fnt_Input_TriggerHapticVibrationAction(vr::VRActionHandle_t action, float fStartSecondsFromNow, float fDurationSeconds, float fFrequency, float fAmplitude, vr::VRInputValueHandle_t ulRestrictToDevice) {
  return fuvr::openvr_shim::mockIVRInput()->TriggerHapticVibrationAction(action, fStartSecondsFromNow, fDurationSeconds, fFrequency, fAmplitude, ulRestrictToDevice);
}

static vr::EVRInputError fnt_Input_GetActionOrigins(vr::VRActionSetHandle_t actionSetHandle, vr::VRActionHandle_t digitalActionHandle, vr::VRInputValueHandle_t* originsOut, uint32_t originOutCount) {
  return fuvr::openvr_shim::mockIVRInput()->GetActionOrigins(actionSetHandle, digitalActionHandle, originsOut, originOutCount);
}

static vr::EVRInputError fnt_Input_GetOriginLocalizedName(vr::VRInputValueHandle_t origin, char* pchNameArray, uint32_t unNameArraySize, int32_t unStringSectionsToInclude) {
  return fuvr::openvr_shim::mockIVRInput()->GetOriginLocalizedName(origin, pchNameArray, unNameArraySize, unStringSectionsToInclude);
}

static vr::EVRInputError fnt_Input_GetOriginTrackedDeviceInfo(vr::VRInputValueHandle_t origin, vr::InputOriginInfo_t* pOriginInfo, uint32_t unOriginInfoSize) {
  return fuvr::openvr_shim::mockIVRInput()->GetOriginTrackedDeviceInfo(origin, pOriginInfo, unOriginInfoSize);
}

static vr::EVRInputError fnt_Input_GetActionBindingInfo(vr::VRActionHandle_t action, vr::InputBindingInfo_t* pOriginInfo, uint32_t unBindingInfoSize, uint32_t unBindingInfoCount, uint32_t* punReturnedBindingInfoCount) {
  return fuvr::openvr_shim::mockIVRInput()->GetActionBindingInfo(action, pOriginInfo, unBindingInfoSize, unBindingInfoCount, punReturnedBindingInfoCount);
}

static vr::EVRInputError fnt_Input_ShowActionOrigins(vr::VRActionSetHandle_t actionSetHandle, vr::VRActionHandle_t ulActionHandle) {
  return fuvr::openvr_shim::mockIVRInput()->ShowActionOrigins(actionSetHandle, ulActionHandle);
}

static vr::EVRInputError fnt_Input_ShowBindingsForActionSet(vr::VRActiveActionSet_t* pSets, uint32_t unSizeOfVRSelectedActionSet_t, uint32_t unSetCount, vr::VRInputValueHandle_t originToHighlight) {
  return fuvr::openvr_shim::mockIVRInput()->ShowBindingsForActionSet(pSets, unSizeOfVRSelectedActionSet_t, unSetCount, originToHighlight);
}

static vr::EVRInputError fnt_Input_GetComponentStateForBinding(const char* pchRenderModelName, const char* pchComponentName, const vr::InputBindingInfo_t* pOriginInfo, uint32_t unBindingInfoSize, uint32_t unBindingInfoCount, vr::RenderModel_ComponentState_t* pComponentState) {
  return fuvr::openvr_shim::mockIVRInput()->GetComponentStateForBinding(pchRenderModelName, pchComponentName, pOriginInfo, unBindingInfoSize, unBindingInfoCount, pComponentState);
}

static vr::EVRInputError fnt_Input_OpenBindingUI(const char* pchAppKey, vr::VRActionSetHandle_t ulActionSetHandle, vr::VRInputValueHandle_t ulDeviceHandle, bool bShowOnDesktop) {
  return fuvr::openvr_shim::mockIVRInput()->OpenBindingUI(pchAppKey, ulActionSetHandle, ulDeviceHandle, bShowOnDesktop);
}

static vr::EVRInputError fnt_Input_GetBindingVariant(vr::VRInputValueHandle_t ulDevicePath, char* pchVariantArray, uint32_t unVariantArraySize) {
  return fuvr::openvr_shim::mockIVRInput()->GetBindingVariant(ulDevicePath, pchVariantArray, unVariantArraySize);
}

}  // extern "C"

namespace fuvr::openvr_shim {
namespace {
struct InputFnTable {
  void* SetActionManifestPath;
  void* GetActionSetHandle;
  void* GetActionHandle;
  void* GetInputSourceHandle;
  void* UpdateActionState;
  void* GetDigitalActionData;
  void* GetAnalogActionData;
  void* GetPoseActionDataRelativeToNow;
  void* GetPoseActionDataForNextFrame;
  void* GetSkeletalActionData;
  void* GetDominantHand;
  void* SetDominantHand;
  void* GetBoneCount;
  void* GetBoneHierarchy;
  void* GetBoneName;
  void* GetSkeletalReferenceTransforms;
  void* GetSkeletalTrackingLevel;
  void* GetSkeletalBoneData;
  void* GetSkeletalSummaryData;
  void* GetSkeletalBoneDataCompressed;
  void* DecompressSkeletalBoneData;
  void* TriggerHapticVibrationAction;
  void* GetActionOrigins;
  void* GetOriginLocalizedName;
  void* GetOriginTrackedDeviceInfo;
  void* GetActionBindingInfo;
  void* ShowActionOrigins;
  void* ShowBindingsForActionSet;
  void* GetComponentStateForBinding;
  void* OpenBindingUI;
  void* GetBindingVariant;
};
static const InputFnTable g_input_fntable = {
  reinterpret_cast<void*>(&fnt_Input_SetActionManifestPath),
  reinterpret_cast<void*>(&fnt_Input_GetActionSetHandle),
  reinterpret_cast<void*>(&fnt_Input_GetActionHandle),
  reinterpret_cast<void*>(&fnt_Input_GetInputSourceHandle),
  reinterpret_cast<void*>(&fnt_Input_UpdateActionState),
  reinterpret_cast<void*>(&fnt_Input_GetDigitalActionData),
  reinterpret_cast<void*>(&fnt_Input_GetAnalogActionData),
  reinterpret_cast<void*>(&fnt_Input_GetPoseActionDataRelativeToNow),
  reinterpret_cast<void*>(&fnt_Input_GetPoseActionDataForNextFrame),
  reinterpret_cast<void*>(&fnt_Input_GetSkeletalActionData),
  reinterpret_cast<void*>(&fnt_Input_GetDominantHand),
  reinterpret_cast<void*>(&fnt_Input_SetDominantHand),
  reinterpret_cast<void*>(&fnt_Input_GetBoneCount),
  reinterpret_cast<void*>(&fnt_Input_GetBoneHierarchy),
  reinterpret_cast<void*>(&fnt_Input_GetBoneName),
  reinterpret_cast<void*>(&fnt_Input_GetSkeletalReferenceTransforms),
  reinterpret_cast<void*>(&fnt_Input_GetSkeletalTrackingLevel),
  reinterpret_cast<void*>(&fnt_Input_GetSkeletalBoneData),
  reinterpret_cast<void*>(&fnt_Input_GetSkeletalSummaryData),
  reinterpret_cast<void*>(&fnt_Input_GetSkeletalBoneDataCompressed),
  reinterpret_cast<void*>(&fnt_Input_DecompressSkeletalBoneData),
  reinterpret_cast<void*>(&fnt_Input_TriggerHapticVibrationAction),
  reinterpret_cast<void*>(&fnt_Input_GetActionOrigins),
  reinterpret_cast<void*>(&fnt_Input_GetOriginLocalizedName),
  reinterpret_cast<void*>(&fnt_Input_GetOriginTrackedDeviceInfo),
  reinterpret_cast<void*>(&fnt_Input_GetActionBindingInfo),
  reinterpret_cast<void*>(&fnt_Input_ShowActionOrigins),
  reinterpret_cast<void*>(&fnt_Input_ShowBindingsForActionSet),
  reinterpret_cast<void*>(&fnt_Input_GetComponentStateForBinding),
  reinterpret_cast<void*>(&fnt_Input_OpenBindingUI),
  reinterpret_cast<void*>(&fnt_Input_GetBindingVariant),
};
}  // namespace
void* inputFnTable() { return const_cast<void*>(static_cast<const void*>(&g_input_fntable)); }
}  // namespace fuvr::openvr_shim
