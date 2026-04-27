# encoder-macos TODO

- Wire `VideoFragmentHeader` (Cap'n Proto) onto fragments at the transport
  boundary; encoder currently emits raw Annex-B bytes only.
- UDP-friendly fragmentation: split large keyframe NALs into MTU-sized
  fragments with `fragmentIndex` / `fragmentCount`. Today every encoded
  frame is one fragment with `endOfFrame = true`.
- Expose encode latency stats (per-frame VT callback timestamp - submit
  timestamp) for the runtime to surface in HUD.
- Adaptive bitrate: react to transport-reported RTT/loss by adjusting
  `AverageBitRate` and `DataRateLimits` on the fly.
- Stereo: today we treat the input as a single side-by-side surface.
  Evaluate dual `VTCompressionSession` (one per eye) once we have real
  per-eye textures from the runtime; pick whichever wins on M2/M3.
- Investigate `kVTCompressionPropertyKey_HDRMetadataInsertionMode` and
  10-bit Main10 once Quest 3 HDR pipeline is sorted.
- Replace synthetic gradient tool with a Metal renderer that writes into an
  IOSurface-backed pixel buffer (closer to the real runtime path).
- Add a fuzz/stress test for AVCC->Annex-B conversion on truncated buffers.
