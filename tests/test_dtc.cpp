#include <gtest/gtest.h>

#include "enginesense/dtc.hpp"
#include "enginesense/simulator.hpp"

using namespace enginesense;

// Verifies: REQ-DTC-001
TEST(Dm1, ParsesReferenceFrameWithAmberLamp) {
  CanFrame f = make_frame(0, pgn::kDm1);
  const std::uint8_t bytes[8] = {0x04, 0xFF, 0x6E, 0x00, 0x00, 0x01, 0xFF, 0xFF};  // SPN 110 FMI 0 OC 1
  for (int i = 0; i < 8; ++i) f.data[static_cast<std::size_t>(i)] = bytes[i];
  const auto dm1 = parse_dm1(f);
  ASSERT_TRUE(dm1.has_value());
  EXPECT_EQ(dm1->lamps.amber_warning, 1);
  EXPECT_EQ(dm1->lamps.red_stop, 0);
  ASSERT_TRUE(dm1->dtc.has_value());
  EXPECT_EQ(dm1->dtc->spn, 110u);
  EXPECT_EQ(dm1->dtc->fmi, 0);
  EXPECT_EQ(dm1->dtc->occurrence, 1);
  EXPECT_FALSE(dm1->dtc->conversion_method);
}

// Verifies: REQ-DTC-001
TEST(Dm1, ParsesNineteenBitSpnAndAllFieldExtremes) {
  CanFrame f = make_frame(0, pgn::kDm1);
  const std::uint8_t bytes[8] = {0x14, 0xFF, 0x34, 0x12, 0x51, 0xFF, 0xFF, 0xFF};
  for (int i = 0; i < 8; ++i) f.data[static_cast<std::size_t>(i)] = bytes[i];
  const auto dm1 = parse_dm1(f);
  ASSERT_TRUE(dm1 && dm1->dtc);
  EXPECT_EQ(dm1->dtc->spn, 0x21234u);  // 0x34 | 0x12 << 8 | (0x51 >> 5) << 16 : SPN above 65535
  EXPECT_EQ(dm1->dtc->fmi, 17);
  EXPECT_EQ(dm1->dtc->occurrence, 127);
  EXPECT_TRUE(dm1->dtc->conversion_method);
  EXPECT_EQ(dm1->lamps.red_stop, 1);
  EXPECT_EQ(dm1->lamps.amber_warning, 1);
}

// Verifies: REQ-DTC-001
TEST(Dm1, NoActiveFaultFrameHasNoDtc) {
  const auto dm1 = parse_dm1(build_dm1(0, LampStatus{}, std::nullopt));
  ASSERT_TRUE(dm1.has_value());
  EXPECT_FALSE(dm1->dtc.has_value());
}

// Verifies: REQ-DTC-001
TEST(Dm1, BuildParseRoundTripOverRandomValues) {
  Rng rng(7);
  for (int i = 0; i < 5000; ++i) {
    Dtc d;
    d.spn = static_cast<std::uint32_t>(rng.next_u64() % 0x80000u);
    d.fmi = static_cast<std::uint8_t>(rng.next_u64() % 32);
    d.occurrence = static_cast<std::uint8_t>(rng.next_u64() % 128);
    d.conversion_method = (rng.next_u64() & 1u) != 0;
    if (d.spn == 0 && d.fmi == 0) d.spn = 1;
    LampStatus l;
    l.malfunction = static_cast<std::uint8_t>(rng.next_u64() % 4);
    l.red_stop = static_cast<std::uint8_t>(rng.next_u64() % 4);
    l.amber_warning = static_cast<std::uint8_t>(rng.next_u64() % 4);
    l.protect = static_cast<std::uint8_t>(rng.next_u64() % 4);
    const auto parsed = parse_dm1(build_dm1(0, l, d));
    ASSERT_TRUE(parsed && parsed->dtc);
    EXPECT_EQ(*parsed->dtc, d);
    EXPECT_EQ(parsed->lamps.malfunction, l.malfunction);
    EXPECT_EQ(parsed->lamps.red_stop, l.red_stop);
    EXPECT_EQ(parsed->lamps.amber_warning, l.amber_warning);
    EXPECT_EQ(parsed->lamps.protect, l.protect);
  }
}

// Verifies: REQ-DTC-001, REQ-ROB-001
TEST(Dm1, RejectsWrongPgnAndShortFrames) {
  EXPECT_FALSE(parse_dm1(make_frame(0, pgn::kEt1)).has_value());
  CanFrame f = build_dm1(0, LampStatus{}, std::nullopt);
  f.dlc = 5;
  EXPECT_FALSE(parse_dm1(f).has_value());
}

TEST(Dm1, FmiDescriptionsExist) {
  for (int i = 0; i < 32; ++i) EXPECT_NE(std::string(describe_fmi(static_cast<std::uint8_t>(i))), "");
}
