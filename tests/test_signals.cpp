#include <gtest/gtest.h>

#include <cmath>

#include "enginesense/signals.hpp"
#include "test_util.hpp"

using namespace enginesense;

namespace {
CanFrame raw_frame(std::uint32_t pgn_value, std::initializer_list<std::uint8_t> bytes) {
  CanFrame f = make_frame(0, pgn_value);
  std::size_t i = 0;
  for (auto b : bytes) f.data[i++] = b;
  return f;
}
double value_of(const DecodeResult& r, SignalId id) {
  for (std::size_t i = 0; i < r.count; ++i)
    if (r.items[i].id == id) return r.items[i].value;
  return std::nan("");
}
SignalStatus status_of(const DecodeResult& r, SignalId id) {
  for (std::size_t i = 0; i < r.count; ++i)
    if (r.items[i].id == id) return r.items[i].status;
  return SignalStatus::Unknown;
}
}  // namespace

// Verifies: REQ-DEC-001
TEST(SignalDecode, Eec1ReferenceVector) {
  // speed raw 0x3840 = 14400 * 0.125 = 1800 rpm ; torque raw 125 - 125 = 0 %
  const auto r = decode_frame(raw_frame(pgn::kEec1, {0xFF, 0xFF, 0x7D, 0x40, 0x38, 0xFF, 0xFF, 0xFF}));
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::EngineSpeed), 1800.0);
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::EngineTorque), 0.0);
}

// Verifies: REQ-DEC-001
TEST(SignalDecode, Et1ReferenceVector) {
  // coolant 125-40 = 85 C ; fuel 80-40 = 40 C ; oil raw 0x2E00 = 11776 * 0.03125 - 273 = 95 C
  const auto r = decode_frame(raw_frame(pgn::kEt1, {0x7D, 0x50, 0x00, 0x2E, 0xFF, 0xFF, 0xFF, 0xFF}));
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::CoolantTemp), 85.0);
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::FuelTemp), 40.0);
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::OilTemp), 95.0);
}

// Verifies: REQ-DEC-001
TEST(SignalDecode, EflP1AndVep1ReferenceVectors) {
  auto r = decode_frame(raw_frame(pgn::kEflP1, {0x64, 0xFF, 0xFF, 0x4B, 0xFF, 0xFF, 0xFF, 0xFF}));
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::FuelPressure), 400.0);  // 100 * 4 kPa
  EXPECT_DOUBLE_EQ(value_of(r, SignalId::OilPressure), 300.0);   // 75 * 4 kPa
  r = decode_frame(raw_frame(pgn::kVep1, {0xFF, 0xFF, 0xFF, 0xFF, 0x28, 0x02, 0xFF, 0xFF}));
  EXPECT_NEAR(value_of(r, SignalId::BatteryVoltage), 27.6, 1e-9);  // 0x0228 = 552 * 0.05 V
}

// Verifies: REQ-DEC-001
TEST(SignalDecode, EncodeDecodeRoundTripErrorBelowHalfLsbOverFullRange) {
  for (const SignalDef& d : all_signal_defs()) {
    const std::uint64_t max_raw = (std::uint64_t{0xFA} << (d.length - 8u)) | ((std::uint64_t{1} << (d.length - 8u)) - 1u);
    const double lo = d.offset;
    const double hi = static_cast<double>(max_raw) * d.resolution + d.offset;
    for (int i = 0; i <= 4000; ++i) {
      const double v = lo + (hi - lo) * i / 4000.0;
      CanFrame f = make_frame(0, d.pgn);
      encode_signal(f, d.id, v);
      const auto dec = decode_frame(f);
      ASSERT_EQ(status_of(dec, d.id), SignalStatus::Valid) << d.name << " v=" << v;
      EXPECT_LE(std::fabs(value_of(dec, d.id) - v), d.resolution / 2.0 + 1e-9) << d.name << " v=" << v;
    }
  }
}

