#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "enginesense/fault.hpp"
#include "enginesense/fault_engine.hpp"
#include "enginesense/simulator.hpp"

namespace enginesense {

/// A named fault-injection experiment with its pass criteria (requirement + latency budget).
struct Scenario {
  std::string name;
  std::string description;
  std::string requirement;               ///< requirement id the latency budget comes from
  Injection injection;                   ///< start_us is filled in by the runner
  FaultType primary;                     ///< fault that must be raised
  std::vector<FaultType> allowed_also;   ///< legitimate secondary detections
  std::uint64_t max_latency_us;          ///< detection budget from injection start
};

const std::vector<Scenario>& standard_scenarios();
const Scenario* find_scenario(std::string_view name);

struct TrialConfig {
  std::uint32_t seed = 1;
  std::uint64_t duration_us = 60'000'000;
  std::uint64_t inject_at_us = 20'000'000;  ///< a seed-dependent offset in [0,1) s is added
  double noise_scale = 1.0;
  RuleConfig rules{};
};

struct TrialResult {
  bool detected = false;
  std::uint64_t injected_at_us = 0;
  std::uint64_t latency_us = 0;
  std::size_t false_alarms = 0;  ///< faults raised before the injection started
  std::size_t unexpected = 0;    ///< faults after injection that are neither primary nor allowed
  std::vector<FaultNotification> notifications;
  std::uint64_t frames = 0;
};

/// Simulate `scenario` against a fresh FaultEngine and score the outcome.
TrialResult run_trial(const Scenario& scenario, const TrialConfig& cfg);

struct NominalResult {
  std::size_t false_alarms = 0;
  std::array<std::size_t, kFaultTypeCount> by_type{};  ///< false alarms per FaultType
  std::uint64_t frames = 0;
};
/// Fault-free run for false-alarm statistics.
NominalResult run_nominal(std::uint32_t seed, std::uint64_t duration_us, const RuleConfig& rules = RuleConfig{},
                          double noise_scale = 1.0);

/// Feed pre-generated frames into a fresh engine and return all notifications; `flush_us` of
/// extra time is ticked at the end so pending debounce / clear timers can finish.
std::vector<FaultNotification> replay_frames(const std::vector<CanFrame>& frames, const RuleConfig& rules = RuleConfig{},
                                             std::uint64_t flush_us = 0);

}  // namespace enginesense
