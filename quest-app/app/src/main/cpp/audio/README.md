# quest-app audio

Quest-side audio receive path. Decodes `proto::AudioPacket` envelopes from
the transport `Audio` channel via libopus and renders to AAudio.

## Components

- `OpusAudioReceiver` — parses packed Cap'n Proto envelopes, decodes Opus.
  Cap'n Proto is reached through `audio_receiver.cpp` only, which is the one
  TU compiled with `-fexceptions -frtti`. Everything else stays no-exception
  to match the rest of `fuvr_quest`.
- `AAudioOutput` — low-latency stereo S16 stream. Internal ring buffer is
  sized for ~40 ms; underruns return silence rather than blocking.
- `router_glue.cpp` — registration shim used by DELTA's `protocol_router`.

## Integration with `protocol_router.cpp` (DELTA)

DELTA's router calls these once at startup and tears down at shutdown:

```cpp
#include "fuvr/audio/router_glue.hpp"

auto audioHandler = fuvr::audio::install_audio_handler(48000, 2);
// inside the Channel::Audio case of the inbound dispatcher:
//   audioHandler(data, size);

// at shutdown:
fuvr::audio::shutdown_audio_handler();
```

That's the only contract — the router does not need to know about
`AAudioOutput` or `OpusAudioReceiver` types.

## Build

The Android NDK build pulls libopus 1.5.2 via ExternalProject (mirroring the
Cap'n Proto build pattern in the parent `CMakeLists.txt`). No prebuilt AAR.

The host-side smoke test (`test_audio_decode.cpp`) is intentionally not
compiled by the Android toolchain — it is exercised on macOS host builds
against Homebrew libopus. See the daemon and encoder CMakeLists for that
path.
