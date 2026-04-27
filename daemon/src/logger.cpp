// SPDX-License-Identifier: Apache-2.0
#include "fuvr/logger.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvrd.capnp.h"

namespace fuvr::daemon {

namespace {
uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

const char* levelTag(LogLevel l) {
    switch (l) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}

void encodeEnvelope(uint64_t streamId, const LogEntry& e, std::vector<uint8_t>& out) {
    ::capnp::MallocMessageBuilder mb;
    auto env = mb.initRoot<::fuvr::daemon::Envelope>();
    env.setSeq(0);
    env.setStreamId(streamId);
    auto ll = env.getBody().initLog();
    ll.setTimestampNs(e.timestampNs);
    ll.setLevel(static_cast<uint8_t>(e.level));
    ll.setModule(e.module);
    ll.setMessage(e.message);
    kj::VectorOutputStream os;
    ::capnp::writePackedMessage(os, mb);
    auto a = os.getArray();
    out.assign(a.begin(), a.end());
}
} // namespace

Logger& Logger::instance() {
    static Logger g;
    return g;
}

void Logger::log(LogLevel lvl, const char* module, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    LogEntry e;
    e.timestampNs = nowNs();
    e.level = lvl;
    e.module = module ? module : "";
    e.message = buf;
    emit(e);
}

void Logger::emit(const LogEntry& e) {
    writeStderr(e);
    std::vector<std::pair<uint64_t, LogSubscriber>> targets;
    {
        std::lock_guard lk(mu_);
        ring_[ringHead_] = e;
        ringHead_ = (ringHead_ + 1) % kRingCapacity;
        if (ringCount_ < kRingCapacity) ++ringCount_;
        targets.reserve(subs_.size());
        for (auto& [id, cb] : subs_) targets.emplace_back(id, cb);
    }
    for (auto& [id, cb] : targets) {
        std::vector<uint8_t> out;
        encodeEnvelope(id, e, out);
        cb(out.data(), out.size());
    }
}

void Logger::writeStderr(const LogEntry& e) {
    std::fprintf(stderr, "[%s] %s: %s\n", levelTag(e.level),
                 e.module.c_str(), e.message.c_str());
}

void Logger::dispatch(const LogEntry&) {}

uint64_t Logger::subscribe(uint64_t streamId, LogSubscriber cb) {
    std::vector<LogEntry> replay;
    {
        std::lock_guard lk(mu_);
        replay.reserve(ringCount_);
        std::size_t start = (ringHead_ + kRingCapacity - ringCount_) % kRingCapacity;
        for (std::size_t i = 0; i < ringCount_; ++i) {
            replay.push_back(ring_[(start + i) % kRingCapacity]);
        }
        subs_[streamId] = cb;
    }
    for (const auto& e : replay) {
        std::vector<uint8_t> out;
        encodeEnvelope(streamId, e, out);
        cb(out.data(), out.size());
    }
    return streamId;
}

void Logger::unsubscribe(uint64_t streamId) {
    std::lock_guard lk(mu_);
    subs_.erase(streamId);
}

std::vector<LogEntry> Logger::recent() const {
    std::lock_guard lk(mu_);
    std::vector<LogEntry> out;
    out.reserve(ringCount_);
    std::size_t start = (ringHead_ + kRingCapacity - ringCount_) % kRingCapacity;
    for (std::size_t i = 0; i < ringCount_; ++i) {
        out.push_back(ring_[(start + i) % kRingCapacity]);
    }
    return out;
}

} // namespace fuvr::daemon
