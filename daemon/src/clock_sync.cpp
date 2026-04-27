// SPDX-License-Identifier: Apache-2.0
#include "fuvr/clock_sync.hpp"

#include <algorithm>
#include <thread>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr.capnp.h"

namespace fuvr::daemon {

namespace {
uint64_t nowMonoNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace

void ClockSync::issuePing(const SendFn& send) {
    if (!send) return;
    uint64_t t0 = nowMonoNs();
    {
        std::lock_guard lk(mu_);
        pending_.push_back({t0, Pending{t0}});
        // Why: bound memory if pongs are dropped.
        while (pending_.size() > 64) pending_.pop_front();
    }
    ::capnp::MallocMessageBuilder mb;
    auto cm = mb.initRoot<::fuvr::proto::ControlMessage>();
    auto cs = cm.initClockSync();
    cs.initPing().setT0(t0);
    kj::VectorOutputStream os;
    ::capnp::writePackedMessage(os, mb);
    auto bytes = os.getArray();
    send(bytes.begin(), bytes.size());
}

void ClockSync::onPong(uint64_t t0, uint64_t t1, uint64_t t2) {
    uint64_t tRecv = nowMonoNs();
    uint64_t tSend = 0;
    {
        std::lock_guard lk(mu_);
        bool found = false;
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->first == t0) {
                tSend = it->second.tSendNs;
                pending_.erase(it);
                found = true;
                break;
            }
        }
        if (!found) return;
        if (tRecv > tSend && (tRecv - tSend) > kMaxAgeNs) return;
    }
    feedSynthetic(tSend, t1, t2, tRecv);
}

void ClockSync::feedSynthetic(uint64_t tSendNs, uint64_t t1Ns, uint64_t t2Ns,
                              uint64_t tRecvNs) {
    int64_t rttMacSpan  = static_cast<int64_t>(tRecvNs) - static_cast<int64_t>(tSendNs);
    int64_t questSpan   = static_cast<int64_t>(t2Ns)    - static_cast<int64_t>(t1Ns);
    int64_t delay2x     = rttMacSpan - questSpan;
    if (delay2x < 0) delay2x = 0;
    uint64_t delayNs    = static_cast<uint64_t>(delay2x / 2);

    int64_t offsetNs = ((static_cast<int64_t>(t1Ns) - static_cast<int64_t>(tSendNs)) +
                        (static_cast<int64_t>(t2Ns) - static_cast<int64_t>(tRecvNs))) / 2;

    std::lock_guard lk(mu_);
    pushSampleLocked(offsetNs, delayNs, nowMonoNs());
}

void ClockSync::pushSampleLocked(int64_t offsetNs, uint64_t delayNs, uint64_t nowNs) {
    samples_.push_back({nowNs, offsetNs, delayNs});
    while (samples_.size() > kWindow) samples_.pop_front();
    evictOldLocked(nowNs);
}

void ClockSync::evictOldLocked(uint64_t nowNs) {
    while (!samples_.empty() &&
           nowNs > samples_.front().recordedAtNs &&
           (nowNs - samples_.front().recordedAtNs) > kMaxAgeNs) {
        samples_.pop_front();
    }
}

ClockSync::Snapshot ClockSync::snapshot() const {
    std::lock_guard lk(mu_);
    Snapshot s;
    if (samples_.empty()) return s;

    std::vector<int64_t>  offs; offs.reserve(samples_.size());
    std::vector<uint64_t> dels; dels.reserve(samples_.size());
    for (const auto& sm : samples_) {
        offs.push_back(sm.offsetNs);
        dels.push_back(sm.oneWayDelayNs);
    }
    auto medianI = [](std::vector<int64_t>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    auto medianU = [](std::vector<uint64_t>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    s.offsetNs      = medianI(offs);
    s.oneWayDelayNs = medianU(dels);
    s.samples       = static_cast<uint32_t>(samples_.size());

    double mean = 0.0;
    for (const auto& sm : samples_) mean += static_cast<double>(sm.offsetNs);
    mean /= static_cast<double>(samples_.size());
    double var = 0.0;
    for (const auto& sm : samples_) {
        double d = static_cast<double>(sm.offsetNs) - mean;
        var += d * d;
    }
    s.varianceNs = var / static_cast<double>(samples_.size());
    return s;
}

ClockSync::Snapshot ClockSync::waitForFirst(std::chrono::milliseconds timeout) const {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard lk(mu_);
            if (!samples_.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return snapshot();
}

} // namespace fuvr::daemon
