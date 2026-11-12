#pragma once
#include <cstdint>
#include <optional>

#include "enginesense/can_frame.hpp"

namespace enginesense {

/// One diagnostic trouble code as carried in DM1 (SAE J1939-73, SPN conversion method 4).
struct Dtc {
  std::uint32_t spn = 0;       ///< 19 bit suspect parameter number
  std::uint8_t fmi = 0;        ///< 5 bit failure mode identifier
  std::uint8_t occurrence = 0; ///< 7 bit occurrence counter
  bool conversion_method = false;
};
inline bool operator==(const Dtc& a, const Dtc& b) {
  return a.spn == b.spn && a.fmi == b.fmi && a.occurrence == b.occurrence &&
         a.conversion_method == b.conversion_method;
}

/// Lamp status, 2 bits each: 0 = off, 1 = on, 2 = error, 3 = not available.
struct LampStatus {
  std::uint8_t malfunction = 0;
  std::uint8_t red_stop = 0;
  std::uint8_t amber_warning = 0;
  std::uint8_t protect = 0;
};

struct Dm1 {
  LampStatus lamps;
  std::optional<Dtc> dtc;  ///< first (and, in a single frame, only) active DTC; empty if none
};

/// Parse a single-frame DM1. Returns nullopt when `frame` is not a DM1 or is too short.
/// Multi-DTC messages (TP.BAM transport) are not reassembled; see docs/architecture.md.
std::optional<Dm1> parse_dm1(const CanFrame& frame) noexcept;

/// Build a single-frame DM1 (priority 6). `dtc == nullopt` encodes "no active DTCs".
CanFrame build_dm1(std::uint64_t timestamp_us, const LampStatus& lamps, const std::optional<Dtc>& dtc,
                   std::uint8_t source = 0) noexcept;

/// Text for an FMI per SAE J1939-73 table A5 (abbreviated).
const char* describe_fmi(std::uint8_t fmi) noexcept;

}  // namespace enginesense