// Verifies: REQ-DEC-001
TEST(SignalDecode, EncodingClampsToValidRangeAndLeavesOtherSignalsUntouched) {
  CanFrame f = make_frame(0, pgn::kEt1);
  encode_signal(f, SignalId::CoolantTemp, 5000.0);  // far beyond range
  const auto dec = decode_frame(f);
  EXPECT_EQ(status_of(dec, SignalId::CoolantTemp), SignalStatus::Valid);
  EXPECT_DOUBLE_EQ(value_of(dec, SignalId::CoolantTemp), 0xFA - 40.0);
  EXPECT_EQ(status_of(dec, SignalId::FuelTemp), SignalStatus::NotAvailable);  // still 0xFF
  encode_signal(f, SignalId::CoolantTemp, std::nan(""));
  EXPECT_DOUBLE_EQ(value_of(decode_frame(f), SignalId::CoolantTemp), -40.0);
}

// Verifies: REQ-DEC-002
TEST(SignalDecode, ClassifiesNotAvailableAndErrorIndicators) {
  auto r = decode_frame(raw_frame(pgn::kEt1, {0xFF, 0xFE, 0xFB, 0xFA, 0xFF, 0xFF, 0xFF, 0xFF}));
  EXPECT_EQ(status_of(r, SignalId::CoolantTemp), SignalStatus::NotAvailable);  // 0xFF
  EXPECT_EQ(status_of(r, SignalId::FuelTemp), SignalStatus::Error);            // 0xFE
  // oil temp 16 bit: 0xFAFB -> high byte 0xFA is the largest valid value
  EXPECT_EQ(status_of(r, SignalId::OilTemp), SignalStatus::Valid);

  r = decode_frame(raw_frame(pgn::kEt1, {0xFA, 0xFB, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  EXPECT_EQ(status_of(r, SignalId::CoolantTemp), SignalStatus::Valid);  // 0xFA is the max valid
  EXPECT_EQ(status_of(r, SignalId::FuelTemp), SignalStatus::Error);     // 0xFB reserved -> error
  EXPECT_EQ(status_of(r, SignalId::OilTemp), SignalStatus::NotAvailable);  // 0xFF00

  r = decode_frame(raw_frame(pgn::kEt1, {0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF}));
  EXPECT_EQ(status_of(r, SignalId::OilTemp), SignalStatus::Error);  // 0xFE00
}

// Verifies: REQ-DEC-002
TEST(SignalDecode, EncodeStatusRoundTrips) {
  for (const SignalDef& d : all_signal_defs()) {
    CanFrame f = make_frame(0, d.pgn);
    encode_signal(f, d.id, d.offset + 10 * d.resolution);
    encode_status(f, d.id, SignalStatus::Error);
    EXPECT_EQ(status_of(decode_frame(f), d.id), SignalStatus::Error) << d.name;
    encode_status(f, d.id, SignalStatus::NotAvailable);
    EXPECT_EQ(status_of(decode_frame(f), d.id), SignalStatus::NotAvailable) << d.name;
  }
}

// Verifies: REQ-DEC-001, REQ-ROB-001
TEST(SignalDecode, ShortPayloadsOnlyDecodeSignalsThatFit) {
  CanFrame f = raw_frame(pgn::kEec1, {0xFF, 0xFF, 0x7D, 0x40, 0x38, 0xFF, 0xFF, 0xFF});
  f.dlc = 3;  // torque (byte 3) fits, speed (bytes 4-5) does not
  const auto r = decode_frame(f);
  EXPECT_EQ(r.count, 1u);
  EXPECT_EQ(r.items[0].id, SignalId::EngineTorque);
  f.dlc = 0;
  EXPECT_EQ(decode_frame(f).count, 0u);
}

TEST(SignalDecode, UnknownPgnYieldsNothing) {
  EXPECT_EQ(decode_frame(make_frame(0, 0xFE00)).count, 0u);
}

TEST(SignalDecode, SpnLookup) {
  EXPECT_EQ(signal_from_spn(110), SignalId::CoolantTemp);
  EXPECT_FALSE(signal_from_spn(9999).has_value());
}
