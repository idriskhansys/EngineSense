#include <gtest/gtest.h>

#include <chrono>

#include "enginesense/fault_engine.hpp"
#include "enginesense/simulator.hpp"

using namespace enginesense;

// Verifies: REQ-PERF-001
TEST(Performance, EngineSustainsRequiredFrameRate) {
  const auto frames = Simulator(SimConfig{}).generate(1800ull * 1'000'000);
  FaultEngine engine;
  std::vector<FaultNotification> out;
  const auto t0 = std::chrono::steady_clock::now();
  for (const auto& f : frames) engine.process(f, out);
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double fps = static_cast<double>(frames.size()) / s;
  RecordProperty("frames_per_second", static_cast<int>(fps));
  EXPECT_GT(fps, static_cast<double>(ES_MIN_FPS));
  EXPECT_TRUE(out.empty());
}
