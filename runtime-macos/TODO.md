# runtime-macos — open questions / TODOs

- [x] `xrPollEvent` emits `XrEventDataSessionStateChanged` driven by the
  daemon connection state machine (pass 3).
- [x] `xrCreateReferenceSpace` / `xrLocateSpace` for VIEW/LOCAL/STAGE
  (pass 3).
- [x] `xrDestroySpace` (pass 3).
- [x] `xrCreateActionSpace` — succeeds but `xrLocateSpace` against an
  action space returns invalid bits until controller pose forwarding lands.
- [x] `EncodeStats` consumer wired into `Session::encoderStats` rolling
  256-frame window (pass 3); read via `fuvr-runtime-metrics`.
- Controller pose forwarding (pass 4): extend `PoseSnapshot` (proto) and
  `DaemonClient` to carry left/right grip + aim poses, then make
  `xrLocateSpace` on action spaces report valid+tracked.
- Replace the `std::hash<string_view>` based path interning with a real
  bidirectional path table so `xrPathToString` can be implemented.
- Action System: only stub implementations for boolean/float/vector2f. Pose
  actions and haptic feedback need transport hookup.
- `xrEndFrame` currently picks `swapchains.front()` for IOSurface lookup.
  When apps create multiple swapchains (per-eye, layers) we need to walk the
  layer chain in `XrFrameEndInfo` and submit each.
- Reconnect path runs the reader thread even before a successful connect; if
  the daemon is permanently absent the runtime spends a thread blocked in
  exponential backoff. Acceptable for M0; revisit for M3.
- The `XR_FUVR_metal_enable` extension struct IDs (`1000'420'00x`) are
  privately picked. Register a real range with Khronos before public release.
- We allocate 3 IOSurfaces per swapchain. Apps that request a different
  `XrSwapchainCreateInfo::sampleCount` or array layers > 1 are not yet
  supported.
