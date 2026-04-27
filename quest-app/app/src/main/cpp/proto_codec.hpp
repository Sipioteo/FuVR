// SPDX-License-Identifier: Apache-2.0
#pragma once

// Bridge between the no-exceptions/no-rtti rest of fuvr_quest and the
// Cap'n Proto C++ runtime (which requires exceptions and RTTI). Anything
// that touches capnp-generated headers lives in proto_codec.cpp and is
// compiled with -fexceptions -frtti via CMakeLists.txt set_source_files_properties.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fuvr {

struct PlainPose {
    float px{0}, py{0}, pz{0};
    float ox{0}, oy{0}, oz{0}, ow{1};
};

struct PlainFov {
    float angleLeft{0}, angleRight{0}, angleUp{0}, angleDown{0};
};

struct PlainViewState {
    PlainPose pose{};
    PlainFov fov{};
};

struct PlainHmdSample {
    uint64_t timestampNs{0};
    uint64_t predictedDisplayTimeNs{0};
    PlainViewState leftView{};
    PlainViewState rightView{};
    float linVelX{0}, linVelY{0}, linVelZ{0};
    float angVelX{0}, angVelY{0}, angVelZ{0};
};

struct PlainController {
    int hand{0}; // 0=left, 1=right
    bool isActive{false};
    PlainPose pose{};
    float linVelX{0}, linVelY{0}, linVelZ{0};
    float angVelX{0}, angVelY{0}, angVelZ{0};
};

struct PlainTouchInput {
    int hand{0};
    float trigger{0}, squeeze{0};
    float thumbstickX{0}, thumbstickY{0};
    bool thumbstickClick{false}, thumbstickTouch{false}, triggerTouch{false};
    bool buttonAClick{false}, buttonAtouch{false};
    bool buttonBClick{false}, buttonBtouch{false};
    bool systemClick{false};
    float thumbrest{0};
};

struct PlainUpstreamFrame {
    uint64_t correlationFrameId{0};
    PlainHmdSample hmd{};
    std::array<PlainController, 2> controllers{};
    std::array<PlainTouchInput, 2> inputs{};
};

struct PlainVideoHeader {
    uint64_t frameId{0};
    uint64_t renderStartNs{0};
    PlainViewState renderedLeft{};
    PlainViewState renderedRight{};
    uint32_t totalSizeBytes{0};
    uint32_t fragmentIndex{0};
    uint32_t fragmentCount{0};
    int codec{0}; // 0=hevc 1=h264
    uint16_t flags{0};
    uint64_t targetDisplayTimeNs{0};
};

// Bit positions for VideoFragmentHeader::flags (mirrors the capnp enum order).
constexpr uint16_t kFlagIdr        = 1u << 0;
constexpr uint16_t kFlagEndOfFrame = 1u << 1;
constexpr uint16_t kFlagCsdHeader  = 1u << 2;

struct PlainHaptic {
    int hand{0};
    uint64_t durationNs{0};
    float amplitude{0};
    float frequencyHz{0};
};

struct PlainSessionConfig {
    uint32_t perEyeWidth{0};
    uint32_t perEyeHeight{0};
    uint32_t refreshRateHz{0};
    int videoCodec{0};
    uint32_t videoBitrateBps{0};
    bool audioEnabled{false};
};

struct PlainClockSync {
    bool isPing{true};
    uint64_t t0{0}, t1{0}, t2{0};
};

struct PlainDeviceCapabilities {
    std::string deviceModel;
    std::string systemVersion;
    uint32_t perEyeWidth{0};
    uint32_t perEyeHeight{0};
    std::vector<uint32_t> refreshRatesHz;
    std::vector<int> supportedCodecs;
    bool hasHandTracking{false};
    bool hasEyeTracking{false};
};

enum class ControlKind {
    None,
    HelloFromQuest,
    HelloFromMac,
    SessionStart,
    SessionStop,
    ClockSync,
    Haptic,
    Error,
};

struct PlainControlMessage {
    ControlKind kind{ControlKind::None};
    PlainSessionConfig sessionConfig{};
    PlainClockSync clockSync{};
    PlainHaptic haptic{};
    std::string errorText;
};

// Outbound: serialize a packed UpstreamFrame to bytes. Returns empty on failure.
std::vector<uint8_t> encode_upstream_frame(const PlainUpstreamFrame& frame);

// Outbound: serialize a packed ControlMessage carrying helloFromQuest.
std::vector<uint8_t> encode_hello_from_quest(const PlainDeviceCapabilities& caps);

// Outbound: pong reply built from a received ping.
std::vector<uint8_t> encode_clock_sync_pong(uint64_t t0, uint64_t t1, uint64_t t2);

// Outbound: ControlMessage carrying the `error :Text` arm. We piggy-back
// telemetry on this arm pending a wire-schema bump that adds a Quest->Mac
// metrics arm; see quest-app/TODO.md.
std::vector<uint8_t> encode_error_message(const std::string& text);

// Inbound: parse a packed VideoFragmentHeader from the front of the payload,
// return how many bytes the header occupied (codec payload follows immediately).
// Returns std::nullopt on parse failure.
std::optional<size_t> decode_video_header(const uint8_t* data, size_t size,
                                          PlainVideoHeader& out);

// Inbound: parse a packed ControlMessage. Returns true on success.
bool decode_control_message(const uint8_t* data, size_t size,
                            PlainControlMessage& out);

}
