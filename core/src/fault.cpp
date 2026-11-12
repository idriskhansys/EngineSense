#include "enginesense/fault.hpp"

namespace enginesense {

const char* to_string(FaultType t) noexcept {
  switch (t) {
    case FaultType::OverTemperature: return "OverTemperature";
    case FaultType::LowOilPressure: return "LowOilPressure";
    case FaultType::OverSpeed: return "OverSpeed";
    case FaultType::LowBattery: return "LowBattery";
    case FaultType::HighBattery: return "HighBattery";
    case FaultType::SensorDropout: return "SensorDropout";
    case FaultType::SignalError: return "SignalError";
    case FaultType::SignalOutOfRange: return "SignalOutOfRange";
    case FaultType::ImplausibleRate: return "ImplausibleRate";
    case FaultType::BusOverload: return "BusOverload";
    case FaultType::EcuDtc: return "EcuDtc";
  }
  return "?";
}

const char* to_string(Severity s) noexcept {
  switch (s) {
    case Severity::Info: return "info";
    case Severity::Warning: return "warning";
    case Severity::Critical: return "critical";
  }
  return "?";
}

}  // namespace enginesense
