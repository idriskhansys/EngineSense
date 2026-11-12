#pragma once
#include <cstdint>

namespace enginesense {

/// Parameter group numbers handled by the analyzer.
namespace pgn {
inline constexpr std::uint32_t kEec1 = 0xF004;   // 61444  Electronic Engine Controller 1
inline constexpr std::uint32_t kEt1 = 0xFEEE;    // 65262  Engine Temperature 1
inline constexpr std::uint32_t kEflP1 = 0xFEEF;  // 65263  Engine Fluid Level/Pressure 1
inline constexpr std::uint32_t kVep1 = 0xFEF7;   // 65271  Vehicle Electrical Power 1
inline constexpr std::uint32_t kDm1 = 0xFECA;    // 65226  Active Diagnostic Trouble Codes
inline constexpr std::uint32_t kProprietaryB = 0xFF00;  // used by the bus-overload injector
}  // namespace pgn

/// Decomposed 29-bit J1939 identifier (SAE J1939-21).
struct J1939Id {
  std::uint8_t priority = 0;
  std::uint32_t pgn = 0;
  std::uint8_t source = 0;
  std::uint8_t destination = 0xFF;  ///< 0xFF = global (broadcast)
};

/// Split a 29-bit CAN identifier. PDU1 (PF < 240) carries a destination address in PS and the
/// PGN's low byte is zero; PDU2 (PF >= 240) uses PS as the group extension.
constexpr J1939Id parse_id(std::uint32_t can_id) noexcept {
  J1939Id r{};
  r.priority = static_cast<std::uint8_t>((can_id >> 26) & 0x7u);
  const std::uint32_t dp = (can_id >> 24) & 0x1u;
  const std::uint32_t pf = (can_id >> 16) & 0xFFu;
  const std::uint32_t ps = (can_id >> 8) & 0xFFu;
  r.source = static_cast<std::uint8_t>(can_id & 0xFFu);
  if (pf < 240u) {
    r.pgn = (dp << 16) | (pf << 8);
    r.destination = static_cast<std::uint8_t>(ps);
  } else {
    r.pgn = (dp << 16) | (pf << 8) | ps;
    r.destination = 0xFF;
  }
  return r;
}

/// Inverse of parse_id().
constexpr std::uint32_t make_id(std::uint8_t priority, std::uint32_t pgn_value, std::uint8_t source,
                                std::uint8_t destination = 0xFF) noexcept {
  const std::uint32_t pf = (pgn_value >> 8) & 0xFFu;
  const std::uint32_t dp = (pgn_value >> 16) & 0x1u;
  const std::uint32_t ps = pf < 240u ? destination : (pgn_value & 0xFFu);
  return (static_cast<std::uint32_t>(priority & 0x7u) << 26) | (dp << 24) | (pf << 16) | (ps << 8) | source;
}

/// Human readable PGN acronym ("EEC1", ...) or "PGN" if unknown.
const char* pgn_name(std::uint32_t pgn_value) noexcept;

/// Nominal transmission period in microseconds, 0 if the PGN is not periodic / unknown.
std::uint64_t nominal_period_us(std::uint32_t pgn_value) noexcept;

}  // namespace enginesense
