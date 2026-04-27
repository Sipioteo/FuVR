# ADR-0003: Cap'n Proto for the wire, JSON for the local control plane

- Status: accepted
- Date: 2026-04-27

## Context

We have two distinct serialisation needs:

1. **Wire format** — Mac↔Quest, 90 Hz video fragments, 1 kHz pose, low-rate
   audio and control. Latency-critical, throughput-sensitive, parsed in C++ on
   both ends, must be stable across versions.
2. **Local control plane** — `mac-app` (SwiftUI) ↔ daemon, ≤10 Hz, status
   updates, settings push, log streaming. Latency-irrelevant, schema churns
   constantly during early development.

## Decision

The wire format is **Cap'n Proto**, schema in `proto/fuvr.capnp`, schema id
frozen at `@0xb1f5d4f7c2a830e5;`. CI guards the id (`proto-check.yml`).

The mac-app↔daemon control plane is **line-delimited JSON** over a Unix
domain socket, with a versioned envelope `{ "v": 1, "type": "...", "payload": ... }`.
The arms intentionally mirror the `ControlMessage` Cap'n Proto union plus a
couple of UI-only types (`metrics`, `log`).

## Consequences

- The Quest reads pose and writes video at GPU thread speed without parsing
  cost — Cap'n Proto's zero-copy read pays off here.
- The control plane changes weekly during development without forcing a
  schema regeneration cycle and without breaking older daemon binaries: JSON
  silently ignores unknown keys.
- We have one extra serialiser (Swift `Codable` mirror types) that has to
  stay in sync with the Cap'n Proto union for the overlapping arms. The
  surface is small (~7 cases) and unit-tested.

## Alternatives considered

- **Cap'n Proto everywhere**, including the local socket. Rejected because
  schema regeneration on every UI iteration kills development velocity, and
  Swift Cap'n Proto support is third-party and unmaintained.
- **JSON everywhere**, including the wire. Rejected: parsing cost at 1 kHz
  pose × controller × input on the Quest is not free, and schema evolution
  for binary data (video fragment headers) is awkward in JSON.
- **Protocol Buffers** for the wire. Rejected: nominally similar to Cap'n
  Proto but lacks zero-copy reads, which matters specifically for the pose
  hot path.
