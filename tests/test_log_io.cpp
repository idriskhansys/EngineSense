#include <gtest/gtest.h>

#include <sstream>

#include "enginesense/log_io.hpp"
#include "enginesense/simulator.hpp"

using namespace enginesense;

// Verifies: REQ-LOG-001
TEST(LogIo, RoundTripIsLosslessForSimulatedTraffic) {
  Simulator sim(SimConfig{});
  const auto frames = sim.generate(30'000'000);
  ASSERT_GT(frames.size(), 3000u);
  ASSERT_EQ(frames.front().timestamp_us, 0u);
  std::stringstream ss;
  write_log(ss, frames);
  const LogReadResult r = read_log(ss);
  EXPECT_EQ(r.bad_lines, 0u);
  ASSERT_EQ(r.frames.size(), frames.size());
  for (std::size_t i = 0; i < frames.size(); ++i) ASSERT_EQ(r.frames[i], frames[i]) << "frame " << i;
}

// Verifies: REQ-LOG-001
TEST(LogIo, FormatMatchesCandumpL) {
  CanFrame f;
  f.timestamp_us = 1'500'250;
  f.id = 0x0CF00400;
  f.dlc = 4;
  f.data = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0};
  EXPECT_EQ(format_log_line(f, "can0", 1'700'000'000), "(1700000001.500250) can0 0CF00400#DEADBEEF");
}

// Verifies: REQ-LOG-001
TEST(LogIo, ParsesForeignCandumpFilesRelativeToFirstFrame) {
  std::istringstream is(
      "# comment\n"
      "(1620000000.250000) vcan0 18FEEE00#7D50002EFFFFFFFF\n"
      "\r\n"
      "(1620000001.250000) vcan0 18FEEE00#7D50002EFFFFFFFF\r\n"
      "(1620000001.5) can1 0CF00400#FFFF7D4038FFFFFF\n");
  const auto r = read_log(is);
  EXPECT_EQ(r.bad_lines, 0u);
  ASSERT_EQ(r.frames.size(), 3u);
  EXPECT_EQ(r.frames[0].timestamp_us, 0u);
  EXPECT_EQ(r.frames[1].timestamp_us, 1'000'000u);
  EXPECT_EQ(r.frames[2].timestamp_us, 1'250'000u);
  EXPECT_EQ(r.frames[2].id, 0x0CF00400u);
}

// Verifies: REQ-LOG-001, REQ-ROB-001
TEST(LogIo, MalformedLinesAreCountedNotFatal) {
  std::istringstream is(
      "(1.000000) can0 18FEEE00#7D50\n"        // ok
      "garbage line\n"                          // bad
      "(1.000000) can0 18FEEE00#7D5\n"          // odd number of hex digits
      "(1.000000) can0 18FEEE00#7D50002EFFFFFFFFFF\n"  // 9 bytes
      "(x.000000) can0 18FEEE00#7D50\n"         // bad timestamp
      "(1.000000) can0 3FFFFFFFF#00\n"          // id wider than 29 bits
      "(1.000000) can0 18FEEE00#GG\n"           // not hex
      "(2.000000) can0 18FEEE00#\n");           // empty payload is legal (dlc 0)
  const auto r = read_log(is);
  EXPECT_EQ(r.frames.size(), 2u);
  EXPECT_EQ(r.bad_lines, 6u);
  EXPECT_EQ(r.first_bad_line, 2u);
}

TEST(LogIo, MissingFileReportsFailure) {
  LogReadResult r;
  EXPECT_FALSE(read_log_file("/definitely/not/here.log", r));
}
