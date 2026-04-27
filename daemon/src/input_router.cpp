// SPDX-License-Identifier: Apache-2.0
#include "fuvr/input_router.hpp"

#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr.capnp.h"
#include "fuvrd.capnp.h"

namespace fuvr::daemon {

namespace {
struct Slot {
    bool  filled = false;
    bool  active = false;
    float trigger = 0, squeeze = 0, thumbstickX = 0, thumbstickY = 0, thumbrest = 0;
    bool  thumbstickClick = false, thumbstickTouch = false, triggerTouch = false;
    bool  buttonAClick = false, buttonATouch = false;
    bool  buttonBClick = false, buttonBTouch = false;
    bool  systemClick = false;
};

void writeController(::fuvr::daemon::ControllerInput::Builder b, const Slot& s) {
    b.setActive(s.active);
    b.setTrigger(s.trigger);
    b.setSqueeze(s.squeeze);
    b.setThumbstickX(s.thumbstickX);
    b.setThumbstickY(s.thumbstickY);
    b.setThumbstickClick(s.thumbstickClick);
    b.setThumbstickTouch(s.thumbstickTouch);
    b.setTriggerTouch(s.triggerTouch);
    b.setButtonAClick(s.buttonAClick);
    b.setButtonATouch(s.buttonATouch);
    b.setButtonBClick(s.buttonBClick);
    b.setButtonBTouch(s.buttonBTouch);
    b.setSystemClick(s.systemClick);
    b.setThumbrest(s.thumbrest);
}
} // namespace

uint64_t InputRouter::addSubscriber(uint64_t sessionId, InputSubscriber cb) {
    std::lock_guard lk(mu_);
    uint64_t id = nextStreamId_++;
    subs_[id] = Entry{sessionId, std::move(cb)};
    return id;
}

void InputRouter::removeSubscriber(uint64_t streamId) {
    std::lock_guard lk(mu_);
    subs_.erase(streamId);
}

bool InputRouter::ingestPackedUpstreamFrame(const uint8_t* data, std::size_t len,
                                            uint64_t sessionId, uint64_t receivedAtNs) {
    kj::ArrayInputStream is(kj::arrayPtr(data, len));
    ::capnp::PackedMessageReader reader(is);
    auto frame = reader.getRoot<::fuvr::proto::UpstreamFrame>();

    Slot left{}, right{};
    auto inputs = frame.getInputs();
    for (auto in : inputs) {
        Slot* dst = nullptr;
        if (in.getHand() == ::fuvr::proto::ControllerHand::LEFT) dst = &left;
        else if (in.getHand() == ::fuvr::proto::ControllerHand::RIGHT) dst = &right;
        if (!dst) continue;
        dst->filled          = true;
        dst->active          = true;
        dst->trigger         = in.getTrigger();
        dst->squeeze         = in.getSqueeze();
        dst->thumbstickX     = in.getThumbstickX();
        dst->thumbstickY     = in.getThumbstickY();
        dst->thumbstickClick = in.getThumbstickClick();
        dst->thumbstickTouch = in.getThumbstickTouch();
        dst->triggerTouch    = in.getTriggerTouch();
        dst->buttonAClick    = in.getButtonAClick();
        dst->buttonATouch    = in.getButtonAtouch();
        dst->buttonBClick    = in.getButtonBClick();
        dst->buttonBTouch    = in.getButtonBtouch();
        dst->systemClick     = in.getSystemClick();
        dst->thumbrest       = in.getThumbrest();
    }

    uint64_t questClock = frame.getHmd().getTimestampNs();

    std::vector<std::pair<uint64_t, InputSubscriber>> targets;
    {
        std::lock_guard lk(mu_);
        targets.reserve(subs_.size());
        for (auto& [id, e] : subs_) {
            if (e.sessionId == sessionId) targets.emplace_back(id, e.cb);
        }
    }
    if (targets.empty()) return true;

    for (auto& [streamId, cb] : targets) {
        ::capnp::MallocMessageBuilder out;
        auto env = out.initRoot<::fuvr::daemon::Envelope>();
        env.setSeq(0);
        env.setStreamId(streamId);
        auto snap = env.getBody().initInputSnapshot();
        snap.setReceivedAtNs(receivedAtNs);
        snap.setQuestClockNs(questClock);
        writeController(snap.initLeft(),  left);
        writeController(snap.initRight(), right);
        kj::VectorOutputStream os;
        ::capnp::writePackedMessage(os, out);
        auto bytes = os.getArray();
        cb(bytes.begin(), bytes.size());
    }
    return true;
}

} // namespace fuvr::daemon
