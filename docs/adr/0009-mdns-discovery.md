# ADR-0009: mDNS / Bonjour discovery for Wi-Fi mode

- Status: accepted
- Date: 2026-04-27

## Context

USB mode uses ADB reverse on a fixed loopback port (9943). Wi-Fi mode has
no such anchor: the Mac's IP can change, the Quest's IP can change, the
LAN may have multiple Macs running fuvrd. We need automatic discovery so
the user does not have to type IP addresses.

## Decision

The daemon advertises an mDNS service on first start of a Wi-Fi-mode
session. The Quest browses for it on connect.

**Service type:** `_fuvr._udp.local.`

**TXT records:**
- `version=1` — protocol major version (frozen schema id implies this; the
  TXT field is a sanity belt)
- `codecs=hevc,h264` — intersection of supported codecs (Opus advertised
  separately when audio ships)
- `transport=udp` — explicit; future transports get their own service type
- `port=N` — UDP port the daemon is listening on (default 9943)

**Hostname:** the daemon publishes under its `gethostname()` value plus a
short suffix `-fuvr` so the same Mac can run multiple daemons (uncommon
but harmless).

**Mac side:** uses Apple's `dnssd` (`<dns_sd.h>`) directly via the
`astro-dnssd` Rust crate. The daemon registers via `DNSServiceRegister`
on session start, deregisters on stop.

**Quest side:** uses `NsdManager` from Android (Java side) via a small
JNI bridge into the C++ transport client. The Quest browses with
`discoverServices("_fuvr._udp")`; the first service that survives a
2-second resolution window is connected to. If the user pinned a specific
hostname in Quest-side settings (future feature), filter by it.

**Behavior:** if mDNS discovery fails after 5 s, the Quest falls back to
manually-entered IP from its settings UI. A "no service found" message
appears head-locked.

## Consequences

- Multi-Mac LANs work: the Quest finds the closest fuvrd. Manual override
  remains available.
- USB mode is unaffected — it does not use mDNS at all.
- The daemon registers as a user-mode service (`gui/$UID` launchd domain)
  and Bonjour treats it as such. Captive portals and corporate networks
  that block mDNS will degrade to manual entry.
- The TXT field set is short and stable; widening it later is additive.

## Alternatives considered

- **Hardcoded broadcast UDP discovery.** Works but is a custom protocol;
  mDNS is the documented Apple+Android pattern. Rejected.
- **QR-code pairing.** Requires the Quest to scan via passthrough camera;
  good UX but doesn't survive IP changes mid-session. Future enhancement
  alongside mDNS, not instead of it.
- **Hardcoded IPs in a config file.** Forces the user into network
  management. Rejected as the primary path.

## Open questions

- Whether to include the daemon's user account in the TXT record for
  multi-user Macs. Defer until we hit the case.
- Authentication / pairing: discovery alone does not pair. Pairing
  is a future ADR — for now any Quest on the LAN that finds the service
  can connect. This is appropriate for v1 LAN-only scope (SPEC §9
  excludes Internet streaming).
