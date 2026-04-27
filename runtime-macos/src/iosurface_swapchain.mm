// SPDX-License-Identifier: Apache-2.0
#import <CoreFoundation/CoreFoundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <mach/mach.h>

#include "fuvr/iosurface_swapchain.hpp"

namespace fuvr::runtime {

IOSurfaceImage::~IOSurfaceImage() {
  if (mtlTexture) {
    CFRelease(static_cast<CFTypeRef>(mtlTexture));
    mtlTexture = nullptr;
  }
  if (surface) {
    CFRelease(surface);
    surface = nullptr;
  }
}

IOSurfaceImage::IOSurfaceImage(IOSurfaceImage&& other) noexcept
    : surface(other.surface), mtlTexture(other.mtlTexture) {
  other.surface = nullptr;
  other.mtlTexture = nullptr;
}

IOSurfaceImage& IOSurfaceImage::operator=(IOSurfaceImage&& other) noexcept {
  if (this != &other) {
    if (mtlTexture) CFRelease(static_cast<CFTypeRef>(mtlTexture));
    if (surface) CFRelease(surface);
    surface = other.surface;
    mtlTexture = other.mtlTexture;
    other.surface = nullptr;
    other.mtlTexture = nullptr;
  }
  return *this;
}

std::vector<std::unique_ptr<IOSurfaceImage>> allocateIOSurfaceSwapchain(
    void* devicePtr, uint32_t width, uint32_t height, uint32_t count) noexcept {
  std::vector<std::unique_ptr<IOSurfaceImage>> out;
  if (width == 0 || height == 0 || count == 0) return out;

  id<MTLDevice> device = (__bridge id<MTLDevice>)devicePtr;
  bool ownsDevice = false;
  if (device == nil) {
    device = MTLCreateSystemDefaultDevice();
    ownsDevice = true;
  }
  if (device == nil) return out;

  for (uint32_t i = 0; i < count; ++i) {
    NSDictionary* props = @{
      (id)kIOSurfaceWidth : @(width),
      (id)kIOSurfaceHeight : @(height),
      (id)kIOSurfaceBytesPerElement : @(4),
      (id)kIOSurfacePixelFormat : @((uint32_t)'BGRA'),
    };
    IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)props);
    if (surf == nullptr) {
      out.clear();
      break;
    }
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc
                                                iosurface:surf
                                                    plane:0];
    if (tex == nil) {
      CFRelease(surf);
      out.clear();
      break;
    }
    auto img = std::make_unique<IOSurfaceImage>();
    img->surface = surf;
    img->mtlTexture = (void*)CFBridgingRetain(tex);
    out.push_back(std::move(img));
  }

  if (ownsDevice) {
    // device gets released when the local id<MTLDevice> goes out of scope (ARC).
  }
  return out;
}

uint32_t iosurfaceCreateMachSendRight(IOSurfaceRef surface) noexcept {
  if (surface == nullptr) return 0;
  mach_port_t port = IOSurfaceCreateMachPort(surface);
  return static_cast<uint32_t>(port);
}

IOSurfaceRef iosurfaceFromMachSendRight(uint32_t machRight) noexcept {
  if (machRight == 0) return nullptr;
  return IOSurfaceLookupFromMachPort(static_cast<mach_port_t>(machRight));
}

void iosurfaceReleaseMachSendRight(uint32_t machRight) noexcept {
  if (machRight == 0) return;
  mach_port_deallocate(mach_task_self(), static_cast<mach_port_t>(machRight));
}

void* defaultMetalDevice() noexcept {
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  if (dev == nil) return nullptr;
  return (void*)CFBridgingRetain(dev);
}

void releaseMetalDevice(void* device) noexcept {
  if (device == nullptr) return;
  CFRelease(static_cast<CFTypeRef>(device));
}

uint32_t iosurfaceID(IOSurfaceRef surface) noexcept {
  if (surface == nullptr) return 0;
  return IOSurfaceGetID(surface);
}

}  // namespace fuvr::runtime
