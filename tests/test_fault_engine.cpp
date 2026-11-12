#include <gtest/gtest.h>

#include "test_util.hpp"

using namespace enginesense;
using namespace estest;

namespace {
/// Feed 1 Hz ET1 frames carrying `temps`; returns all notifications. Time starts at t0.
std::vector<FaultNotification> feed_et1(FaultEngine& e, const std::vector<double>& temps, std::uint64_t t0 = 0) {
  std::vector<FaultNotification> all;
  std::uint64_t t = t0;
  for (double c : temps) {
    e.process(et1(t, c), all);
    t += 1'000'000;
  }
  return all;
}
}  // namespace

// Verifies: REQ-FDE-001
TEST(FaultEngine, SingleOutlierSampleDoesNotRaiseButTwoConsecutiveDo) {
  FaultEngine e;
  auto n = feed_et1(e, {85, 85, 120, 85, 85, 85});  // lone glitch
  EXPECT_EQ(count_raised(n, FaultType::OverTemperature), 0u);

  FaultEngine e2;
  n = feed_et1(e2, {85, 85, 120, 121, 85});
  EXPECT_EQ(count_raised(n, FaultType::OverTemperature), 1u);
}

// Verifies: REQ-FDE-001
TEST(FaultEngine, HysteresisKeepsFaultActiveInsideBandAndClearsBelowIt) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  std::uint64_t t = 0;
  auto step = [&](double c) {
    e.process(et1(t, c), n);
    t += 1'000'000;
  };
  step(85);
  step(110);
  step(110);                               // raised
  ASSERT_EQ(count_raised(n, FaultType::OverTemperature), 1u);
  for (int i = 0; i < 6; ++i) step(103);   // inside the 102..105 band: stays active, never clears
  EXPECT_EQ(count_cleared(n, FaultType::OverTemperature), 0u);
  for (int i = 0; i < 4; ++i) step(95);    // below the band: clears after clear_after_us (2 s)
  EXPECT_EQ(count_cleared(n, FaultType::OverTemperature), 1u);
  for (const auto& f : e.active_faults()) EXPECT_NE(f.type, FaultType::OverTemperature);  // (other PGNs never arrive here)
}

// Verifies: REQ-FDE-001
TEST(FaultEngine, FlappingAroundThresholdDoesNotRetrigger) {
  FaultEngine e;
  std::vector<double> temps = {85, 110, 110};
  for (int i = 0; i < 20; ++i) temps.push_back(i % 2 ? 106.0 : 103.5);  // jitter across the limit, within band
  const auto n = feed_et1(e, temps);
  EXPECT_EQ(count_raised(n, FaultType::OverTemperature), 1u);
  EXPECT_EQ(count_cleared(n, FaultType::OverTemperature), 0u);
}

// Verifies: REQ-FDE-001
TEST(FaultEngine, OilPressureRuleIsGatedByEngineSpeed) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  std::uint64_t t = 0;
  for (int i = 0; i < 6; ++i, t += 500'000) {  // engine stopped: low pressure is normal
    e.process(eec1(t, 0.0), n);
    e.process(eflp1(t, 20.0), n);
  }
  EXPECT_EQ(count_raised(n, FaultType::LowOilPressure), 0u);
  for (int i = 0; i < 6; ++i, t += 500'000) {  // running with no pressure: fault
    e.process(eec1(t, 1200.0), n);
    e.process(eflp1(t, 20.0), n);
  }
  EXPECT_EQ(count_raised(n, FaultType::LowOilPressure), 1u);
}

