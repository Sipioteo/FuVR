# Troubleshooting

User-facing common issues, in roughly the order you'll hit them on a fresh
install. Each entry follows the same shape: **symptom → diagnostic → fix.**

If none of the below match, please open a bug report with the output of
`fuvrctl status` and `fuvrctl bench` attached.

---

## 1. The daemon won't start

**Symptom**
You ran `fuvrctl install` (or `scripts/install-launchd.sh`) and the Mac
app reports "daemon unreachable" or `xrCreateInstance` returns
`XR_ERROR_RUNTIME_UNAVAILABLE`.

**Diagnostic**
```bash
launchctl list | grep fuvr
fuvrctl status
tail -n 100 /tmp/fuvrd.err.log
```

If `launchctl list` shows the service with a non-zero exit code in the
last column, the daemon crashed on launch. Logs at
`/tmp/fuvrd.{out,err}.log` carry the stack and the panic message.

**Fix**

- "binary not found" → `cmake --build build` first, or
  `FUVRD=/full/path/to/fuvrd scripts/install-launchd.sh`.
- "Operation not permitted" → System Settings → Privacy & Security →
  Screen Recording must include `fuvrd` (the encoder pulls frames via
  `CGDisplayStream`).
- Any other crash → file a bug with the contents of the err log.

---

## 2. OpenXR apps don't see FuVR

**Symptom**
Your OpenXR-using app launches but reports no runtime, or picks a
different one (Monado, SteamVR, etc.).

**Diagnostic**
```bash
cat "$HOME/Library/Application Support/OpenXR/1/active_runtime.json"
```

The contents should look roughly like:
```json
{
  "file_format_version": "1.0.0",
  "runtime": {
    "library_path": "/Users/you/Library/Application Support/OpenXR/1/runtime.dylib",
    "name": "FuVR",
    "MND_egl_enable": true
  }
}
```

If the file is missing, the loader has nothing to find. If `library_path`
points at a stale build dir, FuVR was uninstalled but the JSON wasn't
rewritten.

**Fix**

```bash
fuvrctl install   # rewrites the JSON and copies the latest dylib
```

If a different runtime is "winning", check the
`XR_RUNTIME_JSON` environment variable — when set, it overrides the
default search path. Unset it (`unset XR_RUNTIME_JSON`) and restart the
host app.

---

## 3. Quest can't find the Mac

**Symptom**
On the Quest, the FuVR app sits on "Searching for host…" forever. Both
devices are awake and on the same Wi-Fi (or USB-tethered).

**Diagnostic**

Discovery uses Bonjour/mDNS — see ADR-0009 for the wire details. Probe
from the Mac:

```bash
dns-sd -B _fuvr._udp
# Should list a single instance once `fuvrd` is up.

dns-sd -L "FuVR" _fuvr._udp local
# Confirms the advertised port + TXT record.
```

From a Linux box on the same network, `avahi-browse -r _fuvr._udp` does
the same thing.

**Fix**

- Nothing listed → the daemon isn't advertising. Re-run `fuvrctl status`
  and check the err log.
- Listed but headset still can't connect → mDNS often fails across
  Wi-Fi APs / VLANs. Use the USB fallback:
  ```bash
  fuvrctl quest reverse
  ```
  Then in the FuVR app on the Quest, switch the connection mode to
  **"USB (adb reverse)"** and reconnect.
- "Host found but auth failed" → the daemon and the client are on
  incompatible wire versions. Reinstall both from the same release.

---

## 4. Latency feels off

**Symptom**
The image looks fine but motion-to-photon is visibly worse than what the
release notes claim, or worse than the last build.

**Diagnostic**
```bash
fuvrctl bench
```

The generated `bench-output/M0-report-*.md` has the four spike numbers
that tell you which leg is slow:

| Spike | What it covers |
|---|---|
| `fuvr-encode-synthetic` | Encoder `submit→onEncoded` |
| `transport loopback-bench` | UDP + FEC pipeline only |
| `fuvr-vdisplay-helper` | `CGVirtualDisplay` end-to-end probe |
| `fuvr-runtime-metrics` | Runtime↔daemon round-trip |

Compare each leg to the numbers in the most recent release notes.

**Fix**

- Encoder regression → check `mac-app/Settings`, switch the codec
  (HEVC ↔ AV1) and the IDR cadence. Realtime priority class should be on.
- Transport regression → confirm Wi-Fi 6E or USB. ADR-0005 explains why
  there's no ARQ; you may need a closer AP, not a different config.
- Runtime regression → make sure the dylib in `~/Library/.../OpenXR/1/`
  matches the daemon version (`fuvrctl status` prints both).

---

## 5. Encoded frames are huge / bandwidth saturates

**Symptom**
Network looks pegged and the report shows >40 Mbps average for the
encoder leg. Decoder on the Quest can't keep up.

**Diagnostic**
Open the Mac app → Settings → Encoder. The status bar at the bottom
shows the current bitrate target, the realized bitrate, and the IDR
cadence in frames.

**Fix**

- Drop the bitrate target. 30 Mbps is plenty for HEVC at typical
  resolutions; AV1 should sit lower.
- Push the IDR interval out (default is 1s; try 2s for steady scenes).
- Confirm the codec — the synthetic spike encodes static frames cheaply
  and may underestimate the dynamic-scene case. Run with a real game.

If the bitrate target is being honored but the visible frames look
garbage on the Quest, the FEC ratio is probably masking real loss
beyond what ADR-0005 covers — drop the resolution one tier and re-bench.
