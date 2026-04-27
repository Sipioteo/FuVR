# ADR-0005: Reed-Solomon FEC, no ARQ

- Status: accepted
- Date: 2026-04-27

## Context

PCVR streaming has a hard latency budget: motion-to-photon under ~50 ms for
comfort, of which the network round-trip is one of several budgets. At 90 Hz
the inter-frame interval is 11.1 ms. A single retransmit at 5 ms RTT eats
nearly half a frame. At any kind of jitter, ARQ falls behind.

Wi-Fi 6/6E and USB-ADB tunnels both have non-zero packet loss in practice.
We need to recover from loss without paying retransmit latency.

## Decision

The `transport-udp` crate uses **Reed-Solomon FEC** with default `(10, 4)` —
10 data shards, 4 parity shards, ~28% overhead. No ARQ. If a frame can't be
reconstructed from the received shards, it is dropped and the next IDR
(forced by the encoder on a configurable cadence) re-syncs.

The `transport-usb` crate uses a TCP loopback to the ADB-reverse tunnel, which
already has retransmits at the OS level — no FEC there.

## Consequences

- Bandwidth overhead: ~28% on UDP. At 100 Mbps target bitrate, ~128 Mbps
  actual. Wi-Fi 6E handles this comfortably.
- Worst-case loss recovery: 4 lost shards out of 14, recovered in O(n²) but
  for small n. Measured cost: <50 µs per frame on Apple Silicon.
- Frames that exceed FEC capacity drop entirely. The encoder forces IDR every
  N frames (configurable, default 240) so resync time is bounded.

## Alternatives considered

- **ARQ.** Latency-incompatible.
- **Fountain codes (LT, Raptor).** Better for huge files; overkill for our
  small frame sizes and adds patent risk (Raptor) for an open-source project.
- **No FEC, IDR on every loss.** Tested in M0 spike planning; the IDR storm
  ate the bitrate budget. Rejected.
