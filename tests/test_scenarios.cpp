#include <gtest/gtest.h>

#include <algorithm>

#include "enginesense/scenarios.hpp"
#include "test_util.hpp"

using namespace enginesense;
using namespace estest;

namespace {
constexpr int kSeedsPerScenario = 25;  // CI budget; `enginesense_bench latency --trials 200` is the long run
TrialConfig cfg_for(int seed) {
  TrialConfig c;
  c.seed = static_cast<std::uint32_t>(seed);
  c.duration_us = 40'000'000;
  return c;
}
}  // namespace

class ScenarioTest : public ::testing::TestWithParam<std::string> {};

// Verifies: REQ-LAT-001, REQ-LAT-002, REQ-LAT-003, REQ-LAT-004, REQ-LAT-005
TEST_P(ScenarioTest, FaultIsDetectedWithinLatencyBudgetForEverySeed) {
  const Scenario& sc = *find_scenario(GetParam());
  std::uint64_t worst = 0;
  for (int seed = 1; seed <= kSeedsPerScenario; ++seed) {
    const TrialResult r = run_trial(sc, cfg_for(seed));
    ASSERT_TRUE(r.detected) << sc.name << " seed " << seed;
    EXPECT_LE(r.latency_us, sc.max_latency_us) << sc.name << " seed " << seed;
    worst = std::max(worst, r.latency_us);
  }
  RecordProperty("worst_latency_ms", static_cast<int>(worst / 1000));
}

// Verifies: REQ-ACC-003
TEST_P(ScenarioTest, NoFalseAlarmsBeforeInjectionAndNoUnexpectedFaultTypes) {
  const Scenario& sc = *find_scenario(GetParam());
  for (int seed = 1; seed <= kSeedsPerScenario; ++seed) {
    const TrialResult r = run_trial(sc, cfg_for(seed));
    EXPECT_EQ(r.false_alarms, 0u) << sc.name << " seed " << seed;
    EXPECT_EQ(r.unexpected, 0u) << sc.name << " seed " << seed;
  }
}

// Verifies: REQ-ACC-002
TEST_P(ScenarioTest, DetectionRateIsOneHundredPercent) {
  const Scenario& sc = *find_scenario(GetParam());
  int detected = 0;
  for (int seed = 1; seed <= kSeedsPerScenario; ++seed) detected += run_trial(sc, cfg_for(seed)).detected ? 1 : 0;
  EXPECT_EQ(detected, kSeedsPerScenario);
}

static std::vector<std::string> scenario_names() {
  std::vector<std::string> v;
  for (const auto& s : standard_scenarios()) v.push_back(s.name);
  return v;
}
INSTANTIATE_TEST_SUITE_P(Standard, ScenarioTest, ::testing::ValuesIn(scenario_names()),
                         [](const ::testing::TestParamInfo<std::string>& pi) { return pi.param; });

// Verifies: REQ-ACC-001
TEST(Nominal, NoFalseAlarmsAcrossHundredSimulatedMinutes) {
  std::size_t fa = 0;
  for (std::uint32_t seed = 1; seed <= 10; ++seed) fa += run_nominal(seed, 600ull * 1'000'000).false_alarms;
  EXPECT_EQ(fa, 0u);
}

// Verifies: REQ-ACC-001
TEST(Nominal, StaysQuietWithDoubleTheNominalSensorNoise) {
  std::size_t fa = 0;
  for (std::uint32_t seed = 1; seed <= 5; ++seed) fa += run_nominal(seed, 600ull * 1'000'000, RuleConfig{}, 2.0).false_alarms;
  EXPECT_EQ(fa, 0u);
}

// Verifies: REQ-FDE-002
TEST(Scenarios, BusOverloadDoesNotProduceConsequentialDropoutFaults) {
  const Scenario& sc = *find_scenario("bus_overload");
  for (int seed = 1; seed <= 25; ++seed) {
    const TrialResult r = run_trial(sc, cfg_for(seed));
    EXPECT_EQ(count_raised(r.notifications, FaultType::SensorDropout), 0u) << "seed " << seed;
  }
}

// Verifies: REQ-FDE-001
TEST(Scenarios, SingleSampleGlitchBelowConfirmationCountIsIgnored) {
  Scenario sc = *find_scenario("coolant_overtemp");
  sc.injection.magnitude = 25.0;
  sc.injection.duration_us = 600'000;  // shorter than the 1 s sample period: at most one reading is affected
  int overtemp = 0;
  for (int seed = 1; seed <= 25; ++seed) overtemp += run_trial(sc, cfg_for(seed)).detected ? 1 : 0;
  EXPECT_EQ(overtemp, 0);
}

TEST(Scenarios, EveryScenarioNamesAKnownRequirement) {
  for (const auto& s : standard_scenarios()) EXPECT_EQ(s.requirement.rfind("REQ-LAT-", 0), 0u) << s.name;
}
