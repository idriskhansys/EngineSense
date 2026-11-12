#include "enginesense/signals.hpp"

#include <algorithm>
#include <cmath>

#include "enginesense/j1939.hpp"

namespace enginesense {
namespace {

constexpr std::array<SignalDef, kSignalCount> kDefs = {{
    // id                        name               unit    pgn         spn  byte bit len  res      offset
    {SignalId::EngineSpeed,     "Engine speed",     "rpm",  pgn::kEec1,  190, 4, 0, 16, 0.125,   0.0},
    {SignalId::EngineTorque,    "Actual torque",    "%",    pgn::kEec1,  513, 3, 0, 8,  1.0,    -125.0},
    {SignalId::CoolantTemp,     "Coolant temp",     "degC", pgn::kEt1,   110, 1, 0, 8,  1.0,    -40.0},
    {SignalId::FuelTemp,        "Fuel temp",        "degC", pgn::kEt1,   174, 2, 0, 8,  1.0,    -40.0},
    {SignalId::OilTemp,         "Oil temp",         "degC", pgn::kEt1,   175, 3, 0, 16, 0.03125, -273.0},
    {SignalId::OilPressure,     "Oil pressure",     "kPa",  pgn::kEflP1, 100, 4, 0, 8,  4.0,     0.0},
    {SignalId::FuelPressure,    "Fuel pressure",    "kPa",  pgn::kEflP1, 94,  1, 0, 8,  4.0,     0.0},
    {SignalId::BatteryVoltage,  "Battery voltage",  "V",    pgn::kVep1,  168, 5, 0, 16, 0.05,    0.0},
}};

std::uint64_t load_word(const CanFrame& f) noexcept {
  std::uint64_t w = 0;
  const std::size_t n = std::min<std::size_t>(f.dlc, 8);
  for (std::size_t i = 0; i < n; ++i) w |= static_cast<std::uint64_t>(f.data[i]) << (8 * i);
  return w;
}

void store_word(CanFrame& f, std::uint64_t w) noexcept {
  for (std::size_t i = 0; i < 8; ++i) f.data[i] = static_cast<std::uint8_t>((w >> (8 * i)) & 0xFFu);
}

unsigned start_bit_of(const SignalDef& d) noexcept { return (d.start_byte - 1u) * 8u + d.start_bit; }
std::uint64_t mask_of(const SignalDef& d) noexcept { return (std::uint64_t{1} << d.length) - 1u; }

/// Largest raw value that is still a valid measurement: 0xFA for 8 bit, 0xFAFF for 16 bit.
std::uint64_t max_valid_raw(const SignalDef& d) noexcept {
  const unsigned low_bits = d.length - 8u;
  return (std::uint64_t{0xFA} << low_bits) | ((std::uint64_t{1} << low_bits) - 1u);
}

SignalStatus classify(const SignalDef& d, std::uint64_t raw) noexcept {
  const std::uint64_t top = raw >> (d.length - 8u);
  if (top == 0xFF) return SignalStatus::NotAvailable;
  if (top >= 0xFB) return SignalStatus::Error;
  return SignalStatus::Valid;
}

}  // namespace

const SignalDef& signal_def(SignalId id) noexcept { return kDefs[index_of(id)]; }
const std::array<SignalDef, kSignalCount>& all_signal_defs() noexcept { return kDefs; }

const char* to_string(SignalStatus s) noexcept {
  switch (s) {
    case SignalStatus::Unknown: return "unknown";
    case SignalStatus::Valid: return "valid";
    case SignalStatus::NotAvailable: return "n/a";
    case SignalStatus::Error: return "error";
  }
  return "?";
}

std::optional<SignalId> signal_from_spn(std::uint32_t spn) noexcept {
  for (const auto& d : kDefs)
    if (d.spn == spn) return d.id;
  return std::nullopt;
}

DecodeResult decode_frame(const CanFrame& frame) noexcept {
  DecodeResult r;
  const std::uint32_t p = parse_id(frame.id).pgn;
  const std::size_t dlc = std::min<std::size_t>(frame.dlc, 8);
  std::uint64_t word = 0;
  bool loaded = false;
  for (const auto& d : kDefs) {
    if (d.pgn != p) continue;
    const unsigned start = start_bit_of(d);
    const std::size_t last_byte = (start + d.length - 1u) / 8u;
    if (last_byte >= dlc) continue;  // payload too short for this SPN
    if (!loaded) {
      word = load_word(frame);
      loaded = true;
    }
    const std::uint64_t raw = (word >> start) & mask_of(d);
    DecodedSignal s;
    s.id = d.id;
    s.status = classify(d, raw);
    if (s.status == SignalStatus::Valid) s.value = static_cast<double>(raw) * d.resolution + d.offset;
    r.items[r.count++] = s;
  }
  return r;
}

CanFrame make_frame(std::uint64_t timestamp_us, std::uint32_t pgn_value, std::uint8_t source,
                    std::uint8_t priority) noexcept {
  CanFrame f;
  f.timestamp_us = timestamp_us;
  f.id = make_id(priority, pgn_value, source);
  f.dlc = 8;
  f.data.fill(0xFF);
  return f;
}

void encode_signal(CanFrame& frame, SignalId id, double physical) noexcept {
  const SignalDef& d = signal_def(id);
  double raw_d = std::round((physical - d.offset) / d.resolution);
  const double max_raw = static_cast<double>(max_valid_raw(d));
  if (!(raw_d > 0.0)) raw_d = 0.0;  // also catches NaN
  if (raw_d > max_raw) raw_d = max_raw;
  const auto raw = static_cast<std::uint64_t>(raw_d);
  const unsigned start = start_bit_of(d);
  std::uint64_t w = load_word(frame);
  w = (w & ~(mask_of(d) << start)) | (raw << start);
  store_word(frame, w);
}

void encode_status(CanFrame& frame, SignalId id, SignalStatus status) noexcept {
  const SignalDef& d = signal_def(id);
  std::uint64_t raw = 0;
  if (status == SignalStatus::NotAvailable) raw = mask_of(d);
  else if (status == SignalStatus::Error) raw = std::uint64_t{0xFE} << (d.length - 8u);
  else return;
  const unsigned start = start_bit_of(d);
  std::uint64_t w = load_word(frame);
  w = (w & ~(mask_of(d) << start)) | (raw << start);
  store_word(frame, w);
}

const char* pgn_name(std::uint32_t p) noexcept {
  switch (p) {
    case pgn::kEec1: return "EEC1";
    case pgn::kEt1: return "ET1";
    case pgn::kEflP1: return "EFL/P1";
    case pgn::kVep1: return "VEP1";
    case pgn::kDm1: return "DM1";
    default: return "PGN";
  }
}

std::uint64_t nominal_period_us(std::uint32_t p) noexcept {
  switch (p) {
    case pgn::kEec1: return 10'000;
    case pgn::kEflP1: return 500'000;
    case pgn::kEt1:
    case pgn::kVep1:
    case pgn::kDm1: return 1'000'000;
    default: return 0;
  }
}

}  // namespace enginesense
