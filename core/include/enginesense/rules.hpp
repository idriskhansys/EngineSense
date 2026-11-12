#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "enginesense/fault.hpp"
#include "enginesense/signals.hpp"

namespace enginesense {

/// All tunable detection parameters in one place so they can be swept by the benchmark.
struct RuleConfig {
  std::uint32_t bitrate_bps = 250'000;
  std::uint64_t tick_us = 10'000;  ///< evaluation grid between frames (time-based rules)

  // Operating limits (engine protection) -------------------------------------------------
  double coolant_over_c = 105.0;
  double coolant_hyst_c = 3.0;
  double oil_temp_over_c = 130.0;
  double oil_temp_hyst_c = 5.0;
  double oil_pressure_low_kpa = 100.0;
  double oil_pressure_hyst_kpa = 20.0;
  double oil_pressure_min_rpm = 800.0;  ///< rule is gated off below this speed
  double overspeed_rpm = 2600.0;
  double overspeed_hyst_rpm = 100.0;
  double battery_low_v = 22.0;   ///< 24 V system
  double battery_high_v = 32.0;
  double battery_hyst_v = 0.5;

  // Persistence ---------------------------------------------------------------------------
  /// A threshold violation must be seen in this many *consecutive samples* of the signal.
  /// Sample counting (not wall time) makes the rule independent of the PGN period and of jitter.
  unsigned confirm_samples_temp = 2;      // 1 Hz signals
  unsigned confirm_samples_pressure = 2;  // 2 Hz signals
  unsigned confirm_samples_speed = 10;    // 100 Hz signals -> 100 ms
  unsigned confirm_samples_battery = 2;
  unsigned confirm_samples_error = 2;
  unsigned confirm_samples_plausibility = 2;
  std::uint64_t clear_after_us = 2'000'000;  ///< continuous non-violation before a fault clears

  // Dropout / bus -------------------------------------------------------------------------
  double stale_factor = 3.0;  ///< PGN considered lost after stale_factor x nominal period
  double bus_load_limit = 0.85;
  double bus_load_hyst = 0.10;
  /// Dropout faults are suppressed while the 50 ms bus load exceeds this: a starved bus loses
  /// frames, which is a consequence of the overload, not an independent sensor failure.
  double stale_suppress_fast_load = 0.3;
  std::uint64_t bus_raise_us = 250'000;
  std::uint64_t dtc_timeout_us = 3'000'000;

  // Plausibility / rate (index = SignalId) -------------------------------------------------
  struct Range {
    double lo;
    double hi;
  };
  // speed, torque, coolant, fuel temp, oil temp, oil press, fuel press, battery.
  // Pressure limits sit below the 1000 kPa full scale (250 x 4 kPa) so a sensor saturated at
  // full scale is detectable.
  std::array<Range, kSignalCount> plausible = {{{0, 3500}, {-125, 125}, {-40, 150}, {-40, 120},
                                                {-40, 200}, {0, 800}, {0, 800}, {0, 36}}};
  /// Maximum physically possible |d value / dt| per second; 0 disables the rule for the signal.
  std::array<double, kSignalCount> max_rate_per_s = {{0, 0, 5.0, 3.0, 5.0, 1000.0, 1000.0, 10.0}};
  std::uint64_t rate_hold_us = 2'000'000;  ///< how long an ImplausibleRate fault is latched
};

struct RuleSpec {
  std::string key;
  FaultType type = FaultType::OverTemperature;
  Severity severity = Severity::Warning;
  std::optional<SignalId> signal;
  std::uint64_t raise_after_us = 0;  ///< violation must persist this long (time debounce)
  std::uint64_t clear_after_us = 0;  ///< non-violation must persist this long before clearing
  /// Consequential-fault suppression: do not raise while a fault of this type is present.
  std::optional<FaultType> suppressed_by;
};

struct EvalContext {
  std::uint64_t now_us = 0;
  std::uint64_t start_us = 0;
  const SignalTable& signals;
  const std::unordered_map<std::uint32_t, std::uint64_t>& pgn_last_seen;
  double bus_load = 0.0;       ///< 1 s sliding window
  double bus_load_fast = 0.0;  ///< 50 ms sliding window (arbitration-starvation indicator)
};

struct RuleResult {
  bool violated = false;
  double value = 0.0;
};

/// A detection rule inspects the engine state and says whether its condition is violated *now*.
/// Debounce, hysteresis bookkeeping at fault level and notifications live in FaultEngine.
class Rule {
 public:
  explicit Rule(RuleSpec spec) : spec_(std::move(spec)) {}
  virtual ~Rule() = default;
  Rule(const Rule&) = delete;
  Rule& operator=(const Rule&) = delete;

