#include <gtest/gtest.h>

#include <map>

#include "enginesense/fault_engine.hpp"
#include "enginesense/simulator.hpp"
#include "test_util.hpp"

using namespace enginesense;
using namespace estest;

// Verifies: REQ-DET-001
TEST(Simulator, SameSeedGivesIdenticalFramesDifferentSeedDiffers) {
  SimConfig a;
  a.seed = 11;
  SimConfig b = a;
  SimConfig c = a;
  c.seed = 12;
  const auto fa = Simulator(a).generate(20'000'000);
  const auto fb = Simulator(b).generate(20'000'000);
  const auto fc = Simulator(c).generate(20'000'000);
  EXPECT_EQ(fa, fb);
  EXPECT_NE(fa, fc);
}

// Verifies: REQ-SIM-001
TEST(Simulator, MessageRatesMatchJ1939NominalWithinFivePercent) {
  const auto frames = Simulator(SimConfig{}).generate(120'000'000);
  std::map<std::uint32_t, std::size_t> count;
  for (const auto& f : frames) ++count[parse_id(f.id).pgn];
  for (std::uint32_t p : {pgn::kEec1, pgn::kEt1, pgn::kEflP1, pgn::kVep1, pgn::kDm1}) {
    const double expected = 120e6 / static_cast<double>(nominal_period_us(p));
    EXPECT_NEAR(static_cast<double>(count[p]), expected, expected * 0.05) << pgn_name(p);
  }
}