// Verifies: REQ-LAT-002
TEST(FaultEngine, DropoutOfFastPgnIsDetectedByTickWithoutAnyOtherFrame) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  for (std::uint64_t t = 0; t <= 200'000; t += 10'000) e.process(eec1(t, 1500.0), n);
  ASSERT_TRUE(n.empty());
  // bus completely silent from t = 200 ms; only a timer drives the engine
  e.tick(215'000, n);
  EXPECT_TRUE(n.empty());
  e.tick(260'000, n);
  ASSERT_EQ(count_raised(n, FaultType::SensorDropout), 1u);  // EEC1 only; the 1 Hz/2 Hz PGNs are still within their startup grace
  EXPECT_EQ(n[0].event.key, "stale.EEC1");
}

// Verifies: REQ-LAT-002
TEST(FaultEngine, StaleRuleFiresWithinOneTickOfThreePeriods) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  for (std::uint64_t t = 0; t <= 200'000; t += 10'000) e.process(eec1(t, 1500.0), n);
  n.clear();
  e.tick(229'000, n);
  for (const auto& x : n) EXPECT_NE(x.event.key, "stale.EEC1");
  n.clear();
  e.tick(241'000, n);
  bool found = false;
  for (const auto& x : n)
    if (x.event.key == "stale.EEC1") {
      found = true;
      EXPECT_LE(x.event.raised_us - 200'000, 50'000u);
    }
  EXPECT_TRUE(found);
}

// Verifies: REQ-LAT-003
TEST(FaultEngine, StaleStartupGraceIsThreePeriodsFromFirstObservation) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  e.process(eec1(0, 1500.0), n);  // only EEC1 ever arrives
  e.tick(2'900'000, n);
  for (const auto& x : n) EXPECT_NE(x.event.key, "stale.ET1");
  e.tick(3'050'000, n);
  std::size_t et1_stale = 0;
  for (const auto& x : n)
    if (x.event.key == "stale.ET1") ++et1_stale;
  EXPECT_EQ(et1_stale, 1u);
}

// Verifies: REQ-FDE-003
TEST(FaultEngine, DtcLifecycleRaiseOnceRefreshThenClearWhenEcuReportsNone) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  LampStatus amber;
  amber.amber_warning = 1;
  const Dtc d{110, 0, 1, false};
  for (int i = 0; i < 4; ++i) e.process(build_dm1(static_cast<std::uint64_t>(i) * 1'000'000, amber, d), n);
  ASSERT_EQ(count_raised(n, FaultType::EcuDtc), 1u);  // not re-raised by refreshes
  EXPECT_EQ(n[0].event.severity, Severity::Warning);
  EXPECT_EQ(n[0].event.signal, SignalId::CoolantTemp);
  EXPECT_NE(n[0].event.message.find("SPN 110 FMI 0"), std::string::npos);
  e.process(build_dm1(4'000'000, LampStatus{}, std::nullopt), n);
  EXPECT_EQ(count_cleared(n, FaultType::EcuDtc), 1u);
  for (const auto& f : e.active_faults()) EXPECT_NE(f.type, FaultType::EcuDtc);
}

// Verifies: REQ-FDE-003
TEST(FaultEngine, DtcClearsWhenDm1RefreshStops) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  LampStatus red;
  red.red_stop = 1;
  e.process(build_dm1(0, red, Dtc{100, 1, 1, false}), n);
  ASSERT_EQ(count_raised(n, FaultType::EcuDtc), 1u);
  EXPECT_EQ(n[0].event.severity, Severity::Critical);
  e.tick(3'200'000, n);
  EXPECT_EQ(count_cleared(n, FaultType::EcuDtc), 1u);
}

// Verifies: REQ-FDE-001
TEST(FaultEngine, RateRuleLatchesSpikeThenReleases) {
  FaultEngine e;
  const auto n = feed_et1(e, {85, 85, 101, 85, 85, 85, 85, 85, 85});  // 16 degC jump and back, under the limit
  EXPECT_EQ(count_raised(n, FaultType::ImplausibleRate), 1u);
  EXPECT_EQ(count_cleared(n, FaultType::ImplausibleRate), 1u);
  EXPECT_EQ(count_raised(n, FaultType::OverTemperature), 0u);
}

