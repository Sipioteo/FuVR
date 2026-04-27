// SPDX-License-Identifier: Apache-2.0

// This translation unit is the only place that touches the Cap'n Proto C++
// runtime. It is built with -fexceptions -frtti (see CMakeLists.txt) so the
// rest of fuvr_quest can stay -fno-exceptions/-fno-rtti.

#include "proto_codec.hpp"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>
#include <kj/exception.h>

#include "proto_gen/fuvr.capnp.h"

#include <cstring>

namespace fuvr {

namespace {

void copy_pose(const PlainPose& in, fuvr::proto::Pose::Builder out) {
    auto pos = out.initPosition();
    pos.setX(in.px); pos.setY(in.py); pos.setZ(in.pz);
    auto rot = out.initOrientation();
    rot.setX(in.ox); rot.setY(in.oy); rot.setZ(in.oz); rot.setW(in.ow);
}

void copy_view(const PlainViewState& in, fuvr::proto::ViewState::Builder out) {
    copy_pose(in.pose, out.initPose());
    auto fov = out.initFov();
    fov.setAngleLeft(in.fov.angleLeft);
    fov.setAngleRight(in.fov.angleRight);
    fov.setAngleUp(in.fov.angleUp);
    fov.setAngleDown(in.fov.angleDown);
}

PlainPose read_pose(fuvr::proto::Pose::Reader r) {
    PlainPose p;
    auto pos = r.getPosition();
    p.px = pos.getX(); p.py = pos.getY(); p.pz = pos.getZ();
    auto q = r.getOrientation();
    p.ox = q.getX(); p.oy = q.getY(); p.oz = q.getZ(); p.ow = q.getW();
    return p;
}

PlainViewState read_view(fuvr::proto::ViewState::Reader r) {
    PlainViewState v;
    v.pose = read_pose(r.getPose());
    auto f = r.getFov();
    v.fov.angleLeft = f.getAngleLeft();
    v.fov.angleRight = f.getAngleRight();
    v.fov.angleUp = f.getAngleUp();
    v.fov.angleDown = f.getAngleDown();
    return v;
}

std::vector<uint8_t> serialize_packed(capnp::MessageBuilder& msg) {
    try {
        kj::VectorOutputStream out;
        capnp::writePackedMessage(out, msg);
        auto array = out.getArray();
        std::vector<uint8_t> result(array.size());
        std::memcpy(result.data(), array.begin(), array.size());
        return result;
    } catch (const kj::Exception&) {
        return {};
    } catch (...) {
        return {};
    }
}

}  // namespace

std::vector<uint8_t> encode_upstream_frame(const PlainUpstreamFrame& frame) {
    try {
        capnp::MallocMessageBuilder msg;
        auto root = msg.initRoot<fuvr::proto::UpstreamFrame>();
        root.setCorrelationFrameId(frame.correlationFrameId);

        auto hmd = root.initHmd();
        hmd.setTimestampNs(frame.hmd.timestampNs);
        hmd.setPredictedDisplayTimeNs(frame.hmd.predictedDisplayTimeNs);
        copy_view(frame.hmd.leftView, hmd.initLeftView());
        copy_view(frame.hmd.rightView, hmd.initRightView());
        auto lv = hmd.initLinearVelocity();
        lv.setX(frame.hmd.linVelX); lv.setY(frame.hmd.linVelY); lv.setZ(frame.hmd.linVelZ);
        auto av = hmd.initAngularVelocity();
        av.setX(frame.hmd.angVelX); av.setY(frame.hmd.angVelY); av.setZ(frame.hmd.angVelZ);

        auto controllers = root.initControllers(2);
        for (int i = 0; i < 2; ++i) {
            auto c = controllers[i];
            const auto& src = frame.controllers[i];
            c.setHand(src.hand == 0 ? fuvr::proto::ControllerHand::LEFT
                                    : fuvr::proto::ControllerHand::RIGHT);
            c.setIsActive(src.isActive);
            copy_pose(src.pose, c.initPose());
            auto cl = c.initLinearVelocity();
            cl.setX(src.linVelX); cl.setY(src.linVelY); cl.setZ(src.linVelZ);
            auto ca = c.initAngularVelocity();
            ca.setX(src.angVelX); ca.setY(src.angVelY); ca.setZ(src.angVelZ);
        }

        auto inputs = root.initInputs(2);
        for (int i = 0; i < 2; ++i) {
            auto t = inputs[i];
            const auto& s = frame.inputs[i];
            t.setHand(s.hand == 0 ? fuvr::proto::ControllerHand::LEFT
                                  : fuvr::proto::ControllerHand::RIGHT);
            t.setTrigger(s.trigger);
            t.setSqueeze(s.squeeze);
            t.setThumbstickX(s.thumbstickX);
            t.setThumbstickY(s.thumbstickY);
            t.setThumbstickClick(s.thumbstickClick);
            t.setThumbstickTouch(s.thumbstickTouch);
            t.setTriggerTouch(s.triggerTouch);
            t.setButtonAClick(s.buttonAClick);
            t.setButtonAtouch(s.buttonAtouch);
            t.setButtonBClick(s.buttonBClick);
            t.setButtonBtouch(s.buttonBtouch);
            t.setSystemClick(s.systemClick);
            t.setThumbrest(s.thumbrest);
        }

        return serialize_packed(msg);
    } catch (...) {
        return {};
    }
}

std::vector<uint8_t> encode_hello_from_quest(const PlainDeviceCapabilities& caps) {
    try {
        capnp::MallocMessageBuilder msg;
        auto root = msg.initRoot<fuvr::proto::ControlMessage>();
        auto hello = root.initHelloFromQuest();
        hello.setDeviceModel(caps.deviceModel);
        hello.setSystemVersion(caps.systemVersion);
        hello.setPerEyeWidth(caps.perEyeWidth);
        hello.setPerEyeHeight(caps.perEyeHeight);
        auto rates = hello.initRefreshRatesHz(caps.refreshRatesHz.size());
        for (size_t i = 0; i < caps.refreshRatesHz.size(); ++i)
            rates.set(i, caps.refreshRatesHz[i]);
        auto codecs = hello.initSupportedCodecs(caps.supportedCodecs.size());
        for (size_t i = 0; i < caps.supportedCodecs.size(); ++i) {
            codecs.set(i, caps.supportedCodecs[i] == 1 ? fuvr::proto::VideoCodec::H264
                                                       : fuvr::proto::VideoCodec::HEVC);
        }
        hello.setHasHandTracking(caps.hasHandTracking);
        hello.setHasEyeTracking(caps.hasEyeTracking);
        return serialize_packed(msg);
    } catch (...) {
        return {};
    }
}

std::vector<uint8_t> encode_clock_sync_pong(uint64_t t0, uint64_t t1, uint64_t t2) {
    try {
        capnp::MallocMessageBuilder msg;
        auto root = msg.initRoot<fuvr::proto::ControlMessage>();
        auto cs = root.initClockSync();
        auto pong = cs.initPong();
        pong.setT0(t0);
        pong.setT1(t1);
        pong.setT2(t2);
        return serialize_packed(msg);
    } catch (...) {
        return {};
    }
}

std::optional<size_t> decode_video_header(const uint8_t* data, size_t size,
                                          PlainVideoHeader& out) {
    try {
        kj::ArrayPtr<const kj::byte> input(reinterpret_cast<const kj::byte*>(data), size);
        kj::ArrayInputStream stream(input);
        capnp::PackedMessageReader reader(stream);
        auto root = reader.getRoot<fuvr::proto::VideoFragmentHeader>();

        out.frameId = root.getFrameId();
        out.renderStartNs = root.getRenderStartNs();
        out.renderedLeft = read_view(root.getRenderedLeft());
        out.renderedRight = read_view(root.getRenderedRight());
        out.totalSizeBytes = root.getTotalSizeBytes();
        out.fragmentIndex = root.getFragmentIndex();
        out.fragmentCount = root.getFragmentCount();
        out.codec = static_cast<int>(root.getCodec());
        out.flags = root.getFlags();
        out.targetDisplayTimeNs = root.getTargetDisplayTimeNs();

        // Bytes left in the stream are the raw codec payload.
        size_t remaining = stream.getReadBuffer().size();
        if (remaining > size) return std::nullopt;
        return size - remaining;
    } catch (...) {
        return std::nullopt;
    }
}

bool decode_control_message(const uint8_t* data, size_t size,
                            PlainControlMessage& out) {
    try {
        kj::ArrayPtr<const kj::byte> input(reinterpret_cast<const kj::byte*>(data), size);
        kj::ArrayInputStream stream(input);
        capnp::PackedMessageReader reader(stream);
        auto root = reader.getRoot<fuvr::proto::ControlMessage>();

        switch (root.which()) {
            case fuvr::proto::ControlMessage::HELLO_FROM_QUEST:
                out.kind = ControlKind::HelloFromQuest;
                break;
            case fuvr::proto::ControlMessage::HELLO_FROM_MAC: {
                out.kind = ControlKind::HelloFromMac;
                auto cfg = root.getHelloFromMac();
                out.sessionConfig.perEyeWidth = cfg.getPerEyeWidth();
                out.sessionConfig.perEyeHeight = cfg.getPerEyeHeight();
                out.sessionConfig.refreshRateHz = cfg.getRefreshRateHz();
                out.sessionConfig.videoCodec = static_cast<int>(cfg.getVideoCodec());
                out.sessionConfig.videoBitrateBps = cfg.getVideoBitrateBps();
                out.sessionConfig.audioEnabled = cfg.getAudioEnabled();
                break;
            }
            case fuvr::proto::ControlMessage::SESSION_START:
                out.kind = ControlKind::SessionStart;
                break;
            case fuvr::proto::ControlMessage::SESSION_STOP:
                out.kind = ControlKind::SessionStop;
                break;
            case fuvr::proto::ControlMessage::CLOCK_SYNC: {
                out.kind = ControlKind::ClockSync;
                auto cs = root.getClockSync();
                if (cs.isPing()) {
                    out.clockSync.isPing = true;
                    out.clockSync.t0 = cs.getPing().getT0();
                } else {
                    out.clockSync.isPing = false;
                    auto pg = cs.getPong();
                    out.clockSync.t0 = pg.getT0();
                    out.clockSync.t1 = pg.getT1();
                    out.clockSync.t2 = pg.getT2();
                }
                break;
            }
            case fuvr::proto::ControlMessage::HAPTIC: {
                out.kind = ControlKind::Haptic;
                auto h = root.getHaptic();
                out.haptic.hand = (h.getHand() == fuvr::proto::ControllerHand::LEFT) ? 0 : 1;
                out.haptic.durationNs = h.getDurationNs();
                out.haptic.amplitude = h.getAmplitude();
                out.haptic.frequencyHz = h.getFrequencyHz();
                break;
            }
            case fuvr::proto::ControlMessage::ERROR:
                out.kind = ControlKind::Error;
                out.errorText = root.getError();
                break;
            default:
                out.kind = ControlKind::None;
                break;
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace fuvr
