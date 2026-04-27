// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <CoreVideo/CoreVideo.h>

namespace fuvr {

enum class VideoCodec { Hevc, H264 };

struct EncoderConfig {
    uint32_t width;
    uint32_t height;
    uint32_t targetBitrateBps;
    uint32_t framerateHz;
    VideoCodec codec;
    bool realTime = true;
    uint32_t maxKeyframeIntervalFrames = 240;
};

struct EncodedFragment {
    uint64_t frameId;
    uint64_t renderStartNs;
    bool     isKeyframe;
    bool     endOfFrame;
    bool     isCsd;
    const uint8_t* data;
    size_t   size;
};

class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void onFragment(const EncodedFragment&) = 0;
};

class Encoder {
public:
    static std::unique_ptr<Encoder> create(const EncoderConfig&, FrameSink* sink);
    virtual ~Encoder() = default;
    virtual bool submit(CVPixelBufferRef pixelBuffer,
                        uint64_t frameId,
                        uint64_t renderStartNs,
                        bool forceIdr) = 0;
    virtual void flush() = 0;
};

} // namespace fuvr
