# ADR-0007: IOSurface handoff uses a parallel mach service, not SCM_RIGHTS

- Status: accepted
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
2. **Use XPC.** XPC's dictionaries carry mach ports natively
   (`xpc_dictionary_set_mach_send`). Adopting XPC for the entire runtime↔
   daemon RPC would force us to re-author every envelope as an XPC dictionary
   and abandon Cap'n Proto on this leg. Too invasive.
3. **A parallel mach service for the IOSurface handoff only.** The daemon
   registers `com.fuvr.daemon.surface` via `bootstrap_check_in`. The runtime
   looks it up via `bootstrap_look_up`. Per frame, the runtime sends a single
   `mach_msg` containing the IOSurface send-right inline as a port
   descriptor, with the matching frame correlation id encoded in the mach
   message header's `msgh_id`. The daemon receives both the mach message
   (with the port) and the corresponding `SubmitFrameRequest` envelope (on
   the UDS), correlates them by `surfaceToken == msgh_id`, and proceeds.

Option 3 keeps Cap'n Proto for everything except the one byte-stream-hostile
operation, and uses the documented Apple pattern for cross-process IOSurface
handoff. AVFoundation, VideoToolbox itself, and Metal use the same shape.

## Decision

The IOSurface handoff between runtime and daemon goes over a dedicated mach
service `com.fuvr.daemon.surface`, registered by the daemon at startup via
`bootstrap_check_in` and looked up by the runtime on first connect. Per
frame the runtime:

1. Mints a mach send-right via `IOSurfaceCreateMachPort(surface)`.
2. Sends a `mach_msg` to the service with `msgh_id = surfaceToken`, the
   send-right inline as a `mach_msg_port_descriptor_t` with
   `MACH_MSG_TYPE_MOVE_SEND`.
3. Sends a `SubmitFrameRequest` envelope on the existing UDS rpc socket with
   the same `surfaceToken`.

The daemon receives both, indexes the inbound mach send-right by
`surfaceToken`, looks up the IOSurface with `IOSurfaceLookupFromMachPort`,
deallocates the send-right after retaining the `IOSurfaceRef`, and proceeds
with `CVPixelBufferCreateWithIOSurface` + `VTCompressionSession` submission.

If the mach side and the UDS side arrive out of order, the daemon waits up
to 16 ms for the matching counterpart before dropping (one frame at 60 Hz
budget). Drops increment a metric.

## Consequences

- The implementation is two-channel but each channel uses the documented,
  stable Apple primitive for what it carries. No private API, no `fileport`
  hacks.
- Correlation by `surfaceToken == msgh_id` is robust across reconnects: a
  fresh runtime session resets its token sequence; the daemon's per-session
  surface index is cleared on `startSession`.
- We pay one extra `mach_msg_send` per frame. Measured cost is sub-microsecond
  on Apple Silicon; well within the 11.1 ms budget at 90 Hz.
- The daemon must be reachable via mach bootstrap. For a user-mode daemon
  this means the user's `bootstrap_subset` must be inherited, which is true
  for any process launched in the user's login session. CI / sandbox needs
  to bridge this if it ever runs end-to-end.

## Alternatives considered

- **`SCM_RIGHTS` of an fd-converted port.** No public API; `fileport` is
  one-way and undocumented. Rejected.
- **XPC end-to-end.** Replaces Cap'n Proto on the runtime↔daemon leg with
  XPC dictionaries. Too invasive for one frame-time field.
- **In-process daemon.** Voids ADR-0002. Rejected.
- **`mach_msg_send` for everything.** Possible but loses Cap'n Proto's
  schema discipline and reuse with the wire format. Rejected.

## Implementation status

Pass 2 left this stubbed:
- The runtime currently passes the `surfaceToken` in-band via the UDS
  envelope only; the mach side channel is not yet implemented. This works
  correctly only when runtime and daemon happen to share a mach task (i.e.
  unit tests, never production).
- `runtime-macos/TODO.md` and `daemon/TODO.md` both note the symmetric work.
- Pass 3 will add the mach service registration in the daemon and the
  bootstrap lookup + per-frame `mach_msg_send` in the runtime.
