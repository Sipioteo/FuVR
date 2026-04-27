# ADR-0001: Record architecture decisions

- Status: accepted
- Date: 2026-04-27

## Context

This project will live for years and pass through more than one maintainer.
Reasoning behind structural choices needs to outlive the conversation that
produced it, or the next maintainer will rediscover and re-litigate every
seam.

## Decision

We keep architecture decisions as short, numbered, immutable records in
`docs/adr/`. Format: Michael Nygard's classic four-section ADR (context,
decision, consequences, alternatives) with a status header.

Once accepted, an ADR is never edited. Superseding decisions get their own
ADR that links back ("supersedes ADR-NNNN").

## Consequences

- All non-trivial design choices are recorded in one searchable place.
- New contributors can read `docs/adr/` and reconstruct the design rationale.
- Cost: discipline. We have to actually write the ADR when we make the call.
