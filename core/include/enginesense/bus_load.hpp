#pragma once
#include <array>
#include <cstdint>

namespace enginesense {

/// Sliding-window CAN bus load estimator (20 buckets x 50 ms = 1 s window by default).
/// O(1) per frame, no allocation.
class BusLoadMeter {
 public:
  static constexpr std::size_t kBuckets = 20;

  explicit BusLoadMeter(std::uint32_t bitrate_bps = 250'000, std::uint64_t bucket_us = 50'000)
      : bitrate_(bitrate_bps), bucket_us_(bucket_us) {
    reset();
  }

  void reset() {
    bits_.fill(0);
    ids_.fill(kEmpty);
  }

  void add(std::uint64_t ts_us, std::uint32_t bits) {
    const std::uint64_t id = ts_us / bucket_us_;
    const std::size_t idx = static_cast<std::size_t>(id % kBuckets);
    if (ids_[idx] != id) {
      ids_[idx] = id;
      bits_[idx] = 0;
    }
    bits_[idx] += bits;
  }

  /// Fraction of bus capacity used over the window ending at `now_us` (can exceed 1.0 only
  /// through estimation error; callers treat >= 1.0 as saturated).
  double load(std::uint64_t now_us) const {
    const std::uint64_t now_id = now_us / bucket_us_;
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
      const std::uint64_t id = ids_[i];
      if (id != kEmpty && id <= now_id && now_id - id < kBuckets) sum += bits_[i];
    }
    const double window_s = static_cast<double>(kBuckets * bucket_us_) / 1e6;
    return static_cast<double>(sum) / (static_cast<double>(bitrate_) * window_s);
  }

 private:
  static constexpr std::uint64_t kEmpty = ~std::uint64_t{0};
  std::uint32_t bitrate_;
  std::uint64_t bucket_us_;
  std::array<std::uint64_t, kBuckets> bits_{};
  std::array<std::uint64_t, kBuckets> ids_{};
};

}  // namespace enginesense
