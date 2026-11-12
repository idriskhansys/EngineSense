#pragma once
#include <array>
#include <cstdint>

namespace enginesense {

/// One classic CAN 2.0B frame with a 29-bit identifier (J1939 uses extended IDs only).
struct CanFrame {
  std::uint64_t timestamp_us = 0;  ///< microseconds since start of the session / log
  std::uint32_t id = 0;            ///< 29-bit extended identifier
  std::uint8_t dlc = 8;            ///< data length code, 0..8
  std::array<std::uint8_t, 8> data{};
};

inline bool operator==(const CanFrame& a, const CanFrame& b) {
  return a.timestamp_us == b.timestamp_us && a.id == b.id && a.dlc == b.dlc && a.data == b.data;
}
inline bool operator!=(const CanFrame& a, const CanFrame& b) { return !(a == b); }

/// Estimated on-wire size of an extended frame in bits: SOF..EOF (64 + 8*dlc) plus 3 bits of
/// inter-frame space, scaled by 1.1 to approximate bit stuffing. Used for bus-load estimation.
inline std::uint32_t estimated_frame_bits(const CanFrame& f) {
  const std::uint32_t n = f.dlc > 8 ? 8u : f.dlc;
  return (67u + 8u * n) * 11u / 10u;
}

}  // namespace enginesense
