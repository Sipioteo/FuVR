// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace fuvr::audio {

// Pulled S16 stereo PCM at 48 kHz. `numFrames` is samples per channel; the
// buffer holds `numFrames * 2` int16_t values interleaved L,R,L,R...
// `hostTimeNs` is the mach_absolute_time-derived host time of the first
// sample in the buffer, in nanoseconds.
using CaptureCallback = std::function<void(const int16_t* frames,
                                           std::size_t numFrames,
                                           std::uint64_t hostTimeNs)>;

class Capture {
public:
    static constexpr std::uint32_t kSampleRate = 48000;
    static constexpr std::uint32_t kChannels   = 2;

    virtual ~Capture() = default;

    static std::unique_ptr<Capture> create(CaptureCallback cb);

    virtual bool start() = 0;
    virtual void stop()  = 0;
};

} // namespace fuvr::audio
