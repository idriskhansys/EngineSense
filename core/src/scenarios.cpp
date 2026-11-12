#include "enginesense/scenarios.hpp"

#include <algorithm>

#include "enginesense/j1939.hpp"

namespace enginesense {
namespace {

constexpr std::uint64_t kMs = 1000;
constexpr std::uint64_t kSec = 1'000'000;

Injection inj(InjectionType t, SignalId s, double mag, std::uint64_t dur = 0) {
  Injection i;
  i.type = t;
  i.signal = s;
  i.magnitude = mag;
  i.duration_us = dur;
  return i;
}

std::vector<Scenario> build() {
  std::vector<Scenario> v;
  auto add = [&](std::string name, std::string desc, std::string req, Injection i, FaultType primary,
                 std::vector<FaultType> also, std::uint64_t budget) {
    v.push_back({std::move(name), std::move(desc), std::move(req), i, primary, std::move(also), budget});
  };

  add("coolant_overtemp", "Coolant temperature steps +30 degC (sensor/cooling failure)", "REQ-LAT-001",
      inj(InjectionType::SignalStep, SignalId::CoolantTemp, 30.0), FaultType::OverTemperature,
      {FaultType::ImplausibleRate}, 2500 * kMs);

  Injection dropout_eec1;
  dropout_eec1.type = InjectionType::PgnDropout;
  dropout_eec1.pgn = pgn::kEec1;
  add("eec1_dropout", "Engine speed message (EEC1, 10 ms) stops", "REQ-LAT-002", dropout_eec1,
      FaultType::SensorDropout, {}, 100 * kMs);

  Injection dropout_et1;
  dropout_et1.type = InjectionType::PgnDropout;
  dropout_et1.pgn = pgn::kEt1;
  add("et1_dropout", "Temperature message (ET1, 1 s) stops - coolant sensor / wiring loss", "REQ-LAT-003",
      dropout_et1, FaultType::SensorDropout, {}, 3500 * kMs);

  Injection overload;
  overload.type = InjectionType::BusOverload;
  overload.magnitude = 0.95;
  add("bus_overload", "Babbling node floods the bus to 95 % load", "REQ-LAT-004", overload, FaultType::BusOverload,
      {}, 2000 * kMs);

  add("oil_pressure_low", "Oil pressure collapses to 60 kPa", "REQ-LAT-005",
      inj(InjectionType::SignalSet, SignalId::OilPressure, 60.0), FaultType::LowOilPressure, {},
      2500 * kMs);

  add("overspeed", "Engine speed forced to 3000 rpm", "REQ-LAT-005",
      inj(InjectionType::SignalSet, SignalId::EngineSpeed, 3000.0), FaultType::OverSpeed, {}, 500 * kMs);

  add("battery_low", "Battery voltage drops to 20 V", "REQ-LAT-005",
      inj(InjectionType::SignalSet, SignalId::BatteryVoltage, 20.0), FaultType::LowBattery, {}, 2500 * kMs);

  add("coolant_sensor_error", "ECU flags coolant temperature with the J1939 error indicator", "REQ-LAT-005",
      inj(InjectionType::SignalError, SignalId::CoolantTemp, 0.0), FaultType::SignalError, {}, 2500 * kMs);

  add("fuel_pressure_out_of_range", "Fuel pressure sensor saturates at full scale (1000 kPa)", "REQ-LAT-005",
      inj(InjectionType::SignalSet, SignalId::FuelPressure, 1000.0), FaultType::SignalOutOfRange,
      {FaultType::ImplausibleRate}, 2000 * kMs);

  add("coolant_spike", "Coolant reading spikes +15 degC for 1.5 s (EMI / loose connector)", "REQ-LAT-005",
      inj(InjectionType::SignalStep, SignalId::CoolantTemp, 15.0, 1500 * kMs), FaultType::ImplausibleRate, {},
      2500 * kMs);

  Injection dtc;
  dtc.type = InjectionType::ActiveDtc;
  dtc.dtc = Dtc{110, 0, 1, false};  // SPN 110 coolant temperature, FMI 0
  add("ecu_dtc", "ECU reports DTC SPN 110 / FMI 0 in DM1", "REQ-LAT-005", dtc, FaultType::EcuDtc, {}, 1500 * kMs);

  return v;
}

std::uint64_t splitmix(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

bool allowed(const Scenario& s, FaultType t) {
  return t == s.primary || std::find(s.allowed_also.begin(), s.allowed_also.end(), t) != s.allowed_also.end();
}

}  // namespace

const std::vector<Scenario>& standard_scenarios() {
  static const std::vector<Scenario> v = build();
  return v;
}

const Scenario* find_scenario(std::string_view name) {
  for (const auto& s : standard_scenarios())
    if (s.name == name) return &s;
  return nullptr;
}

TrialResult run_trial(const Scenario& scenario, const TrialConfig& cfg) {
  SimConfig sc;
  sc.seed = cfg.seed;
  sc.noise_scale = cfg.noise_scale;
  sc.bitrate_bps = cfg.rules.bitrate_bps;
  Simulator sim(sc);

  TrialResult res;
  res.injected_at_us = cfg.inject_at_us + splitmix(cfg.seed) % kSec;  // vary phase vs. message periods
  Injection i = scenario.injection;
  i.start_us = res.injected_at_us;
  sim.add_injection(i);

  FaultEngine engine(cfg.rules);
  std::vector<CanFrame> frames;
  sim.advance(cfg.duration_us, frames);
  res.frames = frames.size();
  for (const auto& f : frames) engine.process(f, res.notifications);
  engine.tick(cfg.duration_us, res.notifications);

  for (const auto& n : res.notifications) {
    if (n.kind != FaultNotification::Kind::Raised) continue;
    const FaultEvent& e = n.event;
    if (e.raised_us < res.injected_at_us) {
      ++res.false_alarms;
    } else if (e.type == scenario.primary) {
      if (!res.detected) {
        res.detected = true;
        res.latency_us = e.raised_us - res.injected_at_us;
      }
    } else if (!allowed(scenario, e.type)) {
      ++res.unexpected;
    }
  }
  return res;
}

NominalResult run_nominal(std::uint32_t seed, std::uint64_t duration_us, const RuleConfig& rules, double noise_scale) {
  SimConfig sc;
  sc.seed = seed;
  sc.noise_scale = noise_scale;
  sc.bitrate_bps = rules.bitrate_bps;
  Simulator sim(sc);
  FaultEngine engine(rules);
  NominalResult r;
  std::vector<CanFrame> chunk;
  std::vector<FaultNotification> notes;
  constexpr std::uint64_t kChunk = 10 * kSec;  // bounded memory for long runs
  for (std::uint64_t t = 0; t < duration_us; t += kChunk) {
    chunk.clear();
    sim.advance(std::min(duration_us, t + kChunk), chunk);
    r.frames += chunk.size();
    for (const auto& f : chunk) engine.process(f, notes);
  }
  for (const auto& n : notes)
    if (n.kind == FaultNotification::Kind::Raised) {
      ++r.false_alarms;
      ++r.by_type[static_cast<std::size_t>(n.event.type)];
    }
  return r;
}

std::vector<FaultNotification> replay_frames(const std::vector<CanFrame>& frames, const RuleConfig& rules,
                                             std::uint64_t flush_us) {
  FaultEngine engine(rules);
  std::vector<FaultNotification> notes;
  std::uint64_t last = 0;
  for (const auto& f : frames) {
    engine.process(f, notes);
    last = std::max(last, f.timestamp_us);
  }
  if (!frames.empty() && flush_us > 0) engine.tick(last + flush_us, notes);
  return notes;
}

}  // namespace enginesense