// Verifies: REQ-FDE-002
TEST(FaultEngine, DropoutFaultsAreSuppressedWhileBusIsStarved) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  for (std::uint64_t t = 0; t < 200'000; t += 10'000) e.process(eec1(t, 1500.0), n);
  // babbling node: 95 % load of high-priority frames, nothing from EEC1 any more
  std::uint64_t t = 200'000;
  for (; t < 1'500'000; t += 600) {
    CanFrame f;
    f.timestamp_us = t;
    f.id = make_id(0, pgn::kProprietaryB, 0x80);
    f.dlc = 8;
    e.process(f, n);
  }
  EXPECT_EQ(count_raised(n, FaultType::BusOverload), 1u);
  EXPECT_EQ(count_raised(n, FaultType::SensorDropout), 0u);
}

// Verifies: REQ-FDE-001
TEST(FaultEngine, BatteryLowAndHighAreIndependentRules) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  for (int i = 0; i < 4; ++i) e.process(vep1(static_cast<std::uint64_t>(i) * 1'000'000, 20.0), n);
  EXPECT_EQ(count_raised(n, FaultType::LowBattery), 1u);
  EXPECT_EQ(count_raised(n, FaultType::HighBattery), 0u);
  EXPECT_EQ(count_raised(n, FaultType::ImplausibleRate), 0u);
}

// Verifies: REQ-ARCH-003
TEST(FaultEngine, CustomRulesCanBeAddedWithoutTouchingTheEngine) {
  struct FuelTempRule final : Rule {
    FuelTempRule() : Rule([] {
                        RuleSpec s;
                        s.key = "custom.fuel_temp";
                        s.type = FaultType::OverTemperature;
                        return s;
                      }()) {}
    RuleResult evaluate(const EvalContext& c) override {
      return {c.signals[index_of(SignalId::FuelTemp)].value > 60.0, c.signals[index_of(SignalId::FuelTemp)].value};
    }
    std::string describe(double v) const override { return "fuel hot " + std::to_string(v); }
  };
  FaultEngine e;
  e.add_rule(std::make_unique<FuelTempRule>());
  std::vector<FaultNotification> n;
  e.process(et1(0, 85.0, 95.0, 70.0), n);
  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].event.key, "custom.fuel_temp");
}

// Verifies: REQ-DET-001
TEST(FaultEngine, SameInputGivesIdenticalOutputAndResetRestoresInitialState) {
  std::vector<CanFrame> frames;
  for (std::uint64_t t = 0; t < 20'000'000; t += 1'000'000) frames.push_back(et1(t, t > 8'000'000 ? 112.0 : 85.0));
  FaultEngine a, b;
  std::vector<FaultNotification> na, nb, nc;
  for (const auto& f : frames) {
    a.process(f, na);
    b.process(f, nb);
  }
  ASSERT_EQ(na.size(), nb.size());
  for (std::size_t i = 0; i < na.size(); ++i) {
    EXPECT_EQ(na[i].event.key, nb[i].event.key);
    EXPECT_EQ(na[i].event.raised_us, nb[i].event.raised_us);
  }
  a.reset();
  for (const auto& f : frames) a.process(f, nc);
  ASSERT_EQ(nc.size(), na.size());
  for (std::size_t i = 0; i < na.size(); ++i) EXPECT_EQ(nc[i].event.id, na[i].event.id);
}

// Verifies: REQ-ROB-001
TEST(FaultEngine, OutOfOrderTimestampsNeverMoveTimeBackwards) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  e.process(et1(5'000'000, 85.0), n);
  e.process(et1(1'000'000, 85.0), n);  // late frame
  EXPECT_EQ(e.now_us(), 5'000'000u);
  e.tick(2'000'000, n);
  EXPECT_EQ(e.now_us(), 5'000'000u);
}

// Verifies: REQ-ROB-001
TEST(FaultEngine, HugeGapsAreEvaluatedInBoundedTime) {
  FaultEngine e;
  std::vector<FaultNotification> n;
  e.process(et1(0, 85.0), n);
  e.process(et1(86'400'000'000ull, 85.0), n);  // one day later
  EXPECT_GE(count_raised(n, FaultType::SensorDropout), 1u);
}
