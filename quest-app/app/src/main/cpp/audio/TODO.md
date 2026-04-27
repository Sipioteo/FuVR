# Quest audio — TODO

## Post-1.0: Quest microphone (uplink)

A Quest-to-Mac audio path is **out of scope for 1.0** but the architecture
already accommodates it cleanly. Sketch:

1. New transport channel `AudioMic` (would require a wire schema bump — the
   current `Audio` channel is documented `MacToQuest`).
2. AAudio input stream (`AAUDIO_DIRECTION_INPUT`, `AAUDIO_PERFORMANCE_MODE_LOW_LATENCY`)
   capturing mono 48 kHz S16, 20 ms frames.
3. Opus encoder mirroring the Mac-side `OpusEncoderWrap` (mono, low-delay).
4. Daemon-side `MicReceiver` decoding Opus and surfacing PCM to whatever the
   Mac runtime / app wants to do with it (per SPEC §5.M4 line item).

Microphone permission flow on Quest requires `RECORD_AUDIO` runtime grant; the
existing app manifest does not declare it. Hold off until M4 stabilization.

## Misc

- Switch to `OPUS_SET_INBAND_FEC` once the transport reports plr to the encoder.
- Explore AAudio `MMAP` exclusive mode on Quest 3 specifically — anecdotal
  reports of <8 ms callback intervals.
