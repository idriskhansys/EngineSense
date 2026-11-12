#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "enginesense/signals.hpp"

namespace enginesense {

enum class FaultType : std::uint8_t {
  OverTemperature = 0,
  LowOilPressure,
  OverSpeed,
  LowBattery,
  HighBattery,
  SensorDropout,     ///< a periodic PGN stopped arriving (sensor / ECU / wiring loss)
  SignalError,       ///< ECU reports error / not-available indicator for a signal
  SignalOutOfRange,  ///< value outside the physically plausible range
  ImplausibleRate,   ///< value changed faster than physically possible
  BusOverload,       ///< CAN bus load above limit
  EcuDtc,            ///< diagnostic trouble code reported by the ECU in DM1
};
inline constexpr std::size_t kFaultTypeCount = 11;

enum class Severity : std::uint8_t { Info = 0, Warning, Critical };

const char* to_string(FaultType t) noexcept;
const char* to_string(Severity s) noexcept;

struct FaultEvent {
  std::uint64_t id = 0;
  FaultType type = FaultType::OverTemperature;
  Severity severity = Severity::Warning;
  std::string key;      ///< stable identifier of the rule / DTC that produced it
  std::string message;
  std::optional<SignalId> signal;
  std::uint64_t raised_us = 0;
  std::uint64_t cleared_us = 0;  ///< valid when !active
  bool active = true;
  double value = 0.0;            ///< offending value at the time of the last update
};

struct FaultNotification {
  enum class Kind : std::uint8_t { Raised, Cleared };
  Kind kind = Kind::Raised;
  FaultEvent event;
};

}  // namespace enginesense
