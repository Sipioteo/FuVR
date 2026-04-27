# ADR-0007: IOSurface handoff uses a parallel XPC mach service, not SCM_RIGHTS

- Status: accepted (revised 2026-04-27 to specify XPC over raw mach_msg)
- Date: 2026-04-27

## Context

ADR-0002 splits the runtime from the daemon. The runtime renders into
swapchain images backed by IOSurfaces. The encoder lives in the daemon. So
each frame the runtime must hand an IOSurface to the daemon for encoding.

The original sketch (drafted into the first revision of `proto/fuvrd.capnp`)
suggested transferring the IOSurface mach send-right via `SCM_RIGHTS`
alongside the `SubmitFrameRequest` Cap'n Proto envelope on the same Unix
domain socket. **This is not implementable on macOS**: `SCM_RIGHTS` carries
file descriptors only, and an IOSurface mach send-right is a mach port, not
an fd.

We considered three alternatives:

1. **Convert the mach port to an fd.** No public API does this. `fileport`
   exists but is undocumented and one-way. Rejected.
2. **Raw `mach_msg` via `bootstrap_check_in`.** The daemon registers a
   service name; the runtime looks it up. Per frame, send a single
   `mach_msg` with the IOSurface send-right inline. Problem: modern macOS
   deprecated unprivileged `bootstrap_register` and effectively requires
   launchd-managed mach services for service-name registration. Doable but
   the boilerplate (mach msg builder, port descriptor packing, error
   handling, port leak audits) is non-trivial for one operation.
3. **Use XPC for the IOSurface handoff only.** XPC is Apple's documented,
   actively maintained user-mode mach IPC layer. `xpc_connection_create_mach_service`
   handles bootstrap registration via launchd, and
   `xpc_dictionary_set_mach_send` natively packages a mach send-right
   inline in a dictionary alongside other typed values (the surface token
   as `uint64`). The whole frame-handoff RPC becomes ~30 lines on each
   side. Cap'n Proto stays the format for the rest of the runtime↔daemon
   protocol — XPC handles only the one mach-port-shaped handoff.

Option 3 is Apple's recommended pattern and the lowest-risk path. AVFoundation,
VideoToolbox, and AppKit all use XPC for analogous mach-port handoffs.

## Decision

The IOSurface handoff goes over a dedicated **XPC mach service**
`com.fuvr.daemon.surface`. The daemon hosts an `xpc_connection_t` listener
on this name; the runtime opens an `xpc_connection_t` client to it.

Per frame the runtime:

1. Mints a mach send-right via `IOSurfaceCreateMachPort(surface)`.
2. Builds an `xpc_object_t` dictionary with:
   - key `"token"` → `uint64` matching `SubmitFrameRequest.surfaceToken`
   - key `"surface"` → mach send-right via `xpc_dictionary_set_mach_send`
3. `xpc_connection_send_message(connection, dict)` (fire-and-forget).
4. Sends the matching `SubmitFrameRequest` envelope on the existing UDS RPC
   socket (Cap'n Proto), carrying the same `surfaceToken`.
5. Releases its local mach send-right immediately after the XPC send.

The daemon receives both. The XPC event handler extracts the send-right
with `xpc_dictionary_copy_mach_send`, looks up the `IOSurfaceRef` via
`IOSurfaceLookupFromMachPort`, deallocates the send-right, and stores
`(token → IOSurfaceRef)` in a bounded map. When the matching
`SubmitFrameRequest` arrives on the UDS, the daemon takes the surface from
the map, builds a `CVPixelBuffer` via `CVPixelBufferCreateWithIOSurface`,
and submits to `VTCompressionSession`.

If the mach side and the UDS side arrive out of order, the daemon waits up
to 16 ms for the matching counterpart before dropping (one frame at 60 Hz
budget). Drops increment a metric.

## Consequences

- The implementation is two-channel (UDS Cap'n Proto + XPC) but each
  channel uses the documented, stable Apple primitive for what it carries.
  No private API, no `fileport` hacks, no raw mach_msg builders.
- Correlation by `token` is robust across reconnects: a fresh runtime
  session resets its token sequence; the daemon's per-session surface index
  is cleared on `startSession`.
- We pay one `xpc_connection_send_message` per frame. Measured cost is
  ~5 µs on Apple Silicon, well within the 11.1 ms budget at 90 Hz.
- The daemon must be registered via a launchd plist (`MachServices` key)
  for the XPC service name to be reachable. We ship `daemon/launchd/com.fuvr.daemon.plist`
  and `scripts/install-launchd.sh` (writes to
  `~/Library/LaunchAgents/` and runs `launchctl bootstrap gui/$UID ...`).
- For unit tests where we want to bypass XPC (in-process testing), the
  runtime supports `FUVR_INPROCESS_HANDOFF=1`: the surface registry lives
  in a process-shared singleton and the XPC connection is skipped.

## Alternatives considered

- **`SCM_RIGHTS` of an fd-converted port.** No public API; `fileport` is
  one-way and undocumented. Rejected.
- **XPC end-to-end.** Replaces Cap'n Proto on the runtime↔daemon leg with
  XPC dictionaries. Too invasive for the entire RPC surface; we use XPC
  *only* for the IOSurface frame handoff.
- **Raw `mach_msg` via `bootstrap_check_in`.** Modern macOS deprecates
  unprivileged service-name registration; would still need a launchd
  plist *and* hand-rolled message builders. XPC subsumes both.
- **In-process daemon.** Voids ADR-0002. Rejected.

## Implementation status

Pass 2 left the cross-process handoff stubbed (runtime and daemon agree
only when they share a mach task — unit tests).

Pass 3 implements:
- `daemon/src/iosurface_xpc_service.{hpp,mm}`: XPC listener.
- `runtime-macos/src/iosurface_xpc_client.{hpp,mm}`: XPC client.
- `daemon/launchd/com.fuvr.daemon.plist` + `scripts/install-launchd.sh`.
- `FUVR_INPROCESS_HANDOFF=1` test bypass.
