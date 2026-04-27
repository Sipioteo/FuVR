// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace fuvr::daemon {

class RollingWindow {
public:
    static constexpr std::size_t kCapacity = 256;

    void push(double sample);
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] double mean() const;
    [[nodiscard]] double percentile(double p) const;
    void reset();

private:
    mutable std::mutex mu_;
    std::array<double, kCapacity> buf_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

struct MetricsSnapshot {
    float encoderFps = 0.0f;
    float encoderEncodeMsAvg = 0.0f;
    float encoderEncodeMsP95 = 0.0f;
    float transportRttMs = 0.0f;
    float transportLossPct = 0.0f;
    float videoBitrateMbps = 0.0f;
};

class MetricsAggregator {
public:
    void recordEncode(uint64_t durationNs, uint32_t sizeBytes);
    [[nodiscard]] MetricsSnapshot snapshot() const;
    void setTransportStats(float rttMs, float lossPct);

private:
    mutable std::mutex mu_;
    RollingWindow encodeMs_;
    RollingWindow encodeBytes_;
    uint64_t lastFpsSampleNs_ = 0;
    uint64_t framesSinceLast_ = 0;
    float currentFps_ = 0.0f;
    float rttMs_ = 0.0f;
    float lossPct_ = 0.0f;
};

} // namespace fuvr::daemon
