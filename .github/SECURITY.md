# Security policy

## Supported versions

FuVR is pre-1.0. Security fixes land on `main` and the most recent tagged
minor release. Older releases are not patched; please update.

## Reporting a vulnerability

**Do not open a public GitHub issue for security problems.**

Email **security@luminosfilm.com** with:

- A description of the issue and its impact.
- Steps to reproduce, or a proof-of-concept if you have one.
- The FuVR version / commit you tested against.
- Whether you are willing to be credited in the advisory.

Encrypt sensitive details with our PGP key if you prefer:

```
-----BEGIN PGP PUBLIC KEY BLOCK-----
(placeholder — replace with the real key fingerprint and ASCII-armored
 block before publishing the first signed release)

Fingerprint: 0000 0000 0000 0000 0000  0000 0000 0000 0000 0000
-----END PGP PUBLIC KEY BLOCK-----
```

## What to expect

- Acknowledgement within **3 business days**.
- An initial assessment within **7 business days**, including whether we
  consider the report in scope.
- A target fix date once severity is agreed. We aim for **30 days** for
  high-severity issues and **90 days** for everything else.
- A public advisory and CVE coordination on release of the fix, with
  credit to the reporter unless they request otherwise.

## In scope

- The macOS daemon (`fuvrd`), OpenXR runtime, encoder, vdisplay helper.
- The Quest companion app (the FuVR APK).
- The wire protocol (`proto/*.capnp`) and the transport crate.
- The release artifacts on GitHub Releases and the Homebrew tap.

## Out of scope

- Issues in third-party dependencies — please report those upstream and
  CC us if they affect FuVR users.
- Social-engineering or physical-access attacks.
- Reports requiring a malicious LaunchAgent or admin privileges already
  granted by the user.
- Denial of service against `fuvrd` from a process running as the same
  user — `fuvrd` is a user-level service and trusts its own UID.

## Hall of fame

We'll list reporters here once we have any. Thank you for keeping FuVR's
users safe.
