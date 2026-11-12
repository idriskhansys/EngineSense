#pragma once
#include <cstdint>
#include <random>
#include <vector>

#include "enginesense/can_frame.hpp"
#include "enginesense/dtc.hpp"
#include "enginesense/signals.hpp"

namespace enginesense {

/// Small, portable PRNG (splitmix64 seeded xoshiro-free) so a seed gives identical numbers on
/// every platform; <random> distributions are implementation defined and are avoided.
class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}
  std::uint64_t next_u64() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  /// Uniform in [0, 1).
  double uniform() { return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0); }
  /// Standard normal (Box-Muller, one value per call).
  double normal();

 private:
  std::uint64_t state_;
};

enum class InjectionType : std::uint8_t {
  SignalStep,         ///< add `magnitude` to the signal
  SignalSet,          ///< force the signal to `magnitude`
  SignalRamp,         ///< add magnitude * seconds_since_start
  SignalError,        ///< transmit the J1939 error indicator for the signal
  SignalNotAvailable, ///< transmit the J1939 not-available indicator for the signal
  PgnDropout,         ///< stop transmitting `pgn`
  BusOverload,        ///< flood the bus to `magnitude` (0..1) load; other traffic suffers arbitration loss
  ActiveDtc,          ///< report `dtc` in DM1 (amber lamp on)
};

struct Injection {
  InjectionType type = InjectionType::SignalStep;
  std::uint64_t start_us = 0;
  std::uint64_t duration_us = 0;  ///< 0 = until the end of the run
  SignalId signal = SignalId::CoolantTemp;
  std::uint32_t pgn = 0;
  double magnitude = 0.0;
  Dtc dtc{};
};

struct SimConfig {
  std::uint32_t seed = 1;
  std::uint32_t bitrate_bps = 250'000;
  double noise_scale = 1.0;          ///< multiplies all sensor noise sigmas
  double initial_coolant_c = 85.0;
  double load_period_s = 120.0;      ///< period of the synthetic duty cycle
  double jitter_fraction = 0.02;     ///< +-2 % transmit period jitter
  double overload_drop_probability = 0.3;  ///< chance a regular frame loses arbitration while overloaded
};

/// Virtual engine + J1939 transmitter. Generates frames in timestamp order with a seeded,
/// platform independent random source; faults are injected at the frame level.
class Simulator {
 public:
  explicit Simulator(SimConfig cfg = SimConfig{});

  void add_injection(const Injection& inj);
  void clear_injections();

  /// Append all frames with timestamp in (current_time, to_us] to `out`.
  void advance(std::uint64_t to_us, std::vector<CanFrame>& out);
  /// Convenience: run from the current time for `duration_us`.
  std::vector<CanFrame> generate(std::uint64_t duration_us);

  std::uint64_t time_us() const noexcept { return time_us_; }
  const SimConfig& config() const noexcept { return cfg_; }

 private:
  struct Msg {
    std::uint32_t pgn;
    std::uint8_t priority;
    std::uint64_t period_us;
    std::uint64_t next_due_us;
  };
  struct Truth {
    double load = 0.5, rpm = 700, torque = 20, coolant = 85, fuel_temp = 40, oil_temp = 95, oil_p = 250,
           fuel_p = 450, battery = 27.6;
  };

  void step_model();
  bool injection_active(const Injection& i, std::uint64_t t) const noexcept;
  bool overload_active(std::uint64_t t, double* magnitude) const noexcept;
  std::uint64_t next_overload_start(std::uint64_t after) const noexcept;
  double apply(SignalId id, double v, std::uint64_t t, SignalStatus* forced) const noexcept;
  bool build(const Msg& m, std::uint64_t t, CanFrame& f);
  double sig_value(SignalId id, std::uint64_t t, SignalStatus* forced);

  SimConfig cfg_;
  Rng rng_;
  Truth truth_;
  std::vector<Msg> msgs_;
  std::vector<Injection> injections_;
  std::uint64_t time_us_ = 0;
  std::uint64_t model_us_ = 0;
  std::uint64_t flood_next_us_ = ~std::uint64_t{0};
  std::uint32_t flood_counter_ = 0;
};

}  // namespace enginesense
