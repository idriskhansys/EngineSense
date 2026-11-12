#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "enginesense/can_frame.hpp"
#include "enginesense/j1939.hpp"

namespace enginesense {

enum class SignalId : std::uint8_t {
  EngineSpeed = 0,
  EngineTorque,
  CoolantTemp,
  FuelTemp,
  OilTemp,
  OilPressure,
  FuelPressure,
  BatteryVoltage,
};
inline constexpr std::size_t kSignalCount = 8;

enum class SignalStatus : std::uint8_t {
  Unknown = 0,    ///< never received
  Valid,          ///< value is a valid physical measurement
  NotAvailable,   ///< J1939 "not available" indicator (all ones)
  Error,          ///< J1939 error / reserved indicator (0xFE.. and 0xFB..0xFD)
};

/// Static description of one SPN as it is laid out in its PGN (SAE J1939-71).
struct SignalDef {
  SignalId id;
  const char* name;
  const char* unit;
  std::uint32_t pgn;
  std::uint32_t spn;
  std::uint8_t start_byte;  ///< 1-based byte position in the 8 byte payload
  std::uint8_t start_bit;   ///< 0-based bit within that byte (LSB = 0)
  std::uint8_t length;      ///< bits, 8 or 16 here
  double resolution;        ///< physical units per bit
  double offset;            ///< physical = raw * resolution + offset
};

const SignalDef& signal_def(SignalId id) noexcept;
const std::array<SignalDef, kSignalCount>& all_signal_defs() noexcept;
const char* to_string(SignalStatus s) noexcept;
std::optional<SignalId> signal_from_spn(std::uint32_t spn) noexcept;
constexpr std::size_t index_of(SignalId id) noexcept { return static_cast<std::size_t>(id); }

/// Latest known state of one signal.
struct SignalValue {
  double value = 0.0;
  SignalStatus status = SignalStatus::Unknown;
  std::uint64_t last_update_us = 0;
  std::uint64_t update_count = 0;
};
using SignalTable = std::array<SignalValue, kSignalCount>;

struct DecodedSignal {
  SignalId id = SignalId::EngineSpeed;
  double value = 0.0;
  SignalStatus status = SignalStatus::Unknown;
};
struct DecodeResult {
  std::array<DecodedSignal, kSignalCount> items{};
  std::size_t count = 0;
};

/// Decode every known signal carried by `frame`. Frames with unknown PGNs or a DLC that is too
/// short for a given signal simply yield fewer (or zero) items; this function never throws.
DecodeResult decode_frame(const CanFrame& frame) noexcept;

// ---- encoding (used by the simulator and the tests) ----

/// Create a frame for `pgn` with all payload bytes set to 0xFF ("not available").
CanFrame make_frame(std::uint64_t timestamp_us, std::uint32_t pgn_value, std::uint8_t source = 0,
                    std::uint8_t priority = 6) noexcept;
/// Write a physical value (rounded to resolution, clamped to the valid raw range).
void encode_signal(CanFrame& frame, SignalId id, double physical) noexcept;
/// Write a J1939 NotAvailable / Error indicator. Valid is ignored.
void encode_status(CanFrame& frame, SignalId id, SignalStatus status) noexcept;

}  // namespace enginesense