// Verifies: REQ-SIM-001
TEST(Simulator, TimestampsAreMonotonicAndNominalBusLoadIsRealistic) {
  const auto frames = Simulator(SimConfig{}).generate(60'000'000);
  BusLoadMeter meter(250'000);
  std::uint64_t prev = 0;
  for (const auto& f : frames) {
    ASSERT_GE(f.timestamp_us, prev);
    prev = f.timestamp_us;
    meter.add(f.timestamp_us, estimated_frame_bits(f));
  }
  const double load = meter.load(60'000'000);
  EXPECT_GT(load, 0.03);
  EXPECT_LT(load, 0.12);
}

// Verifies: REQ-SIM-001
TEST(Simulator, NominalSignalsStayInsideOperatingEnvelopeForTenMinutes) {
  const auto frames = Simulator(SimConfig{}).generate(600'000'000);
  double max_cool = -1e9, min_oilp = 1e9, max_rpm = -1e9, min_batt = 1e9;
  for (const auto& f : frames) {
    const auto d = decode_frame(f);
    for (std::size_t i = 0; i < d.count; ++i) {
      const double v = d.items[i].value;
      if (d.items[i].status != SignalStatus::Valid) FAIL() << "nominal traffic must be valid";
      switch (d.items[i].id) {
        case SignalId::CoolantTemp: max_cool = std::max(max_cool, v); break;
        case SignalId::OilPressure: min_oilp = std::min(min_oilp, v); break;
        case SignalId::EngineSpeed: max_rpm = std::max(max_rpm, v); break;
        case SignalId::BatteryVoltage: min_batt = std::min(min_batt, v); break;
        default: break;
      }
    }
  }
  EXPECT_LT(max_cool, 95.0);
  EXPECT_GT(min_oilp, 200.0);
  EXPECT_LT(max_rpm, 1900.0);
  EXPECT_GT(min_batt, 26.5);
}

// Verifies: REQ-SIM-002
TEST(Simulator, InjectionsProduceTheDocumentedFrameLevelEffects) {
  SimConfig sc;
  sc.noise_scale = 0.0;  // exact values
  Simulator sim(sc);
  Injection set;
  set.type = InjectionType::SignalSet;
  set.signal = SignalId::OilPressure;
  set.magnitude = 60.0;
  set.start_us = 5'000'000;
  set.duration_us = 3'000'000;
  sim.add_injection(set);
  Injection err;
  err.type = InjectionType::SignalError;
  err.signal = SignalId::FuelTemp;
  err.start_us = 5'000'000;
  sim.add_injection(err);
  Injection drop;
  drop.type = InjectionType::PgnDropout;
  drop.pgn = pgn::kVep1;
  drop.start_us = 5'000'000;
  sim.add_injection(drop);

  const auto frames = sim.generate(12'000'000);
  std::size_t vep_before = 0, vep_after = 0;
  for (const auto& f : frames) {
    const auto d = decode_frame(f);
    for (std::size_t i = 0; i < d.count; ++i) {
      const auto& s = d.items[i];
      const bool in_window = f.timestamp_us >= 5'000'000 && f.timestamp_us < 8'000'000;
      if (s.id == SignalId::OilPressure) {
        if (in_window) {
          EXPECT_DOUBLE_EQ(s.value, 60.0);
        } else {
          EXPECT_GT(s.value, 200.0);
        }
      }
      if (s.id == SignalId::FuelTemp && f.timestamp_us >= 5'000'000) {
        EXPECT_EQ(s.status, SignalStatus::Error);
      }
      if (s.id == SignalId::BatteryVoltage) {
        if (f.timestamp_us < 5'000'000) ++vep_before;
        else ++vep_after;
      }
    }
  }
  EXPECT_GT(vep_before, 3u);
  EXPECT_EQ(vep_after, 0u);
}

// Verifies: REQ-SIM-002
TEST(Simulator, BusOverloadReachesTargetLoadAndCausesArbitrationLoss) {
  SimConfig sc;
  Simulator sim(sc);
  Injection o;
  o.type = InjectionType::BusOverload;
  o.magnitude = 0.95;
  o.start_us = 3'000'000;
  sim.add_injection(o);
  const auto frames = sim.generate(8'000'000);
  BusLoadMeter meter(250'000);
  std::size_t eec1_before = 0, eec1_during = 0;
  for (const auto& f : frames) {
    meter.add(f.timestamp_us, estimated_frame_bits(f));
    if (parse_id(f.id).pgn == pgn::kEec1) {
      if (f.timestamp_us < 3'000'000) ++eec1_before;
      else ++eec1_during;
    }
  }
  EXPECT_NEAR(meter.load(8'000'000), 0.95, 0.06);
  // 3 s before vs 5 s during: rate must drop by the configured loss probability (30 %)
  const double rate_before = static_cast<double>(eec1_before) / 3.0;
  const double rate_during = static_cast<double>(eec1_during) / 5.0;
  EXPECT_NEAR(rate_during / rate_before, 0.7, 0.08);
}

// Verifies: REQ-SIM-002
TEST(Simulator, RampInjectionGrowsLinearly) {
  SimConfig sc;
  sc.noise_scale = 0.0;
  Simulator sim(sc);
  Injection r;
  r.type = InjectionType::SignalRamp;
  r.signal = SignalId::CoolantTemp;
  r.magnitude = 0.5;  // degC / s
  r.start_us = 10'000'000;
  sim.add_injection(r);
  const auto frames = sim.generate(60'000'000);
  double at_20 = 0, at_50 = 0;
  for (const auto& f : frames) {
    const auto d = decode_frame(f);
    for (std::size_t i = 0; i < d.count; ++i)
      if (d.items[i].id == SignalId::CoolantTemp) {
        if (f.timestamp_us > 19'900'000 && at_20 == 0) at_20 = d.items[i].value;
        if (f.timestamp_us > 49'900'000 && at_50 == 0) at_50 = d.items[i].value;
      }
  }
  EXPECT_NEAR(at_50 - at_20, 0.5 * 30.0, 1.5);  // slope 0.5 C/s over 30 s (plus slow thermal drift)
}

TEST(Rng, IsPlatformIndependentGoldenSequence) {
  Rng r(1);
  EXPECT_EQ(r.next_u64(), 0x910A2DEC89025CC1ull);  // splitmix64 reference output for seed 1
}

TEST(Rng, NormalHasUnitMoments) {
  Rng r(5);
  double s = 0, s2 = 0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double x = r.normal();
    s += x;
    s2 += x * x;
  }
  EXPECT_NEAR(s / n, 0.0, 0.01);
  EXPECT_NEAR(s2 / n, 1.0, 0.02);
}
