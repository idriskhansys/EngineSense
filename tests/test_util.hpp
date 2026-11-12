#pragma once
#include <string>
#include <vector>

#include "enginesense/dtc.hpp"
#include "enginesense/fault_engine.hpp"
#include "enginesense/signals.hpp"

namespace estest {
using namespace enginesense;

inline CanFrame et1(std::uint64_t ts, double coolant, double oil_temp = 95.0, double fuel_temp = 40.0) {
  CanFrame f = make_frame(ts, pgn::kEt1, 0, 6);
  encode_signal(f, SignalId::CoolantTemp, coolant);
  encode_signal(f, SignalId::OilTemp, oil_temp);
  encode_signal(f, SignalId::FuelTemp, fuel_temp);
  return f;
}
inline CanFrame eec1(std::uint64_t ts, double rpm, double torque = 30.0) {
  CanFrame f = make_frame(ts, pgn::kEec1, 0, 3);
  encode_signal(f, SignalId::EngineSpeed, rpm);
  encode_signal(f, SignalId::EngineTorque, torque);
  return f;
}
inline CanFrame eflp1(std::uint64_t ts, double oil_kpa, double fuel_kpa = 450.0) {
  CanFrame f = make_frame(ts, pgn::kEflP1, 0, 6);
  encode_signal(f, SignalId::OilPressure, oil_kpa);
  encode_signal(f, SignalId::FuelPressure, fuel_kpa);
  return f;
}
inline CanFrame vep1(std::uint64_t ts, double volts = 27.6) {
  CanFrame f = make_frame(ts, pgn::kVep1, 0, 6);
  encode_signal(f, SignalId::BatteryVoltage, volts);
  return f;
}

inline std::size_t count_raised(const std::vector<FaultNotification>& v, FaultType t) {
  std::size_t n = 0;
  for (const auto& x : v)
    if (x.kind == FaultNotification::Kind::Raised && x.event.type == t) ++n;
  return n;
}
inline std::size_t count_cleared(const std::vector<FaultNotification>& v, FaultType t) {
  std::size_t n = 0;
  for (const auto& x : v)
    if (x.kind == FaultNotification::Kind::Cleared && x.event.type == t) ++n;
  return n;
}
}  // namespace estest
