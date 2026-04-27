// SPDX-License-Identifier: Apache-2.0
#include <cstring>

#include "fuvr/runtime.hpp"

extern "C" {

__attribute__((visibility("default"))) XrResult
xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest) {
  if (loaderInfo == nullptr || runtimeRequest == nullptr) {
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
      loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
      loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (runtimeRequest->structType !=
          XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST ||
      runtimeRequest->structVersion != XR_RUNTIME_INFO_STRUCT_VERSION ||
      runtimeRequest->structSize != sizeof(XrNegotiateRuntimeRequest)) {
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
      loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION) {
    return XR_ERROR_INITIALIZATION_FAILED;
  }

  runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
  runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;
  runtimeRequest->getInstanceProcAddr =
      reinterpret_cast<PFN_xrGetInstanceProcAddr>(
          fuvr::runtime::getInstanceProcAddr);
  return XR_SUCCESS;
}

__attribute__((visibility("default"))) XrResult xrGetInstanceProcAddr(
    XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
  return fuvr::runtime::getInstanceProcAddr(instance, name, function);
}

}  // extern "C"
