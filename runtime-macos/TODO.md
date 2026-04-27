# runtime-macos — open questions / TODOs

- Implement `xrCreateReferenceSpace` / `xrLocateSpace` once we have a Quest
  reference space mapped to Mac coordinate space.
- `xrPollEvent` should emit `XrEventDataSessionStateChanged` driven by the
  daemon connection state machine (currently never fires).
- Replace the `std::hash<string_view>` based path interning with a real
  bidirectional path table so `xrPathToString` can be implemented.
- Action System: only stub implementations for boolean/float/vector2f. Pose
  actions and haptic feedback need transport hookup.
- **Mach port transport across tasks.** macOS does not allow sending mach
  send-rights via `SCM_RIGHTS` (which is fd-only). The proto schema comment in
  `proto/fuvrd.capnp` is therefore aspirational; we currently encode the mach
  port *name* inside `SubmitFrameRequest.surfaceToken` and rely on the daemon
  living in the same Mach task during early development. For a proper
  out-of-process daemon, `runtime-macos` and `daemon/` must agree on a
  `mach_msg`-based side channel that transfers `MACH_MSG_TYPE_COPY_SEND`
  descriptors keyed by the same surfaceToken ordinal. **Daemon-side change
  required.**
- `xrEndFrame` currently picks `swapchains.front()` for IOSurface lookup.
  When apps create multiple swapchains (per-eye, layers) we need to walk the
  layer chain in `XrFrameEndInfo` and submit each.
- `submitFrame` is fire-and-forget; `EncodeStats` arrives asynchronously and
  is currently dropped. Wire into a metrics aggregator.
- Reconnect path runs the reader thread even before a successful connect; if
  the daemon is permanently absent the runtime spends a thread blocked in
  exponential backoff. Acceptable for M0; revisit for M3.
- The `XR_FUVR_metal_enable` extension struct IDs (`1000'420'00x`) are
  privately picked. Register a real range with Khronos before public release.
- We allocate 3 IOSurfaces per swapchain. Apps that request a different
  `XrSwapchainCreateInfo::sampleCount` or array layers > 1 are not yet
  supported.
