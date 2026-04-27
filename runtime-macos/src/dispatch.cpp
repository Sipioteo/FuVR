// SPDX-License-Identifier: Apache-2.0
#include <cstring>

#include "fuvr/runtime.hpp"

namespace fuvr::runtime {

extern XrResult xrCreateInstance_impl(const XrInstanceCreateInfo*, XrInstance*) noexcept;
extern XrResult xrDestroyInstance_impl(XrInstance) noexcept;
extern XrResult xrGetInstanceProperties_impl(XrInstance, XrInstanceProperties*) noexcept;
extern XrResult xrEnumerateInstanceExtensionProperties_impl(
    const char*, uint32_t, uint32_t*, XrExtensionProperties*) noexcept;
extern XrResult xrGetSystem_impl(XrInstance, const XrSystemGetInfo*, XrSystemId*) noexcept;
extern XrResult xrGetSystemProperties_impl(XrInstance, XrSystemId, XrSystemProperties*) noexcept;
extern XrResult xrEnumerateViewConfigurations_impl(
    XrInstance, XrSystemId, uint32_t, uint32_t*, XrViewConfigurationType*) noexcept;
extern XrResult xrEnumerateViewConfigurationViews_impl(
    XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t*,
    XrViewConfigurationView*) noexcept;
extern XrResult xrEnumerateEnvironmentBlendModes_impl(
    XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t*,
    XrEnvironmentBlendMode*) noexcept;
extern XrResult xrGetMetalGraphicsRequirementsKHR_impl(
    XrInstance, XrSystemId, XrGraphicsRequirementsMetalKHR*) noexcept;
extern XrResult xrGetReferenceSpaceBoundsRect_impl(
    XrSession, XrReferenceSpaceType, XrExtent2Df*) noexcept;
extern XrResult xrPollEvent_impl(XrInstance, XrEventDataBuffer*) noexcept;
extern XrResult xrResultToString_impl(XrInstance, XrResult, char*) noexcept;
extern XrResult xrStructureTypeToString_impl(XrInstance, XrStructureType, char*) noexcept;

extern XrResult xrCreateSession_impl(XrInstance, const XrSessionCreateInfo*, XrSession*) noexcept;
extern XrResult xrDestroySession_impl(XrSession) noexcept;
extern XrResult xrBeginSession_impl(XrSession, const XrSessionBeginInfo*) noexcept;
extern XrResult xrEndSession_impl(XrSession) noexcept;
extern XrResult xrRequestExitSession_impl(XrSession) noexcept;
extern XrResult xrWaitFrame_impl(XrSession, const XrFrameWaitInfo*, XrFrameState*) noexcept;
extern XrResult xrBeginFrame_impl(XrSession, const XrFrameBeginInfo*) noexcept;
extern XrResult xrEndFrame_impl(XrSession, const XrFrameEndInfo*) noexcept;
extern XrResult xrLocateViews_impl(XrSession, const XrViewLocateInfo*, XrViewState*,
                                    uint32_t, uint32_t*, XrView*) noexcept;
extern XrResult xrCreateReferenceSpace_impl(XrSession, const XrReferenceSpaceCreateInfo*,
                                             XrSpace*) noexcept;
extern XrResult xrDestroySpace_impl(XrSpace) noexcept;
extern XrResult xrLocateSpace_impl(XrSpace, XrSpace, XrTime, XrSpaceLocation*) noexcept;
extern XrResult xrEnumerateReferenceSpaces_impl(
    XrSession, uint32_t, uint32_t*, XrReferenceSpaceType*) noexcept;

extern XrResult xrEnumerateSwapchainFormats_impl(XrSession, uint32_t, uint32_t*,
                                                  int64_t*) noexcept;
extern XrResult xrCreateSwapchain_impl(XrSession, const XrSwapchainCreateInfo*,
                                        XrSwapchain*) noexcept;
extern XrResult xrDestroySwapchain_impl(XrSwapchain) noexcept;
extern XrResult xrEnumerateSwapchainImages_impl(XrSwapchain, uint32_t, uint32_t*,
                                                 XrSwapchainImageBaseHeader*) noexcept;
extern XrResult xrAcquireSwapchainImage_impl(XrSwapchain, const XrSwapchainImageAcquireInfo*,
                                              uint32_t*) noexcept;
extern XrResult xrWaitSwapchainImage_impl(XrSwapchain,
                                           const XrSwapchainImageWaitInfo*) noexcept;
extern XrResult xrReleaseSwapchainImage_impl(XrSwapchain,
                                              const XrSwapchainImageReleaseInfo*) noexcept;

extern XrResult xrCreateActionSet_impl(XrInstance, const XrActionSetCreateInfo*,
                                        XrActionSet*) noexcept;
extern XrResult xrDestroyActionSet_impl(XrActionSet) noexcept;
extern XrResult xrCreateAction_impl(XrActionSet, const XrActionCreateInfo*, XrAction*) noexcept;
extern XrResult xrDestroyAction_impl(XrAction) noexcept;
extern XrResult xrSuggestInteractionProfileBindings_impl(
    XrInstance, const XrInteractionProfileSuggestedBinding*) noexcept;
extern XrResult xrAttachSessionActionSets_impl(
    XrSession, const XrSessionActionSetsAttachInfo*) noexcept;
extern XrResult xrSyncActions_impl(XrSession, const XrActionsSyncInfo*) noexcept;
extern XrResult xrGetActionStateBoolean_impl(XrSession, const XrActionStateGetInfo*,
                                              XrActionStateBoolean*) noexcept;
extern XrResult xrGetActionStateFloat_impl(XrSession, const XrActionStateGetInfo*,
                                            XrActionStateFloat*) noexcept;
extern XrResult xrGetActionStateVector2f_impl(XrSession, const XrActionStateGetInfo*,
                                               XrActionStateVector2f*) noexcept;
extern XrResult xrGetActionStatePose_impl(XrSession, const XrActionStateGetInfo*,
                                           XrActionStatePose*) noexcept;
extern XrResult xrApplyHapticFeedback_impl(XrSession, const XrHapticActionInfo*,
                                            const XrHapticBaseHeader*) noexcept;
extern XrResult xrStopHapticFeedback_impl(XrSession, const XrHapticActionInfo*) noexcept;
extern XrResult xrCreateActionSpace_impl(XrSession, const XrActionSpaceCreateInfo*,
                                          XrSpace*) noexcept;
extern XrResult xrPathToString_impl(XrInstance, XrPath, uint32_t, uint32_t*, char*) noexcept;
extern XrResult xrStringToPath_impl(XrInstance, const char*, XrPath*) noexcept;
extern XrResult xrGetCurrentInteractionProfile_impl(
    XrSession, XrPath, XrInteractionProfileState*) noexcept;

extern XrResult xrCreateHandTrackerEXT_impl(XrSession, const void*,
                                             uint64_t*) noexcept;
extern XrResult xrDestroyHandTrackerEXT_impl(uint64_t) noexcept;
extern XrResult xrLocateHandJointsEXT_impl(uint64_t, const void*,
                                            void*) noexcept;

namespace {

template <typename Fn>
PFN_xrVoidFunction toPfn(Fn fn) noexcept {
  return reinterpret_cast<PFN_xrVoidFunction>(fn);
}

struct Entry {
  const char* name;
  PFN_xrVoidFunction fn;
};

}  // namespace

XrResult getInstanceProcAddr(XrInstance instance, const char* name,
                             PFN_xrVoidFunction* function) noexcept {
  (void)instance;
  if (function == nullptr || name == nullptr) {
    return XR_ERROR_VALIDATION_FAILURE;
  }
  *function = nullptr;

  static const Entry kTable[] = {
      {"xrGetInstanceProcAddr",
       toPfn(static_cast<PFN_xrGetInstanceProcAddr>(getInstanceProcAddr))},
      {"xrEnumerateInstanceExtensionProperties",
       toPfn(xrEnumerateInstanceExtensionProperties_impl)},
      {"xrCreateInstance", toPfn(xrCreateInstance_impl)},
      {"xrDestroyInstance", toPfn(xrDestroyInstance_impl)},
      {"xrGetInstanceProperties", toPfn(xrGetInstanceProperties_impl)},
      {"xrPollEvent", toPfn(xrPollEvent_impl)},
      {"xrResultToString", toPfn(xrResultToString_impl)},
      {"xrStructureTypeToString", toPfn(xrStructureTypeToString_impl)},
      {"xrGetSystem", toPfn(xrGetSystem_impl)},
      {"xrGetSystemProperties", toPfn(xrGetSystemProperties_impl)},
      {"xrEnumerateViewConfigurations", toPfn(xrEnumerateViewConfigurations_impl)},
      {"xrEnumerateViewConfigurationViews",
       toPfn(xrEnumerateViewConfigurationViews_impl)},
      {"xrEnumerateEnvironmentBlendModes",
       toPfn(xrEnumerateEnvironmentBlendModes_impl)},
      {"xrGetMetalGraphicsRequirementsKHR",
       toPfn(xrGetMetalGraphicsRequirementsKHR_impl)},
      {"xrGetReferenceSpaceBoundsRect",
       toPfn(xrGetReferenceSpaceBoundsRect_impl)},
      {"xrCreateSession", toPfn(xrCreateSession_impl)},
      {"xrDestroySession", toPfn(xrDestroySession_impl)},
      {"xrBeginSession", toPfn(xrBeginSession_impl)},
      {"xrEndSession", toPfn(xrEndSession_impl)},
      {"xrRequestExitSession", toPfn(xrRequestExitSession_impl)},
      {"xrWaitFrame", toPfn(xrWaitFrame_impl)},
      {"xrBeginFrame", toPfn(xrBeginFrame_impl)},
      {"xrEndFrame", toPfn(xrEndFrame_impl)},
      {"xrLocateViews", toPfn(xrLocateViews_impl)},
      {"xrCreateReferenceSpace", toPfn(xrCreateReferenceSpace_impl)},
      {"xrDestroySpace", toPfn(xrDestroySpace_impl)},
      {"xrLocateSpace", toPfn(xrLocateSpace_impl)},
      {"xrEnumerateReferenceSpaces", toPfn(xrEnumerateReferenceSpaces_impl)},
      {"xrEnumerateSwapchainFormats", toPfn(xrEnumerateSwapchainFormats_impl)},
      {"xrCreateSwapchain", toPfn(xrCreateSwapchain_impl)},
      {"xrDestroySwapchain", toPfn(xrDestroySwapchain_impl)},
      {"xrEnumerateSwapchainImages", toPfn(xrEnumerateSwapchainImages_impl)},
      {"xrAcquireSwapchainImage", toPfn(xrAcquireSwapchainImage_impl)},
      {"xrWaitSwapchainImage", toPfn(xrWaitSwapchainImage_impl)},
      {"xrReleaseSwapchainImage", toPfn(xrReleaseSwapchainImage_impl)},
      {"xrCreateActionSet", toPfn(xrCreateActionSet_impl)},
      {"xrDestroyActionSet", toPfn(xrDestroyActionSet_impl)},
      {"xrCreateAction", toPfn(xrCreateAction_impl)},
      {"xrDestroyAction", toPfn(xrDestroyAction_impl)},
      {"xrSuggestInteractionProfileBindings",
       toPfn(xrSuggestInteractionProfileBindings_impl)},
      {"xrAttachSessionActionSets", toPfn(xrAttachSessionActionSets_impl)},
      {"xrSyncActions", toPfn(xrSyncActions_impl)},
      {"xrGetActionStateBoolean", toPfn(xrGetActionStateBoolean_impl)},
      {"xrGetActionStateFloat", toPfn(xrGetActionStateFloat_impl)},
      {"xrGetActionStateVector2f", toPfn(xrGetActionStateVector2f_impl)},
      {"xrGetActionStatePose", toPfn(xrGetActionStatePose_impl)},
      {"xrApplyHapticFeedback", toPfn(xrApplyHapticFeedback_impl)},
      {"xrStopHapticFeedback", toPfn(xrStopHapticFeedback_impl)},
      {"xrCreateActionSpace", toPfn(xrCreateActionSpace_impl)},
      {"xrPathToString", toPfn(xrPathToString_impl)},
      {"xrStringToPath", toPfn(xrStringToPath_impl)},
      {"xrGetCurrentInteractionProfile",
       toPfn(xrGetCurrentInteractionProfile_impl)},
      {"xrCreateHandTrackerEXT", toPfn(xrCreateHandTrackerEXT_impl)},
      {"xrDestroyHandTrackerEXT", toPfn(xrDestroyHandTrackerEXT_impl)},
      {"xrLocateHandJointsEXT", toPfn(xrLocateHandJointsEXT_impl)},
  };

  for (const auto& e : kTable) {
    if (std::strcmp(name, e.name) == 0) {
      *function = e.fn;
      return XR_SUCCESS;
    }
  }
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

}  // namespace fuvr::runtime
