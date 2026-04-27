# ADR-0008: OpenXR extension scope for v1

- Status: accepted
- Date: 2026-04-27

## Context

The OpenXR registry has ~150 extensions. Many are vendor-specific (Meta,
HTC, Valve), some are layered Khronos extensions, and a few are de-facto
mandatory for any modern app to feel correct. We need to draw a line for
v1.

## Decision

The runtime advertises and implements the following extensions in v1:

**Core (mandatory for v1):**
- `XR_FUVR_metal_enable` — vendor extension (ours), exposes Metal swapchain
  images.
- `XR_KHR_composition_layer_color_scale_bias` — apps assume this.

**Stubbed (advertised but limited):**
- `XR_KHR_vulkan_enable2` — declared, returns `XR_ERROR_GRAPHICS_DEVICE_INVALID`
  until MoltenVK integration lands. Tracked as M3 work.
- `XR_EXT_hand_tracking` — declared. `xrLocateHandJointsEXT` returns invalid
  bits until the Quest forwards joint data; the wire schema is frozen so
  joints ride the `error` arm with prefix `q-hand:` until a major version
  bump (post-1.0).
- `XR_EXT_eye_gaze_interaction` — declared. Returns invalid bits; Quest 3
  has eye tracking on Pro models only.

**Out of scope for v1 (returned with `XR_ERROR_EXTENSION_NOT_PRESENT`):**
- `XR_FB_passthrough` — passthrough is a Quest system feature; remoting it
  through a streaming runtime is meaningful research but not v1 scope.
- `XR_FB_spatial_entities`, anchors, scene understanding — needed for room-
  scale apps, deferred.
- `XR_KHR_visibility_mask` — nice-to-have, deferred.
- All vendor extensions from non-Meta vendors.

## Consequences

- v1 covers: Blender VR mode, Godot OpenXR baseline, Unity OpenXR Plugin
  baseline, Unreal OpenXR baseline. These are the engines the project
  cares about per SPEC §3.1.2.
- v1 does NOT cover: passthrough AR, mixed reality, room scanning,
  multi-modal eye+hand interaction. Those are research scope, not product.
- Adding extensions later does not require a wire schema bump because the
  schema does not encode extension semantics — extensions live entirely on
  the OpenXR side.

## Alternatives considered

- **All Khronos extensions advertised.** Tempting but every advertised
  extension creates an obligation to actually implement it; runtime
  conformance suite (CTS) checks this. Rejected.
- **Bare-minimum core only.** Apps would refuse to launch (most ask for at
  least hand tracking advertisement even when they don't use it).
  Rejected.
