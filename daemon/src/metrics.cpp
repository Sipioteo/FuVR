// SPDX-License-Identifier: Apache-2.0
#include "fuvr/metrics.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace fuvr::daemon {

void RollingWindow::push(double sample) {
    std::lock_guard lk(mu_);
    buf_[head_] = sample;
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;
}

std::size_t RollingWindow::size() const {
    std::lock_guard lk(mu_);
    return count_;
}

double RollingWindow::mean() const {
    std::lock_guard lk(mu_);
    if (count_ == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < count_; ++i) sum += buf_[i];
    return sum / static_cast<double>(count_);
}

double RollingWindow::percentile(double p) const {
    std::lock_guard lk(mu_);
    if (count_ == 0) return 0.0;
    std::vector<double> sorted(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(count_));
    std::sort(sorted.begin(), sorted.end());
    double idx = p * static_cast<double>(count_ - 1);
    auto lo = static_cast<std::size_t>(idx);
    auto hi = std::min(lo + 1, count_ - 1);
    double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

void RollingWindow::reset() {
    std::lock_guard lk(mu_);
    head_ = 0;
    count_ = 0;
}

void MetricsAggregator::recordEncode(uint64_t durationNs, uint32_t sizeBytes) {
    encodeMs_.push(static_cast<double>(durationNs) / 1.0e6);
    encodeBytes_.push(static_cast<double>(sizeBytes));

    std::lock_guard lk(mu_);
    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    ++framesSinceLast_;
    if (lastFpsSampleNs_ == 0) {
        lastFpsSampleNs_ = now;
        return;
    }
    uint64_t deltaNs = now - lastFpsSampleNs_;
    if (deltaNs >= 1'000'000'000ULL) {
        currentFps_ = static_cast<float>(static_cast<double>(framesSinceLast_) * 1.0e9 /
                                          static_cast<double>(deltaNs));
        framesSinceLast_ = 0;
        lastFpsSampleNs_ = now;
    }
}

MetricsSnapshot MetricsAggregator::snapshot() const {
    MetricsSnapshot s;
    s.encoderEncodeMsAvg = static_cast<float>(encodeMs_.mean());
    s.encoderEncodeMsP95 = static_cast<float>(encodeMs_.percentile(0.95));
    double meanBytes = encodeBytes_.mean();
    {
        std::lock_guard lk(mu_);
        s.encoderFps = currentFps_;
        s.transportRttMs = rttMs_;
        s.transportLossPct = lossPct_;
        s.videoBitrateMbps = static_cast<float>(meanBytes * 8.0 *
                                                static_cast<double>(currentFps_) / 1.0e6);
    }
    return s;
}

void MetricsAggregator::setTransportStats(float rttMs, float lossPct) {
    std::lock_guard lk(mu_);
    rttMs_ = rttMs;
    lossPct_ = lossPct;
}

} // namespace fuvr::daemon
