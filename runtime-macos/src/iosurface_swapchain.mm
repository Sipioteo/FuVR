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

void* deviceFromCommandQueue(void* commandQueue) noexcept {
  if (commandQueue == nullptr) return nullptr;
  id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)commandQueue;
  id<MTLDevice> dev = q.device;
  if (dev == nil) return nullptr;
  return (void*)CFBridgingRetain(dev);
}

uint32_t iosurfaceID(IOSurfaceRef surface) noexcept {
  if (surface == nullptr) return 0;
  return IOSurfaceGetID(surface);
}

// STEREO-SPLIT: stereo blitter implementation.
namespace {

struct StereoBlitterImpl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  bool ownsQueue = false;
  uint32_t perEyeW = 0;
  uint32_t perEyeH = 0;
  // Small ring so we don't blit on top of a surface still held by the encoder
  // pipeline. 3 matches the typical IOSurface swapchain depth.
  static constexpr int kRing = 3;
  IOSurfaceRef surfaces[kRing] = {nullptr, nullptr, nullptr};
  id<MTLTexture> textures[kRing] = {nil, nil, nil};
  int next = 0;
};

}  // namespace

StereoBlitter::~StereoBlitter() { shutdown(); }

bool StereoBlitter::init(void* devicePtr, void* commandQueuePtr,
                          uint32_t perEyeWidth, uint32_t perEyeHeight) noexcept {
  if (impl_ != nullptr) return true;
  if (perEyeWidth == 0 || perEyeHeight == 0) return false;
  id<MTLDevice> device = (__bridge id<MTLDevice>)devicePtr;
  if (device == nil) return false;

  auto* s = new StereoBlitterImpl();
  s->device = device;
  s->perEyeW = perEyeWidth;
  s->perEyeH = perEyeHeight;
  if (commandQueuePtr != nullptr) {
    s->queue = (__bridge id<MTLCommandQueue>)commandQueuePtr;
    s->ownsQueue = false;
  } else {
    s->queue = [device newCommandQueue];
    s->ownsQueue = true;
  }
  if (s->queue == nil) {
    delete s;
    return false;
  }

  const uint32_t W = perEyeWidth * 2;
  const uint32_t H = perEyeHeight;
  for (int i = 0; i < StereoBlitterImpl::kRing; ++i) {
    NSDictionary* props = @{
      (id)kIOSurfaceWidth : @(W),
      (id)kIOSurfaceHeight : @(H),
      (id)kIOSurfaceBytesPerElement : @(4),
      (id)kIOSurfacePixelFormat : @((uint32_t)'BGRA'),
    };
    IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)props);
    if (surf == nullptr) {
      delete s;
      return false;
    }
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:W
                                    height:H
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc
                                                iosurface:surf
                                                    plane:0];
    if (tex == nil) {
      CFRelease(surf);
      delete s;
      return false;
    }
    s->surfaces[i] = surf;
    s->textures[i] = tex;
  }
  impl_ = s;
  return true;
}

IOSurfaceRef StereoBlitter::blitToCombined(void* leftTexPtr,
                                            void* rightTexPtr) noexcept {
  if (impl_ == nullptr) return nullptr;
  auto* s = static_cast<StereoBlitterImpl*>(impl_);
  id<MTLTexture> leftTex = (__bridge id<MTLTexture>)leftTexPtr;
  id<MTLTexture> rightTex = (__bridge id<MTLTexture>)rightTexPtr;
  if (leftTex == nil) return nullptr;

  const int slot = s->next;
  s->next = (s->next + 1) % StereoBlitterImpl::kRing;
  id<MTLTexture> dst = s->textures[slot];
  IOSurfaceRef dstSurf = s->surfaces[slot];
  if (dst == nil || dstSurf == nullptr) return nullptr;

  id<MTLCommandBuffer> cb = [s->queue commandBuffer];
  id<MTLBlitCommandEncoder> enc = [cb blitCommandEncoder];

  const uint32_t eyeW = s->perEyeW;
  const uint32_t eyeH = s->perEyeH;

  auto blitOne = [&](id<MTLTexture> src, uint32_t dstX) {
    if (src == nil) return;
    // Why: source might be a different size than perEye if the app over-sized
    // the swapchain. Clamp to min so we don't sample out-of-bounds.
    NSUInteger srcW = MIN((NSUInteger)src.width, (NSUInteger)eyeW);
    NSUInteger srcH = MIN((NSUInteger)src.height, (NSUInteger)eyeH);
    [enc copyFromTexture:src
             sourceSlice:0
             sourceLevel:0
            sourceOrigin:MTLOriginMake(0, 0, 0)
              sourceSize:MTLSizeMake(srcW, srcH, 1)
               toTexture:dst
        destinationSlice:0
        destinationLevel:0
       destinationOrigin:MTLOriginMake(dstX, 0, 0)];
  };

  blitOne(leftTex, 0);
  // If right is missing, mirror left into the right half so we still get a
  // valid 4128-wide surface (degraded mono); avoids encoder-side garbage.
  blitOne(rightTex != nil ? rightTex : leftTex, eyeW);

  [enc endEncoding];
  [cb commit];
  // Why: encoder may pick up the IOSurface synchronously via the XPC token
  // path. Wait so the blit's GPU writes are visible before the surface ships.
  [cb waitUntilCompleted];
  return dstSurf;
}

void StereoBlitter::shutdown() noexcept {
  if (impl_ == nullptr) return;
  auto* s = static_cast<StereoBlitterImpl*>(impl_);
  for (int i = 0; i < StereoBlitterImpl::kRing; ++i) {
    s->textures[i] = nil;
    if (s->surfaces[i]) CFRelease(s->surfaces[i]);
    s->surfaces[i] = nullptr;
  }
  s->queue = nil;
  s->device = nil;
  delete s;
  impl_ = nullptr;
}

}  // namespace fuvr::runtime
