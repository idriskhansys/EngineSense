#include "enginesense/rules.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "enginesense/j1939.hpp"

namespace enginesense {
namespace {

std::string num(double v, int prec = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(prec) << v;
  return os.str();
}

const SignalValue& sig(const EvalContext& ctx, SignalId id) { return ctx.signals[index_of(id)]; }

}  // namespace

// ---- ThresholdRule ------------------------------------------------------------------------

void ThresholdRule::reset() {
  last_count_ = 0;
  latched_ = false;
  consecutive_ = 0;
  last_value_ = 0.0;
}

RuleResult ThresholdRule::evaluate(const EvalContext& ctx) {
  const SignalValue& s = sig(ctx, p_.signal);
  if (s.status != SignalStatus::Valid) {
    latched_ = false;
    consecutive_ = 0;
    return {false, last_value_};
  }
  if (s.update_count != last_count_) {  // act only on new samples
    last_count_ = s.update_count;
    last_value_ = s.value;
    bool gate_ok = true;
    if (p_.gate_signal) {
      const SignalValue& g = sig(ctx, *p_.gate_signal);
      gate_ok = g.status == SignalStatus::Valid && g.value >= p_.gate_min;
    }
    const bool above = p_.direction == Direction::Above;
    const bool beyond = above ? s.value > p_.threshold : s.value < p_.threshold;
    const bool in_band = above ? s.value >= p_.threshold - p_.hysteresis : s.value <= p_.threshold + p_.hysteresis;
    if (beyond) latched_ = true;
    else if (!in_band) latched_ = false;  // inside the hysteresis band the previous state is kept
    if (!gate_ok) latched_ = false;
    consecutive_ = latched_ ? consecutive_ + 1 : 0;
  }
  return {consecutive_ >= p_.confirm_samples, last_value_};
}

std::string ThresholdRule::describe(double value) const {
  const SignalDef& d = signal_def(p_.signal);
  std::ostringstream os;
  os << d.name << " " << num(value) << " " << d.unit
     << (p_.direction == Direction::Above ? " above limit " : " below limit ") << num(p_.threshold) << " "
     << d.unit;
  return os.str();
}

// ---- PlausibilityRule ---------------------------------------------------------------------

void PlausibilityRule::reset() {
  last_count_ = 0;
  consecutive_ = 0;
  last_value_ = 0.0;
}

RuleResult PlausibilityRule::evaluate(const EvalContext& ctx) {
  const SignalValue& s = sig(ctx, signal_);
  if (s.status != SignalStatus::Valid) {
    consecutive_ = 0;
    return {false, last_value_};
  }
  if (s.update_count != last_count_) {
    last_count_ = s.update_count;
    last_value_ = s.value;
    const bool bad = s.value < lo_ || s.value > hi_;
    consecutive_ = bad ? consecutive_ + 1 : 0;
  }
  return {consecutive_ >= confirm_, last_value_};
}

std::string PlausibilityRule::describe(double value) const {
  const SignalDef& d = signal_def(signal_);
  return std::string(d.name) + " " + num(value) + " " + d.unit + " outside plausible range [" + num(lo_, 0) +
         ", " + num(hi_, 0) + "]";
}

// ---- RateRule -----------------------------------------------------------------------------

void RateRule::reset() {
  last_count_ = 0;
  have_prev_ = false;
  prev_value_ = 0.0;
  prev_ts_ = 0;
  latched_until_ = 0;
  latched_ = false;
  last_rate_ = 0.0;
}

RuleResult RateRule::evaluate(const EvalContext& ctx) {
  const SignalValue& s = sig(ctx, signal_);
  if (s.status != SignalStatus::Valid) {
    have_prev_ = false;  // do not compute a rate across an invalid gap
  } else if (s.update_count != last_count_) {
    last_count_ = s.update_count;
    if (have_prev_ && s.last_update_us > prev_ts_) {
      const double dt = static_cast<double>(s.last_update_us - prev_ts_) / 1e6;
      const double rate = std::fabs(s.value - prev_value_) / dt;
      if (rate > max_rate_) {
        latched_ = true;
        latched_until_ = ctx.now_us + hold_us_;
        last_rate_ = rate;
      }
    }
    have_prev_ = true;
    prev_value_ = s.value;
    prev_ts_ = s.last_update_us;
  }
  if (latched_ && ctx.now_us >= latched_until_) latched_ = false;
  return {latched_, last_rate_};
}

std::string RateRule::describe(double value) const {
  const SignalDef& d = signal_def(signal_);
  return std::string(d.name) + " changed at " + num(value) + " " + d.unit + "/s (limit " + num(max_rate_) + " " +
         d.unit + "/s)";
}

// ---- SignalErrorRule ----------------------------------------------------------------------

void SignalErrorRule::reset() {
  last_count_ = 0;
  consecutive_ = 0;
  last_status_ = SignalStatus::Unknown;
}

RuleResult SignalErrorRule::evaluate(const EvalContext& ctx) {
  const SignalValue& s = sig(ctx, signal_);
  if (s.update_count != last_count_) {
    last_count_ = s.update_count;
    last_status_ = s.status;
    const bool bad = s.status == SignalStatus::Error || s.status == SignalStatus::NotAvailable;
    consecutive_ = bad ? consecutive_ + 1 : 0;
  }
  return {consecutive_ >= confirm_, static_cast<double>(last_status_ == SignalStatus::Error ? 1 : 0)};
}

std::string SignalErrorRule::describe(double /*value*/) const {
  return std::string(signal_def(signal_).name) + " reported as " + to_string(last_status_) + " by ECU";
}

// ---- StaleRule ----------------------------------------------------------------------------

RuleResult StaleRule::evaluate(const EvalContext& ctx) {
  const auto it = ctx.pgn_last_seen.find(pgn_);
  const std::uint64_t ref = it != ctx.pgn_last_seen.end() ? it->second : ctx.start_us;
  const std::uint64_t age = ctx.now_us > ref ? ctx.now_us - ref : 0;
  const double limit = factor_ * static_cast<double>(period_us_);
  if (ctx.bus_load_fast > suppress_fast_load_) return {false, static_cast<double>(age) / 1000.0};
  return {static_cast<double>(age) > limit, static_cast<double>(age) / 1000.0};  // value = age in ms
}

std::string StaleRule::describe(double value) const {
  std::ostringstream os;
  os << "PGN 0x" << std::hex << std::uppercase << pgn_ << std::dec << " (" << pgn_name(pgn_)
     << ") not received for " << num(value, 0) << " ms";
  return os.str();
}

// ---- BusLoadRule --------------------------------------------------------------------------

RuleResult BusLoadRule::evaluate(const EvalContext& ctx) {
  if (ctx.bus_load > limit_) latched_ = true;
  else if (ctx.bus_load < limit_ - hyst_) latched_ = false;
  return {latched_, ctx.bus_load * 100.0};
}

std::string BusLoadRule::describe(double value) const {
  return "CAN bus load " + num(value) + " % above limit " + num(limit_ * 100.0, 0) + " %";
}

// ---- default rule set ---------------------------------------------------------------------

std::vector<std::unique_ptr<Rule>> make_default_rules(const RuleConfig& c) {
  std::vector<std::unique_ptr<Rule>> r;
  auto spec = [&](std::string key, FaultType t, Severity sev, std::optional<SignalId> s) {
    RuleSpec sp;
    sp.key = std::move(key);
    sp.type = t;
    sp.severity = sev;
    sp.signal = s;
    sp.clear_after_us = c.clear_after_us;
    return sp;
  };

  // Engine protection limits
  r.push_back(std::make_unique<ThresholdRule>(
      spec("over_temp.coolant", FaultType::OverTemperature, Severity::Critical, SignalId::CoolantTemp),
      ThresholdParams{SignalId::CoolantTemp, Direction::Above, c.coolant_over_c, c.coolant_hyst_c,
                      c.confirm_samples_temp, std::nullopt, 0.0}));
  r.push_back(std::make_unique<ThresholdRule>(
      spec("over_temp.oil", FaultType::OverTemperature, Severity::Critical, SignalId::OilTemp),
      ThresholdParams{SignalId::OilTemp, Direction::Above, c.oil_temp_over_c, c.oil_temp_hyst_c,
                      c.confirm_samples_temp, std::nullopt, 0.0}));
  r.push_back(std::make_unique<ThresholdRule>(
      spec("low_oil_pressure", FaultType::LowOilPressure, Severity::Critical, SignalId::OilPressure),
      ThresholdParams{SignalId::OilPressure, Direction::Below, c.oil_pressure_low_kpa, c.oil_pressure_hyst_kpa,
                      c.confirm_samples_pressure, SignalId::EngineSpeed, c.oil_pressure_min_rpm}));
  r.push_back(std::make_unique<ThresholdRule>(
      spec("over_speed", FaultType::OverSpeed, Severity::Critical, SignalId::EngineSpeed),
      ThresholdParams{SignalId::EngineSpeed, Direction::Above, c.overspeed_rpm, c.overspeed_hyst_rpm,
                      c.confirm_samples_speed, std::nullopt, 0.0}));
  r.push_back(std::make_unique<ThresholdRule>(
      spec("low_battery", FaultType::LowBattery, Severity::Warning, SignalId::BatteryVoltage),
      ThresholdParams{SignalId::BatteryVoltage, Direction::Below, c.battery_low_v, c.battery_hyst_v,
                      c.confirm_samples_battery, std::nullopt, 0.0}));
  r.push_back(std::make_unique<ThresholdRule>(
      spec("high_battery", FaultType::HighBattery, Severity::Warning, SignalId::BatteryVoltage),
      ThresholdParams{SignalId::BatteryVoltage, Direction::Above, c.battery_high_v, c.battery_hyst_v,
                      c.confirm_samples_battery, std::nullopt, 0.0}));

  // Per-signal sanity rules
  for (const SignalDef& d : all_signal_defs()) {
    const std::size_t i = index_of(d.id);
    r.push_back(std::make_unique<SignalErrorRule>(
        spec("signal_error." + std::to_string(d.spn), FaultType::SignalError, Severity::Warning, d.id), d.id,
        c.confirm_samples_error));
    r.push_back(std::make_unique<PlausibilityRule>(
        spec("out_of_range." + std::to_string(d.spn), FaultType::SignalOutOfRange, Severity::Warning, d.id), d.id,
        c.plausible[i].lo, c.plausible[i].hi, c.confirm_samples_plausibility));
    if (c.max_rate_per_s[i] > 0.0) {
      RuleSpec sp = spec("rate." + std::to_string(d.spn), FaultType::ImplausibleRate, Severity::Warning, d.id);
      sp.clear_after_us = 0;  // latch time is the rule's own hold
      r.push_back(std::make_unique<RateRule>(std::move(sp), d.id, c.max_rate_per_s[i], c.rate_hold_us));
    }
  }

  // Dropout of each periodic PGN. Consequential to a saturated bus, hence suppressed by it.
  for (const std::uint32_t p : {pgn::kEec1, pgn::kEt1, pgn::kEflP1, pgn::kVep1, pgn::kDm1}) {
    RuleSpec sp = spec(std::string("stale.") + pgn_name(p), FaultType::SensorDropout, Severity::Critical, std::nullopt);
    sp.clear_after_us = 500'000;
    sp.suppressed_by = FaultType::BusOverload;
    r.push_back(std::make_unique<StaleRule>(std::move(sp), p, nominal_period_us(p), c.stale_factor,
                                                  c.stale_suppress_fast_load));
  }

  // Bus overload
  RuleSpec bus = spec("bus_overload", FaultType::BusOverload, Severity::Critical, std::nullopt);
  bus.raise_after_us = c.bus_raise_us;
  bus.clear_after_us = 2'000'000;
  r.push_back(std::make_unique<BusLoadRule>(std::move(bus), c.bus_load_limit, c.bus_load_hyst));
  return r;
}

}  // namespace enginesense
