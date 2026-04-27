// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// Forward-declare CoreFoundation types so headers stay C++-clean.
typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::runtime {

// Owns an IOSurface + companion MTLTexture (as `void*` to keep the header
// pure C++). The MTLTexture is retained; the IOSurfaceRef is retained by
// CFRetain. Both released in the destructor.
struct IOSurfaceImage {
  IOSurfaceRef surface{nullptr};
  void* mtlTexture{nullptr};  // id<MTLTexture> — strong reference

  IOSurfaceImage() = default;
  ~IOSurfaceImage();
  IOSurfaceImage(const IOSurfaceImage&) = delete;
  IOSurfaceImage& operator=(const IOSurfaceImage&) = delete;
  IOSurfaceImage(IOSurfaceImage&& other) noexcept;
  IOSurfaceImage& operator=(IOSurfaceImage&& other) noexcept;
};

// Allocate `count` BGRA8 IOSurface-backed Metal textures sized width x height.
// `device` is an `id<MTLDevice>` — pass nullptr to use the system default.
// Returns empty vector on failure.
std::vector<std::unique_ptr<IOSurfaceImage>> allocateIOSurfaceSwapchain(
    void* device, uint32_t width, uint32_t height, uint32_t count) noexcept;

// Create a mach send-right for an IOSurface. Returns 0 (MACH_PORT_NULL) on
// failure. Caller-owns the right; transferring it via SCM_RIGHTS gives
// ownership to the receiver, but locally the right must be deallocated when
// no longer needed (mach_port_deallocate).
uint32_t iosurfaceCreateMachSendRight(IOSurfaceRef surface) noexcept;

// Look up an IOSurface from a mach send-right. Does not consume the right.
IOSurfaceRef iosurfaceFromMachSendRight(uint32_t machRight) noexcept;

// Release a mach send-right (mach_port_deallocate on mach_task_self()).
void iosurfaceReleaseMachSendRight(uint32_t machRight) noexcept;

// Get the system default MTLDevice. Returns id<MTLDevice> as void*; retained.
void* defaultMetalDevice() noexcept;

// Release a retained MTLDevice from defaultMetalDevice().
void releaseMetalDevice(void* device) noexcept;

// Given an id<MTLCommandQueue>, returns its id<MTLDevice> (retained).
void* deviceFromCommandQueue(void* commandQueue) noexcept;

uint32_t iosurfaceID(IOSurfaceRef surface) noexcept;

// STEREO-SPLIT: combines two per-eye IOSurface-backed Metal textures into a
// side-by-side IOSurface (left half = eye 0, right half = eye 1). Owns a small
// ring of destination IOSurfaces so successive blits don't fight the encoder
// for the previous frame's surface.
class StereoBlitter {
 public:
  StereoBlitter() = default;
  ~StereoBlitter();
  StereoBlitter(const StereoBlitter&) = delete;
  StereoBlitter& operator=(const StereoBlitter&) = delete;

  // device: id<MTLDevice> (retained externally), commandQueue may be nullptr
  // (a private queue is created if so). perEyeWidth/Height are the source
  // texture dimensions; the destination is (perEyeWidth*2) x perEyeHeight.
  bool init(void* device, void* commandQueue, uint32_t perEyeWidth,
            uint32_t perEyeHeight) noexcept;

  // Blit `leftTex` (id<MTLTexture>) and `rightTex` to a fresh combined surface.
  // Returns the destination IOSurfaceRef (NOT retained for caller — the
  // blitter owns it; caller must CFRetain if it outlives the next blit cycle).
  // The returned surface has its blit committed and waited.
  IOSurfaceRef blitToCombined(void* leftTex, void* rightTex) noexcept;

  void shutdown() noexcept;

 private:
  void* impl_{nullptr};  // Opaque Obj-C state.
};

}  // namespace fuvr::runtime
