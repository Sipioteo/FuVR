// SPDX-License-Identifier: Apache-2.0
//
// Draft vendor extension XR_FUVR_metal_enable. The app passes its
// id<MTLDevice> via XrGraphicsBindingMetalFUVR in xrCreateSession's `next`
// chain. xrEnumerateSwapchainImages returns an array of
// XrSwapchainImageMetalFUVR, each with a strong id<MTLTexture> backed by an
// IOSurface.
#pragma once

#include <openxr/openxr.h>

#ifndef XR_FUVR_metal_enable
#define XR_FUVR_metal_enable 1
#define XR_FUVR_METAL_ENABLE_EXTENSION_NAME "XR_FUVR_metal_enable"
#define XR_FUVR_METAL_ENABLE_SPEC_VERSION 1

// We pick struct type values from the XR vendor reserved range
// (1000'000'000+). Khronos has not assigned this extension a number; these
// IDs are private until the spec is registered.
#define XR_TYPE_GRAPHICS_BINDING_METAL_FUVR  ((XrStructureType)1000'420'000)
#define XR_TYPE_SWAPCHAIN_IMAGE_METAL_FUVR   ((XrStructureType)1000'420'001)

typedef struct XrGraphicsBindingMetalFUVR {
  XrStructureType type;
  const void* next;
  void* mtlDevice;  // id<MTLDevice>
} XrGraphicsBindingMetalFUVR;

typedef struct XrSwapchainImageMetalFUVR {
  XrStructureType type;
  void* next;
  void* texture;  // id<MTLTexture>
} XrSwapchainImageMetalFUVR;

#endif
