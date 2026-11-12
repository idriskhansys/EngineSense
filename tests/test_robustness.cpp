#include <gtest/gtest.h>

#include <sstream>

#include "enginesense/fault_engine.hpp"
#include "enginesense/log_io.hpp"
#include "enginesense/simulator.hpp"

using namespace enginesense;

// Verifies: REQ-ROB-001
TEST(Robustness, RandomFramesNeverCrashDecoderOrEngine) {
  Rng rng(2024);
  FaultEngine engine;
  std::vector<FaultNotification> out;
  std::uint64_t t = 0;
  for (int i = 0; i < 300000; ++i) {
    CanFrame f;
    t += rng.next_u64() % 3000;
    f.timestamp_us = t;
    // bias towards the PGNs we actually decode so the interesting paths are hit
    const std::uint32_t pgns[] = {pgn::kEec1, pgn::kEt1, pgn::kEflP1, pgn::kVep1, pgn::kDm1, 0x1234};
    f.id = (rng.next_u64() % 3 == 0) ? static_cast<std::uint32_t>(rng.next_u64()) & 0x1FFFFFFFu
                                     : make_id(static_cast<std::uint8_t>(rng.next_u64() % 8), pgns[rng.next_u64() % 6],
                                               static_cast<std::uint8_t>(rng.next_u64()));
    f.dlc = static_cast<std::uint8_t>(rng.next_u64() % 12);  // includes illegal DLC > 8
    for (auto& b : f.data) b = static_cast<std::uint8_t>(rng.next_u64());
    engine.process(f, out);
    if (out.size() > 100000) out.clear();
  }
  SUCCEED();
}

// Verifies: REQ-ROB-001
TEST(Robustness, EveryDlcAndEveryPgnDecodesSafely) {
  for (std::uint8_t dlc = 0; dlc <= 8; ++dlc)
    for (std::uint32_t p : {pgn::kEec1, pgn::kEt1, pgn::kEflP1, pgn::kVep1, pgn::kDm1}) {
      CanFrame f = make_frame(0, p);
      f.dlc = dlc;
      (void)decode_frame(f);
      (void)parse_dm1(f);
    }
}

// Verifies: REQ-ROB-001, REQ-LOG-001
TEST(Robustness, RandomTextNeverBreaksTheLogParser) {
  Rng rng(99);
  std::string text;
  const std::string alphabet = "0123456789ABCDEFabcdef()#. \n\r-:xG";
  for (int i = 0; i < 200000; ++i) text.push_back(alphabet[rng.next_u64() % alphabet.size()]);
  std::istringstream is(text);
  const auto r = read_log(is);
  EXPECT_GE(r.bad_lines + r.frames.size(), 1u);
}

// Verifies: REQ-ROB-001
TEST(Robustness, EngineSurvivesTimestampJumpsInBothDirections) {
  FaultEngine e;
  std::vector<FaultNotification> out;
  Rng rng(3);
  for (int i = 0; i < 20000; ++i) {
    CanFrame f = make_frame(rng.next_u64() % 4'000'000'000ull, pgn::kEt1);
    e.process(f, out);
    if (out.size() > 10000) out.clear();
  }
  SUCCEED();
}
