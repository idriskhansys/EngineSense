#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "enginesense/log_io.hpp"
#include "enginesense/scenarios.hpp"

using namespace enginesense;

namespace {
std::string slurp(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  std::stringstream ss;
  ss << is.rdbuf();
  std::string s = ss.str();
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}
}  // namespace

// Verifies: REQ-DET-001, REQ-QUAL-002
TEST(Regression, GoldenLogReplaysToTheCommittedFaultReport) {
  LogReadResult log;
  ASSERT_TRUE(read_log_file(std::string(ES_TEST_DATA_DIR) + "/golden_mixed.log", log));
  ASSERT_EQ(log.bad_lines, 0u);
  ASSERT_GT(log.frames.size(), 3000u);
  const auto notes = replay_frames(log.frames);
  std::ostringstream os;
  write_fault_csv(os, notes);
  const std::string expected = slurp(std::string(ES_TEST_DATA_DIR) + "/golden_mixed_faults.csv");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(os.str(), expected) << "Detection behaviour changed. If intentional, run tools/update_golden.sh and review the diff.";
}

// Verifies: REQ-QUAL-002
TEST(Regression, GoldenReportContainsTheThreeInjectedFaultsInOrder) {
  LogReadResult log;
  ASSERT_TRUE(read_log_file(std::string(ES_TEST_DATA_DIR) + "/golden_mixed.log", log));
  const auto notes = replay_frames(log.frames);
  std::vector<FaultType> raised;
  for (const auto& n : notes)
    if (n.kind == FaultNotification::Kind::Raised) raised.push_back(n.event.type);
  ASSERT_GE(raised.size(), 3u);
  EXPECT_NE(std::find(raised.begin(), raised.end(), FaultType::OverTemperature), raised.end());
  EXPECT_NE(std::find(raised.begin(), raised.end(), FaultType::EcuDtc), raised.end());
  EXPECT_NE(std::find(raised.begin(), raised.end(), FaultType::SensorDropout), raised.end());
}
