#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "enginesense/log_io.hpp"
#include "enginesense/monitor.hpp"
#include "enginesense/scenarios.hpp"

using namespace enginesense;
using namespace std::chrono;

namespace {
std::vector<CanFrame> golden() {
  LogReadResult log;
  EXPECT_TRUE(read_log_file(std::string(ES_TEST_DATA_DIR) + "/golden_mixed.log", log));
  return log.frames;
}
}  // namespace

// Verifies: REQ-PERF-003, REQ-DET-001
TEST(Monitor, ThreadedReplayLosesNoFramesAndMatchesOfflineResult) {
  const auto frames = golden();
  const auto offline = replay_frames(frames);

  Monitor m;
  Monitor::Options o;
  o.speed = 0;  // as fast as possible, with back-pressure
  o.queue_capacity = 64;  // tiny queue forces the producer to block repeatedly
  ASSERT_TRUE(m.start_replay(frames, o));
  ASSERT_TRUE(m.wait_finished(seconds(30)));
  const auto snap = m.snapshot();
  EXPECT_EQ(snap.frames_processed, frames.size());
  EXPECT_EQ(snap.frames_dropped, 0u);
  EXPECT_FALSE(snap.running);

  const auto online = m.all_notifications();
  ASSERT_EQ(online.size(), offline.size());
  for (std::size_t i = 0; i < online.size(); ++i) {
    EXPECT_EQ(online[i].event.key, offline[i].event.key);
    EXPECT_EQ(online[i].event.raised_us, offline[i].event.raised_us);
    EXPECT_EQ(online[i].kind, offline[i].kind);
  }
  EXPECT_EQ(m.drain_notifications().size(), online.size());
  EXPECT_TRUE(m.drain_notifications().empty());
}

// Verifies: REQ-PERF-002
TEST(Monitor, SnapshotLatencyStaysBelowFrameBudgetWhileEngineIsSaturated) {
  Monitor m;
  Monitor::Options o;
  o.speed = 0;
  o.duration_us = 6000ull * 1'000'000;  // ~630k frames as fast as the machine can go
  ASSERT_TRUE(m.start_simulation(SimConfig{}, o));

  std::vector<double> lat_ms;
  while (m.running()) {
    const auto t0 = steady_clock::now();
    const MonitorSnapshot s = m.snapshot();
    (void)m.history(SignalId::CoolantTemp, 300);
    lat_ms.push_back(duration<double, std::milli>(steady_clock::now() - t0).count());
    (void)s;
    std::this_thread::sleep_for(microseconds(500));
  }
  ASSERT_TRUE(m.wait_finished(seconds(60)));
  ASSERT_GT(lat_ms.size(), 50u);
  std::sort(lat_ms.begin(), lat_ms.end());
  const double p99 = lat_ms[static_cast<std::size_t>(0.99 * static_cast<double>(lat_ms.size() - 1))];
  RecordProperty("snapshot_p99_us", static_cast<int>(p99 * 1000));
  EXPECT_LT(p99, static_cast<double>(ES_SNAPSHOT_P99_MS));
  EXPECT_GT(m.snapshot().frames_processed, 500000u);
}

// Verifies: REQ-PERF-003
TEST(Monitor, RealTimeModeNeverBlocksProducerAndAccountsForEveryLostFrame) {
  std::vector<CanFrame> burst;
  for (std::uint64_t i = 0; i < 2000; ++i) {
    CanFrame f = make_frame(i * 10, pgn::kEec1);  // 100 kframes/s burst
    encode_signal(f, SignalId::EngineSpeed, 1500.0);
    burst.push_back(f);
  }
  Monitor m;
  Monitor::Options o;
  o.speed = 1.0;                        // real-time replay => try_push, never blocks
  o.queue_capacity = 16;
  o.debug_consumer_delay_us = 500;      // consumer ~100x slower than the producer
  ASSERT_TRUE(m.start_replay(burst, o));
  ASSERT_TRUE(m.wait_finished(seconds(30)));
  const auto s = m.snapshot();
  EXPECT_EQ(s.frames_processed + s.frames_dropped, burst.size());  // nothing vanishes silently
  EXPECT_GT(s.frames_dropped, burst.size() / 2);
  EXPECT_GT(s.frames_processed, 0u);
  EXPECT_LE(s.queue_depth, 16u);
}

TEST(Monitor, LiveInjectionIsDetectedAndReportedToTheUiThread) {
  Monitor m;
  Monitor::Options o;
  o.speed = 40.0;  // 40x real time
  ASSERT_TRUE(m.start_simulation(SimConfig{}, o));
  std::this_thread::sleep_for(milliseconds(100));
  m.inject_scenario(*find_scenario("oil_pressure_low"));
  bool seen = false;
  const auto deadline = steady_clock::now() + seconds(10);
  while (!seen && steady_clock::now() < deadline) {
    for (const auto& n : m.drain_notifications())
      if (n.kind == FaultNotification::Kind::Raised && n.event.type == FaultType::LowOilPressure) seen = true;
    std::this_thread::sleep_for(milliseconds(10));
  }
  EXPECT_TRUE(seen);
  const auto snap = m.snapshot();
  EXPECT_TRUE(snap.running);
  EXPECT_GT(snap.time_us, 0u);
  EXPECT_FALSE(m.history(SignalId::EngineSpeed, 100).empty());
  m.stop();
  EXPECT_FALSE(m.running());
}

TEST(Monitor, CanBeStoppedMidRunAndRestarted) {
  Monitor m;
  Monitor::Options o;
  o.speed = 20.0;
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(m.start_simulation(SimConfig{}, o));
    std::this_thread::sleep_for(milliseconds(30));
    m.stop();
    EXPECT_FALSE(m.running());
  }
}
