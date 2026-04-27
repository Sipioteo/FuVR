// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fuvr::daemon {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

struct LogEntry {
    uint64_t    timestampNs = 0;
    LogLevel    level = LogLevel::Info;
    std::string module;
    std::string message;
};

// Subscriber receives raw bytes representing a packed Envelope carrying a
// LogLine.
using LogSubscriber = std::function<void(const uint8_t*, std::size_t)>;

class Logger {
public:
    static Logger& instance();

    void log(LogLevel lvl, const char* module, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    // Subscribe to live log stream. Replays the ring buffer first, then
    // streams new entries. Returns the assigned streamId.
    uint64_t subscribe(uint64_t streamId, LogSubscriber cb);
    void unsubscribe(uint64_t streamId);

    // Test hook.
    [[nodiscard]] std::vector<LogEntry> recent() const;

private:
    Logger() = default;
    void emit(const LogEntry& e);
    void writeStderr(const LogEntry& e);
    void dispatch(const LogEntry& e);

    static constexpr std::size_t kRingCapacity = 256;

    mutable std::mutex mu_;
    std::array<LogEntry, kRingCapacity> ring_{};
    std::size_t ringHead_ = 0;
    std::size_t ringCount_ = 0;
    std::unordered_map<uint64_t, LogSubscriber> subs_;
};

#define FUVR_LOG_INFO(mod, ...)  ::fuvr::daemon::Logger::instance().log(::fuvr::daemon::LogLevel::Info,  (mod), __VA_ARGS__)
#define FUVR_LOG_WARN(mod, ...)  ::fuvr::daemon::Logger::instance().log(::fuvr::daemon::LogLevel::Warn,  (mod), __VA_ARGS__)
#define FUVR_LOG_ERROR(mod, ...) ::fuvr::daemon::Logger::instance().log(::fuvr::daemon::LogLevel::Error, (mod), __VA_ARGS__)

} // namespace fuvr::daemon
