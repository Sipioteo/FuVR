// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Bridge a game's eye-render texture (Metal or OpenGL) into an IOSurface
// the daemon can immediately encode with VideoToolbox.
//
// Strategy per backend:
//
// • OpenGL (NSOpenGL CGL contexts on macOS):
//     1. Create an `IOSurface` of the right size + BGRA8 format.
//     2. Create a CGL texture bound to that IOSurface via
//        `CGLTexImageIOSurface2D`.
//     3. Stand up an FBO whose color attachment is that texture.
//     4. Bind the game's source texture to a sampler and draw a fullscreen
//        triangle that copies pixels into the FBO. (We can't `glCopyImage*`
//        portably across CGL contexts and texture formats.)
//
//   The shim attaches to the GL context that's current when `Submit()`
//   fires — that's the game's render context, by OpenVR contract.
//
// • Metal (id<MTLTexture>):
//     1. Pull the input texture's `MTLDevice`.
//     2. Maintain a per-eye IOSurface-backed `MTLTexture` (BGRA8).
//     3. Use a `MTLBlitCommandEncoder` to copy source → IOSurface texture.
//     4. Commit the command buffer; the daemon retrieves the surface via
//        XPC.
//
// The bridges hold a small ring of per-eye IOSurfaces (default 3) so the
// game keeps producing while VideoToolbox encodes the previous frame.

#include <cstdint>
#include <memory>

#include "openvr.h"

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::openvr_shim {

struct BridgeFrame {
  /// 64-bit token paired with the IOSurface in the XPC handoff.
  uint64_t   token;
  /// Borrowed (not retained); valid until the next call to `acquire`.
  IOSurfaceRef surface;
};

/// Abstract base; real implementations live in
/// `texture_bridge_gl.mm` and `texture_bridge_metal.mm`.
class TextureBridge {
 public:
  virtual ~TextureBridge() = default;

  /// Lazily allocate the per-eye IOSurface ring on first call. `width` and
  /// `height` are the per-eye render-target size.
  virtual bool prepare(uint32_t width, uint32_t height) = 0;

  /// Copy `tex` into the next IOSurface in the ring; return its token +
  /// borrowed handle so the caller can ship the surface via XPC and
  /// announce the token over the RPC socket.
  ///
  /// NOTE: with stereo composite enabled (the standard path), callers
  /// should NOT ship the per-eye `BridgeFrame` directly — instead they
  /// should call `finalizeStereoFrame()` after both eyes are stashed.
  /// The per-eye result is still returned (token+surface) so callers
  /// that want to keep diagnosing per-eye output can, but the shim's
  /// production path treats per-eye copies as scratch and only ships
  /// the SBS frame.
  virtual BridgeFrame copyFromTexture(uint32_t eye,
                                      const vr::Texture_t* tex,
                                      const vr::VRTextureBounds_t* bounds) = 0;

  /// Composite the most recently `copyFromTexture`-stashed left + right
  /// eye IOSurfaces into a single side-by-side IOSurface
  /// (`perEyeWidth*2 × perEyeHeight`, BGRA8, IOSurface-backed Metal
  /// texture) and return a fresh `BridgeFrame` referring to it.
  ///
  /// Returns `{0, nullptr}` if either eye hasn't been copied since the
  /// last finalize (e.g. the game only rendered one eye for this frame
  /// — caller should drop and wait for the next pair).
  ///
  /// The SBS IOSurface ring is allocated lazily on first call and
  /// reused across frames.
  virtual BridgeFrame finalizeStereoFrame() = 0;
};

/// Returns nullptr if the texture type / current OS doesn't expose the
/// platform context we need to copy from.
std::unique_ptr<TextureBridge> makeGLBridge();
std::unique_ptr<TextureBridge> makeMetalBridge();

}  // namespace fuvr::openvr_shim
