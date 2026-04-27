// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#define FUVR_LOG(fmt, ...)                                                     \
  do {                                                                         \
    if (std::getenv("FUVR_RT_DEBUG"))                                          \
      std::fprintf(stderr, "[fuvr-rt] " fmt "\n", ##__VA_ARGS__);              \
  } while (0)

#include "fuvr/iosurface_swapchain.hpp"
#include "fuvr/path_registry.hpp"
#include "fuvr/runtime.hpp"

namespace fuvr::runtime {

namespace {

std::mutex& registryMutex() noexcept {
  static std::mutex m;
  return m;
}

std::unordered_map<uint64_t, Instance*>& instanceRegistry() noexcept {
  static std::unordered_map<uint64_t, Instance*> r;
  return r;
}

std::unordered_map<uint64_t, Session*>& sessionRegistry() noexcept {
  static std::unordered_map<uint64_t, Session*> r;
  return r;
}

std::unordered_map<uint64_t, ActionSet*>& actionSetRegistry() noexcept {
  static std::unordered_map<uint64_t, ActionSet*> r;
  return r;
}

std::unordered_map<uint64_t, Action*>& actionRegistry() noexcept {
  static std::unordered_map<uint64_t, Action*> r;
  return r;
}

std::unordered_map<uint64_t, Swapchain*>& swapchainRegistry() noexcept {
  static std::unordered_map<uint64_t, Swapchain*> r;
  return r;
}

std::atomic<uint64_t>& nextHandle() noexcept {
  static std::atomic<uint64_t> n{1};
  return n;
}

uint64_t allocHandle() noexcept {
  return nextHandle().fetch_add(1, std::memory_order_relaxed);
}

constexpr XrSystemId kSystemId = 1;

const char* kSupportedExtensions[] = {
    "XR_KHR_metal_enable",
    "XR_KHR_vulkan_enable2",
    "XR_FUVR_metal_enable",
    "XR_EXT_hand_tracking",
    "XR_EXT_eye_gaze_interaction",
    "XR_EXT_local_floor",
    "XR_EXT_debug_utils",
};

}  // namespace

Instance* lookupInstance(XrInstance handle) noexcept {
  std::lock_guard<std::mutex> lk(registryMutex());
  auto it = instanceRegistry().find(reinterpret_cast<uint64_t>(handle));
  return it == instanceRegistry().end() ? nullptr : it->second;
}

Session* lookupSession(XrSession handle) noexcept {
  std::lock_guard<std::mutex> lk(registryMutex());
  auto it = sessionRegistry().find(reinterpret_cast<uint64_t>(handle));
  return it == sessionRegistry().end() ? nullptr : it->second;
}

XrResult xrCreateInstance_impl(const XrInstanceCreateInfo* info,
                                XrInstance* out) noexcept {
  if (info == nullptr || out == nullptr) {
    return XR_ERROR_VALIDATION_FAILURE;
  }
  auto inst = std::make_unique<Instance>();
  const uint64_t h = allocHandle();
  inst->handle = reinterpret_cast<XrInstance>(h);
  Instance* raw = inst.get();
  {
    std::lock_guard<std::mutex> lk(registryMutex());
    instanceRegistry().emplace(h, raw);
  }
  inst.release();
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrDestroyInstance_impl(XrInstance instance) noexcept {
  Instance* inst = lookupInstance(instance);
  if (inst == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  {
    std::lock_guard<std::mutex> lk(registryMutex());
    instanceRegistry().erase(reinterpret_cast<uint64_t>(instance));
  }
  delete inst;
  return XR_SUCCESS;
}

XrResult xrGetInstanceProperties_impl(XrInstance instance,
                                       XrInstanceProperties* props) noexcept {
  if (lookupInstance(instance) == nullptr || props == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  props->runtimeVersion = kRuntimeVersion;
  std::strncpy(props->runtimeName, kRuntimeName, XR_MAX_RUNTIME_NAME_SIZE - 1);
  props->runtimeName[XR_MAX_RUNTIME_NAME_SIZE - 1] = '\0';
  return XR_SUCCESS;
}

XrResult xrEnumerateInstanceExtensionProperties_impl(
    const char* layerName, uint32_t capacity, uint32_t* countOutput,
    XrExtensionProperties* properties) noexcept {
  (void)layerName;
  if (countOutput == nullptr) {
    return XR_ERROR_VALIDATION_FAILURE;
  }
  const uint32_t total =
      sizeof(kSupportedExtensions) / sizeof(kSupportedExtensions[0]);
  if (properties == nullptr || capacity == 0) {
    *countOutput = total;
    return XR_SUCCESS;
  }
  if (capacity < total) {
    *countOutput = total;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  for (uint32_t i = 0; i < total; ++i) {
    properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    properties[i].next = nullptr;
    std::strncpy(properties[i].extensionName, kSupportedExtensions[i],
                 XR_MAX_EXTENSION_NAME_SIZE - 1);
    properties[i].extensionName[XR_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
    properties[i].extensionVersion = 1;
  }
  *countOutput = total;
  return XR_SUCCESS;
}

XrResult xrGetSystem_impl(XrInstance instance, const XrSystemGetInfo* info,
                           XrSystemId* systemId) noexcept {
  FUVR_LOG("xrGetSystem(formFactor=%d)", info ? info->formFactor : -1);
  if (lookupInstance(instance) == nullptr || info == nullptr ||
      systemId == nullptr) {
    FUVR_LOG("  -> handle invalid");
    return XR_ERROR_HANDLE_INVALID;
  }
  if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
    FUVR_LOG("  -> form factor unsupported");
    return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
  }
  *systemId = kSystemId;
  FUVR_LOG("  -> ok systemId=%llu", (unsigned long long)kSystemId);
  return XR_SUCCESS;
}

XrResult xrGetSystemProperties_impl(XrInstance instance, XrSystemId systemId,
                                     XrSystemProperties* props) noexcept {
  FUVR_LOG("xrGetSystemProperties(sys=%llu)", (unsigned long long)systemId);
  if (lookupInstance(instance) == nullptr || systemId != kSystemId ||
      props == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  props->systemId = systemId;
  props->vendorId = 0x4655;
  std::strncpy(props->systemName, "FuVR HMD (Quest via streaming)",
               XR_MAX_SYSTEM_NAME_SIZE - 1);
  props->systemName[XR_MAX_SYSTEM_NAME_SIZE - 1] = '\0';
  props->graphicsProperties.maxLayerCount = 16;
  props->graphicsProperties.maxSwapchainImageWidth = 4128;
  props->graphicsProperties.maxSwapchainImageHeight = 2208;
  props->trackingProperties.orientationTracking = XR_TRUE;
  props->trackingProperties.positionTracking = XR_TRUE;
  return XR_SUCCESS;
}

XrResult xrGetReferenceSpaceBoundsRect_impl(
    XrSession session, XrReferenceSpaceType referenceSpaceType,
    XrExtent2Df* bounds) noexcept {
  // Why: Blender calls this during GHOST_XrSession::start to size the stage.
  // We don't have real stage bounds (the Quest's guardian is not forwarded
  // to the runtime), so we report unavailable. Spec: when bounds are not
  // available, fill with zeros and return XR_SPACE_BOUNDS_UNAVAILABLE.
  if (bounds != nullptr) {
    bounds->width = 0.0f;
    bounds->height = 0.0f;
  }
  (void)session;
  (void)referenceSpaceType;
  return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XrResult xrGetMetalGraphicsRequirementsKHR_impl(
    XrInstance instance, XrSystemId systemId,
    XrGraphicsRequirementsMetalKHR* req) noexcept {
  FUVR_LOG("xrGetMetalGraphicsRequirementsKHR(sys=%llu)", (unsigned long long)systemId);
  if (lookupInstance(instance) == nullptr || systemId != kSystemId ||
      req == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  // Why: spec requires the runtime to advertise the recommended Metal device
  // before xrCreateSession. We expose the system default; multi-GPU Macs
  // will need refinement in M3.
  req->metalDevice = defaultMetalDevice();
  return XR_SUCCESS;
}

XrResult xrEnumerateViewConfigurations_impl(
    XrInstance instance, XrSystemId systemId, uint32_t capacity,
    uint32_t* countOutput, XrViewConfigurationType* types) noexcept {
  if (lookupInstance(instance) == nullptr || systemId != kSystemId ||
      countOutput == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (types == nullptr || capacity == 0) {
    *countOutput = 1;
    return XR_SUCCESS;
  }
  types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  *countOutput = 1;
  return XR_SUCCESS;
}

XrResult xrEnumerateViewConfigurationViews_impl(
    XrInstance instance, XrSystemId systemId, XrViewConfigurationType type,
    uint32_t capacity, uint32_t* countOutput,
    XrViewConfigurationView* views) noexcept {
  FUVR_LOG("xrEnumerateViewConfigurationViews(type=%d, cap=%u)", type, capacity);
  if (lookupInstance(instance) == nullptr || systemId != kSystemId ||
      countOutput == nullptr) {
    FUVR_LOG("  -> handle invalid");
    return XR_ERROR_HANDLE_INVALID;
  }
  if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
    FUVR_LOG("  -> view config %d unsupported", type);
    return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
  }
  if (views == nullptr || capacity == 0) {
    *countOutput = 2;
    return XR_SUCCESS;
  }
  if (capacity < 2) {
    *countOutput = 2;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  for (uint32_t i = 0; i < 2; ++i) {
    views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    views[i].next = nullptr;
    views[i].recommendedImageRectWidth = 2064;
    views[i].recommendedImageRectHeight = 2208;
    views[i].maxImageRectWidth = 4128;
    views[i].maxImageRectHeight = 2208;
    views[i].recommendedSwapchainSampleCount = 1;
    views[i].maxSwapchainSampleCount = 1;
  }
  *countOutput = 2;
  return XR_SUCCESS;
}

XrResult xrEnumerateEnvironmentBlendModes_impl(
    XrInstance instance, XrSystemId systemId, XrViewConfigurationType type,
    uint32_t capacity, uint32_t* countOutput,
    XrEnvironmentBlendMode* modes) noexcept {
  FUVR_LOG("xrEnumerateEnvironmentBlendModes(cap=%u)", capacity);
  (void)type;
  if (lookupInstance(instance) == nullptr || systemId != kSystemId ||
      countOutput == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (modes == nullptr || capacity == 0) {
    *countOutput = 1;
    return XR_SUCCESS;
  }
  modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  *countOutput = 1;
  return XR_SUCCESS;
}

XrResult xrPollEvent_impl(XrInstance instance, XrEventDataBuffer* buffer) noexcept {
  Instance* inst = lookupInstance(instance);
  if (inst == nullptr || buffer == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  bool got = inst->events.pop(buffer);
  if (got && std::getenv("FUVR_RT_DEBUG"))
    std::fprintf(stderr, "[fuvr-rt] xrPollEvent() -> type=%d\n", buffer->type);
  return got ? XR_SUCCESS : XR_EVENT_UNAVAILABLE;
}

XrResult xrResultToString_impl(XrInstance instance, XrResult value,
                                char* out) noexcept {
  if (lookupInstance(instance) == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (value == XR_SUCCESS) {
    std::strncpy(out, "XR_SUCCESS", XR_MAX_RESULT_STRING_SIZE - 1);
  } else {
    std::snprintf(out, XR_MAX_RESULT_STRING_SIZE, "XR_RESULT_%d",
                  static_cast<int>(value));
  }
  return XR_SUCCESS;
}

XrResult xrStructureTypeToString_impl(XrInstance instance,
                                       XrStructureType value,
                                       char* out) noexcept {
  if (lookupInstance(instance) == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  std::snprintf(out, XR_MAX_STRUCTURE_NAME_SIZE, "XR_TYPE_%d",
                static_cast<int>(value));
  return XR_SUCCESS;
}

XrResult xrPathToString_impl(XrInstance instance, XrPath path,
                              uint32_t bufferCapacity, uint32_t* countOutput,
                              char* buffer) noexcept {
  if (lookupInstance(instance) == nullptr || countOutput == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  const std::string* s = pathRegistry().lookup(path);
  if (s == nullptr) return XR_ERROR_PATH_INVALID;
  const uint32_t needed = static_cast<uint32_t>(s->size()) + 1;
  *countOutput = needed;
  if (buffer == nullptr || bufferCapacity == 0) return XR_SUCCESS;
  if (bufferCapacity < needed) return XR_ERROR_SIZE_INSUFFICIENT;
  std::memcpy(buffer, s->data(), s->size());
  buffer[s->size()] = '\0';
  return XR_SUCCESS;
}

XrResult xrStringToPath_impl(XrInstance instance, const char* str,
                              XrPath* out) noexcept {
  if (lookupInstance(instance) == nullptr || str == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  *out = pathRegistry().intern(std::string_view(str));
  return XR_SUCCESS;
}

XrResult xrGetCurrentInteractionProfile_impl(
    XrSession, XrPath, XrInteractionProfileState* state) noexcept {
  // Why: Blender calls this during session init to learn which controller
  // bindings are bound for each user path. Until controller pose forwarding
  // is wired (pass 5 item) we report XR_NULL_PATH so callers know there is
  // no current profile but do not interpret the call as an error.
  if (state == nullptr) {
    return XR_ERROR_VALIDATION_FAILURE;
  }
  state->type = XR_TYPE_INTERACTION_PROFILE_STATE;
  state->next = nullptr;
  state->interactionProfile = XR_NULL_PATH;
  return XR_SUCCESS;
}

namespace detail {
std::unordered_map<uint64_t, Session*>& sessions() noexcept {
  return sessionRegistry();
}
std::unordered_map<uint64_t, ActionSet*>& actionSets() noexcept {
  return actionSetRegistry();
}
std::unordered_map<uint64_t, Action*>& actions() noexcept {
  return actionRegistry();
}
std::unordered_map<uint64_t, Swapchain*>& swapchains() noexcept {
  return swapchainRegistry();
}
uint64_t nextHandleAlloc() noexcept { return allocHandle(); }
std::mutex& globalMutex() noexcept { return registryMutex(); }
}  // namespace detail

}  // namespace fuvr::runtime
