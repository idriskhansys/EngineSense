#include "enginesense/simulator.hpp"

#include <algorithm>
#include <cmath>

#include "enginesense/j1939.hpp"

namespace enginesense {
namespace {
constexpr std::uint64_t kModelStepUs = 10'000;
constexpr std::uint64_t kNever = ~std::uint64_t{0};
constexpr double kTwoPi = 6.283185307179586476925286766559;
}  // namespace

double Rng::normal() {
  double u1 = uniform();
  if (u1 < 1e-300) u1 = 1e-300;
  const double u2 = uniform();
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

Simulator::Simulator(SimConfig cfg)
    : cfg_(cfg),
      rng_(0x5EED0000ull + cfg.seed),
      // staggered start offsets so messages do not all collide at t = 0
      msgs_{
          {pgn::kEec1, 3, nominal_period_us(pgn::kEec1), 0},
          {pgn::kEt1, 6, nominal_period_us(pgn::kEt1), 100'000},
          {pgn::kEflP1, 6, nominal_period_us(pgn::kEflP1), 250'000},
          {pgn::kVep1, 6, nominal_period_us(pgn::kVep1), 400'000},
          {pgn::kDm1, 6, nominal_period_us(pgn::kDm1), 700'000},
      } {
  truth_.coolant = cfg.initial_coolant_c;
  truth_.oil_temp = cfg.initial_coolant_c + 10.0;
  step_model();
}

void Simulator::add_injection(const Injection& inj) {
  injections_.push_back(inj);
  if (inj.type == InjectionType::BusOverload) flood_next_us_ = std::min(flood_next_us_, std::max(inj.start_us, time_us_ + 1));
}

void Simulator::clear_injections() {
  injections_.clear();
  flood_next_us_ = kNever;
}

bool Simulator::injection_active(const Injection& i, std::uint64_t t) const noexcept {
  return t >= i.start_us && (i.duration_us == 0 || t < i.start_us + i.duration_us);
}

bool Simulator::overload_active(std::uint64_t t, double* magnitude) const noexcept {
  for (const auto& i : injections_) {
    if (i.type == InjectionType::BusOverload && injection_active(i, t)) {
      if (magnitude) *magnitude = i.magnitude;
      return true;
    }
  }
  return false;
}

std::uint64_t Simulator::next_overload_start(std::uint64_t after) const noexcept {
  std::uint64_t best = kNever;
  for (const auto& i : injections_)
    if (i.type == InjectionType::BusOverload && i.start_us > after) best = std::min(best, i.start_us);
  return best;
}

void Simulator::step_model() {
  const double t = static_cast<double>(model_us_) / 1e6;
  const double dt = static_cast<double>(kModelStepUs) / 1e6;
  Truth& m = truth_;
  m.load = 0.5 + 0.35 * std::sin(kTwoPi * t / cfg_.load_period_s);
  m.rpm = 700.0 + (1800.0 - 700.0) * (0.3 + 0.7 * m.load);
  m.torque = 15.0 + 70.0 * m.load;
  const double coolant_target = 75.0 + 15.0 * m.load;
  m.coolant += (coolant_target - m.coolant) * dt / 60.0;
  m.oil_temp = m.coolant + 10.0 + 5.0 * m.load;
  m.fuel_temp = 35.0 + 8.0 * m.load;
  m.oil_p = 120.0 + 0.2 * m.rpm;
  m.fuel_p = 450.0;
  m.battery = 27.6;
  model_us_ += kModelStepUs;
}

double Simulator::apply(SignalId id, double v, std::uint64_t t, SignalStatus* forced) const noexcept {
  for (const auto& i : injections_) {
    if (i.signal != id || !injection_active(i, t)) continue;
    switch (i.type) {
      case InjectionType::SignalStep: v += i.magnitude; break;
      case InjectionType::SignalSet: v = i.magnitude; break;
      case InjectionType::SignalRamp: v += i.magnitude * static_cast<double>(t - i.start_us) / 1e6; break;
      case InjectionType::SignalError: *forced = SignalStatus::Error; break;
      case InjectionType::SignalNotAvailable: *forced = SignalStatus::NotAvailable; break;
      default: break;
    }
  }
  return v;
}

double Simulator::sig_value(SignalId id, std::uint64_t t, SignalStatus* forced) {
  const double k = cfg_.noise_scale;
  double v = 0.0;
  switch (id) {
    case SignalId::EngineSpeed: v = truth_.rpm + 5.0 * k * rng_.normal(); break;
    case SignalId::EngineTorque: v = truth_.torque + 0.5 * k * rng_.normal(); break;
    case SignalId::CoolantTemp: v = truth_.coolant + 0.2 * k * rng_.normal(); break;
    case SignalId::FuelTemp: v = truth_.fuel_temp + 0.2 * k * rng_.normal(); break;
    case SignalId::OilTemp: v = truth_.oil_temp + 0.3 * k * rng_.normal(); break;
    case SignalId::OilPressure: v = truth_.oil_p + 3.0 * k * rng_.normal(); break;
    case SignalId::FuelPressure: v = truth_.fuel_p + 4.0 * k * rng_.normal(); break;
    case SignalId::BatteryVoltage: v = truth_.battery + 0.05 * k * rng_.normal(); break;
  }
  return apply(id, v, t, forced);
}

bool Simulator::build(const Msg& m, std::uint64_t t, CanFrame& f) {
  for (const auto& i : injections_)
    if (i.type == InjectionType::PgnDropout && i.pgn == m.pgn && injection_active(i, t)) return false;

  double load = 0.0;
  if (overload_active(t, &load) && rng_.uniform() < cfg_.overload_drop_probability) return false;

  f = make_frame(t, m.pgn, 0, m.priority);
  if (m.pgn == pgn::kDm1) {
    std::optional<Dtc> dtc;
    LampStatus lamps;
    for (const auto& i : injections_) {
      if (i.type == InjectionType::ActiveDtc && injection_active(i, t)) {
        dtc = i.dtc;
        lamps.amber_warning = 1;
      }
    }
    f = build_dm1(t, lamps, dtc, 0);
    return true;
  }
  for (const auto& d : all_signal_defs()) {
    if (d.pgn != m.pgn) continue;
    SignalStatus forced = SignalStatus::Valid;
    const double v = sig_value(d.id, t, &forced);
    if (forced == SignalStatus::Valid) encode_signal(f, d.id, v);
    else encode_status(f, d.id, forced);
  }
  return true;
}

void Simulator::advance(std::uint64_t to_us, std::vector<CanFrame>& out) {
  for (;;) {
    // pick the earliest pending transmission
    std::size_t best = msgs_.size();
    std::uint64_t best_t = flood_next_us_;
    for (std::size_t i = 0; i < msgs_.size(); ++i)
      if (msgs_[i].next_due_us < best_t) {
        best = i;
        best_t = msgs_[i].next_due_us;
      }
    if (best_t == kNever || best_t > to_us) break;
    while (model_us_ <= best_t) step_model();

    if (best == msgs_.size()) {  // bus flood frame
      double load = 0.0;
      if (overload_active(best_t, &load) && load > 0.0) {
        CanFrame f;
        f.timestamp_us = best_t;
        f.id = make_id(0, pgn::kProprietaryB, 0x80);
        f.dlc = 8;
        f.data.fill(0);
        for (int b = 0; b < 4; ++b) f.data[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((flood_counter_ >> (8 * b)) & 0xFFu);
        ++flood_counter_;
        out.push_back(f);
        const double period_s = static_cast<double>(estimated_frame_bits(f)) / (static_cast<double>(cfg_.bitrate_bps) * load);
        flood_next_us_ = best_t + std::max<std::uint64_t>(1, static_cast<std::uint64_t>(period_s * 1e6));
      } else {
        flood_next_us_ = next_overload_start(best_t);
      }
      continue;
    }

    Msg& m = msgs_[best];
    CanFrame f;
    if (build(m, best_t, f)) out.push_back(f);
    const double jitter = 1.0 + cfg_.jitter_fraction * (2.0 * rng_.uniform() - 1.0);
    m.next_due_us = best_t + std::max<std::uint64_t>(1, static_cast<std::uint64_t>(static_cast<double>(m.period_us) * jitter));
  }
  time_us_ = std::max(time_us_, to_us);
}

std::vector<CanFrame> Simulator::generate(std::uint64_t duration_us) {
  std::vector<CanFrame> v;
  v.reserve(static_cast<std::size_t>(duration_us / 9000 + 16));
  advance(time_us_ + duration_us, v);
  return v;
}

}  // namespace enginesense
