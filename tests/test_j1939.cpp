#include <gtest/gtest.h>

#include "enginesense/j1939.hpp"
#include "enginesense/simulator.hpp"

using namespace enginesense;

// Verifies: REQ-DEC-003
TEST(J1939Id, ParsesPdu2BroadcastMessages) {
  const J1939Id eec1 = parse_id(0x0CF00400);
  EXPECT_EQ(eec1.priority, 3);
  EXPECT_EQ(eec1.pgn, 0xF004u);
  EXPECT_EQ(eec1.source, 0x00);
  EXPECT_EQ(eec1.destination, 0xFF);

  const J1939Id et1 = parse_id(0x18FEEE17);
  EXPECT_EQ(et1.priority, 6);
  EXPECT_EQ(et1.pgn, 0xFEEEu);
  EXPECT_EQ(et1.source, 0x17);
}

// Verifies: REQ-DEC-003
TEST(J1939Id, ParsesPdu1DestinationSpecificMessages) {
  const J1939Id req = parse_id(0x18EA2511);  // Request (PGN 0xEA00) from SA 0x11 to DA 0x25
  EXPECT_EQ(req.pgn, 0xEA00u);                // PS is the destination, not part of the PGN
  EXPECT_EQ(req.destination, 0x25);
  EXPECT_EQ(req.source, 0x11);
}

// Verifies: REQ-DEC-003
TEST(J1939Id, HonoursDataPageBit) {
  const J1939Id id = parse_id(make_id(6, 0x1FF00, 0x80));
  EXPECT_EQ(id.pgn, 0x1FF00u);
  EXPECT_EQ(id.priority, 6);
}

// Verifies: REQ-DEC-003
TEST(J1939Id, MakeIdIsInverseOfParseId) {
  Rng rng(42);
  for (int i = 0; i < 20000; ++i) {
    const std::uint32_t can_id = static_cast<std::uint32_t>(rng.next_u64()) & 0x1CFFFFFFu;  // bit 25 (EDP) cleared
    const J1939Id p = parse_id(can_id);
    EXPECT_EQ(make_id(p.priority, p.pgn, p.source, p.destination), can_id);
  }
}

TEST(J1939Id, KnownPeriods) {
  EXPECT_EQ(nominal_period_us(pgn::kEec1), 10'000u);
  EXPECT_EQ(nominal_period_us(pgn::kEt1), 1'000'000u);
  EXPECT_EQ(nominal_period_us(0x1234), 0u);
}
