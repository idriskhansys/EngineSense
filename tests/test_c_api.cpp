#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "enginesense/c_api.h"

using namespace std::chrono;

// Verifies: REQ-ARCH-002
TEST(CApi, SimulationInjectionAndEventPolling) {
  es_monitor* m = es_create();
  ASSERT_NE(m, nullptr);
  ASSERT_EQ(es_start_simulation(m, 5, 40.0), 0);

  es_snapshot snap;
  const auto deadline = steady_clock::now() + seconds(10);
  do {
    std::this_thread::sleep_for(milliseconds(20));
    es_get_snapshot(m, &snap);
  } while (snap.frames_processed < 2000 && steady_clock::now() < deadline);
  ASSERT_GE(snap.frames_processed, 2000u);
  EXPECT_EQ(snap.signals[0].status, 1);  // engine speed valid
  EXPECT_GT(snap.signals[0].value, 600.0);
  EXPECT_GE(snap.bus_load, 0.0);
  EXPECT_LT(snap.bus_load, 0.2);

  int idx = -1;
  for (int i = 0; i < es_scenario_count(); ++i)
    if (std::strcmp(es_scenario_name(i), "coolant_overtemp") == 0) idx = i;
  ASSERT_GE(idx, 0);
  ASSERT_EQ(es_inject_scenario(m, idx), 0);
  EXPECT_EQ(es_inject_scenario(m, 9999), -1);

  bool raised = false;
  es_fault f;
  int32_t cleared = 0;
  while (!raised && steady_clock::now() < deadline) {
    while (es_poll_event(m, &f, &cleared)) {
      if (!cleared && std::strcmp(f.type_name, "OverTemperature") == 0) {
        raised = true;
        EXPECT_EQ(f.severity, 2);
        EXPECT_NE(std::strstr(f.message, "Coolant temp"), nullptr);
      }
    }
    std::this_thread::sleep_for(milliseconds(10));
  }
  EXPECT_TRUE(raised);

  es_fault active[16];
  EXPECT_GE(es_get_active_faults(m, active, 16), 0);
  double t[64], v[64];
  EXPECT_GT(es_get_history(m, 2, t, v, 64), 0);
  es_stop(m);
  es_destroy(m);
}

// Verifies: REQ-ARCH-002
TEST(CApi, ReplayOfGoldenLog) {
  es_monitor* m = es_create();
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(es_start_replay(m, "/does/not/exist.log", 0), -1);
  const std::string path = std::string(ES_TEST_DATA_DIR) + "/golden_mixed.log";
  ASSERT_EQ(es_start_replay(m, path.c_str(), 0.0), 0);
  es_snapshot snap;
  const auto deadline = steady_clock::now() + seconds(30);
  do {
    std::this_thread::sleep_for(milliseconds(20));
    es_get_snapshot(m, &snap);
  } while (snap.running && steady_clock::now() < deadline);
  EXPECT_EQ(snap.running, 0);
  EXPECT_GT(snap.frames_processed, 3000u);
  EXPECT_EQ(snap.frames_dropped, 0u);
  es_destroy(m);
}

// Verifies: REQ-ARCH-002, REQ-ROB-001
TEST(CApi, NullAndRangeSafety) {
  es_snapshot snap;
  es_get_snapshot(nullptr, &snap);
  es_get_snapshot(nullptr, nullptr);
  es_stop(nullptr);
  es_destroy(nullptr);
  es_fault f;
  int32_t c;
  EXPECT_EQ(es_poll_event(nullptr, &f, &c), 0);
  EXPECT_EQ(es_get_active_faults(nullptr, &f, 1), 0);
  EXPECT_EQ(es_start_simulation(nullptr, 1, 1.0), -1);
  EXPECT_STREQ(es_signal_name(-1), "");
  EXPECT_STREQ(es_signal_name(ES_SIGNAL_COUNT), "");
  EXPECT_STREQ(es_signal_name(2), "Coolant temp");
  EXPECT_STREQ(es_signal_unit(2), "degC");
  EXPECT_STREQ(es_scenario_name(-1), "");
  EXPECT_GE(es_scenario_count(), 10);
  EXPECT_STREQ(es_version(), "1.0.0");
}
