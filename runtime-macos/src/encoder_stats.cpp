// SPDX-License-Identifier: Apache-2.0
#include "fuvr/runtime.hpp"

#include <algorithm>
#include <vector>

namespace fuvr::runtime {

void EncoderStats::push(const EncodeStatSample& s) noexcept {
  std::lock_guard<std::mutex> lk(mu_);
  ring_[head_] = s;
  head_ = (head_ + 1) % kWindow;
  if (count_ < kWindow) ++count_;
}

EncoderStatsSnapshot EncoderStats::snapshot() const noexcept {
  std::lock_guard<std::mutex> lk(mu_);
  EncoderStatsSnapshot out{};
  if (count_ == 0) return out;
  out.sampleCount = static_cast<uint32_t>(count_);

  std::vector<double> durMs;
  durMs.reserve(count_);
  uint64_t sumBytes = 0;
  uint64_t earliestArrival = UINT64_MAX;
  uint64_t latestArrival = 0;
  for (std::size_t i = 0; i < count_; ++i) {
    const auto& s = ring_[i];
    durMs.push_back(static_cast<double>(s.encodeDurationNs) / 1.0e6);
    sumBytes += s.encodedSizeBytes;
    if (s.arrivalNs != 0) {
      earliestArrival = std::min(earliestArrival, s.arrivalNs);
      latestArrival = std::max(latestArrival, s.arrivalNs);
    }
  }
  double meanMs = 0.0;
  for (double v : durMs) meanMs += v;
  meanMs /= static_cast<double>(durMs.size());
  out.meanEncodeMs = meanMs;

  std::vector<double> sorted = durMs;
  std::sort(sorted.begin(), sorted.end());
  std::size_t p95Idx = static_cast<std::size_t>(0.95 * (sorted.size() - 1));
  out.p95EncodeMs = sorted[p95Idx];

  if (latestArrival > earliestArrival && earliestArrival != UINT64_MAX) {
    const double windowSec =
        static_cast<double>(latestArrival - earliestArrival) / 1.0e9;
    if (windowSec > 0.0) {
      out.fps = static_cast<double>(count_ - 1) / windowSec;
      out.bitrateMbps =
          (static_cast<double>(sumBytes) * 8.0) / (windowSec * 1.0e6);
    }
  }
  return out;
}

}  // namespace fuvr::runtime