  const RuleSpec& spec() const noexcept { return spec_; }
  virtual RuleResult evaluate(const EvalContext& ctx) = 0;
  virtual std::string describe(double value) const = 0;
  virtual void reset() {}

 private:
  RuleSpec spec_;
};

enum class Direction : std::uint8_t { Above, Below };

struct ThresholdParams {
  SignalId signal = SignalId::CoolantTemp;
  Direction direction = Direction::Above;
  double threshold = 0.0;
  double hysteresis = 0.0;
  unsigned confirm_samples = 1;
  std::optional<SignalId> gate_signal;  ///< rule only active while gate_signal >= gate_min
  double gate_min = 0.0;
};

/// Limit check with hysteresis band and N-consecutive-sample confirmation.
class ThresholdRule final : public Rule {
 public:
  ThresholdRule(RuleSpec spec, const ThresholdParams& params) : Rule(std::move(spec)), p_(params) {}
  RuleResult evaluate(const EvalContext& ctx) override;
  std::string describe(double value) const override;
  void reset() override;

 private:
  ThresholdParams p_;
  std::uint64_t last_count_ = 0;
  bool latched_ = false;
  unsigned consecutive_ = 0;
  double last_value_ = 0.0;
};

/// Physical plausibility window, N consecutive samples.
class PlausibilityRule final : public Rule {
 public:
  PlausibilityRule(RuleSpec spec, SignalId signal, double lo, double hi, unsigned confirm)
      : Rule(std::move(spec)), signal_(signal), lo_(lo), hi_(hi), confirm_(confirm) {}
  RuleResult evaluate(const EvalContext& ctx) override;
  std::string describe(double value) const override;
  void reset() override;

 private:
  SignalId signal_;
  double lo_, hi_;
  unsigned confirm_;
  std::uint64_t last_count_ = 0;
  unsigned consecutive_ = 0;
  double last_value_ = 0.0;
};

/// Rate-of-change check; a single offending sample latches the violation for `hold_us`.
class RateRule final : public Rule {
 public:
  RateRule(RuleSpec spec, SignalId signal, double max_rate_per_s, std::uint64_t hold_us)
      : Rule(std::move(spec)), signal_(signal), max_rate_(max_rate_per_s), hold_us_(hold_us) {}
  RuleResult evaluate(const EvalContext& ctx) override;
  std::string describe(double value) const override;
  void reset() override;

 private:
  SignalId signal_;
  double max_rate_;
  std::uint64_t hold_us_;
  std::uint64_t last_count_ = 0;
  bool have_prev_ = false;
  double prev_value_ = 0.0;
  std::uint64_t prev_ts_ = 0;
  std::uint64_t latched_until_ = 0;
  bool latched_ = false;
  double last_rate_ = 0.0;
};

/// ECU flags the signal as error / not available for N consecutive samples.
class SignalErrorRule final : public Rule {
 public:
  SignalErrorRule(RuleSpec spec, SignalId signal, unsigned confirm)
      : Rule(std::move(spec)), signal_(signal), confirm_(confirm) {}
  RuleResult evaluate(const EvalContext& ctx) override;
  std::string describe(double value) const override;
  void reset() override;

 private:
  SignalId signal_;
  unsigned confirm_;
  std::uint64_t last_count_ = 0;
  unsigned consecutive_ = 0;
  SignalStatus last_status_ = SignalStatus::Unknown;
};

/// A periodic PGN has not been received for `factor` x its nominal period.
class StaleRule final : public Rule {
 public:
  StaleRule(RuleSpec spec, std::uint32_t pgn_value, std::uint64_t period_us, double factor,
            double suppress_fast_load = 2.0)
      : Rule(std::move(spec)), pgn_(pgn_value), period_us_(period_us), factor_(factor),
        suppress_fast_load_(suppress_fast_load) {}
  RuleResult evaluate(const EvalContext& ctx) override;
  std::string describe(double value) const override;

 private:
  std::uint32_t pgn_;
  std::uint64_t period_us_;
  double factor_;
  double suppress_fast_load_;
};

/// Bus load above limit, with hysteresis.
class BusLoadRule final : public Rule {
 public:
  BusLoadRule(RuleSpec spec, double limit, double hysteresis)
      : Rule(std::move(spec)), limit_(limit), hyst_(hysteresis) {}
  RuleResult evaluate(const EvalContext& ctx) override;
  std::string describe(double value) const override;
  void reset() override { latched_ = false; }

 private:
  double limit_, hyst_;
  bool latched_ = false;
};

/// The rule set used by the shipped application.
std::vector<std::unique_ptr<Rule>> make_default_rules(const RuleConfig& cfg);

}  // namespace enginesense
