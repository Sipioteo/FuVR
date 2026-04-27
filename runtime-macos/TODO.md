# runtime-macos — open questions / TODOs

- Wire `xrEnumerateSwapchainImages` to actually allocate Metal/Vulkan textures
  once we pick the graphics extension story (see SPEC §11.2).
- Implement `xrCreateReferenceSpace` / `xrLocateSpace` once we have a Quest
  reference space mapped to Mac coordinate space.
- Wire `XR_FUVR_metal_enable` properly — for now we only advertise it.
- `xrPollEvent` should emit `XrEventDataSessionStateChanged` driven by the
  transport channel state machine.
- Decide single-process vs daemon (SPEC §11.3). Current code assumes
  in-process.
- Replace the `std::hash<string_view>` based path interning with a real
  bidirectional path table so `xrPathToString` can be implemented.
- Action System: only stub implementations for boolean/float/vector2f. Pose
  actions and haptic feedback need transport hookup.
