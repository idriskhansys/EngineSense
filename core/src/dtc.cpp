#include "enginesense/dtc.hpp"

#include "enginesense/j1939.hpp"
#include "enginesense/signals.hpp"

namespace enginesense {

std::optional<Dm1> parse_dm1(const CanFrame& f) noexcept {
  if (parse_id(f.id).pgn != pgn::kDm1 || f.dlc < 6) return std::nullopt;
  Dm1 out;
  const std::uint8_t b0 = f.data[0];
  out.lamps.malfunction = static_cast<std::uint8_t>((b0 >> 6) & 0x3u);
  out.lamps.red_stop = static_cast<std::uint8_t>((b0 >> 4) & 0x3u);
  out.lamps.amber_warning = static_cast<std::uint8_t>((b0 >> 2) & 0x3u);
  out.lamps.protect = static_cast<std::uint8_t>(b0 & 0x3u);

  Dtc d;
  d.spn = static_cast<std::uint32_t>(f.data[2]) | (static_cast<std::uint32_t>(f.data[3]) << 8) |
          (static_cast<std::uint32_t>((f.data[4] >> 5) & 0x7u) << 16);
  d.fmi = static_cast<std::uint8_t>(f.data[4] & 0x1Fu);
  d.conversion_method = (f.data[5] & 0x80u) != 0;
  d.occurrence = static_cast<std::uint8_t>(f.data[5] & 0x7Fu);
  if (d.spn != 0 || d.fmi != 0) out.dtc = d;  // SPN 0 / FMI 0 is the "no fault" placeholder
  return out;
}

CanFrame build_dm1(std::uint64_t ts, const LampStatus& l, const std::optional<Dtc>& dtc,
                   std::uint8_t source) noexcept {
  CanFrame f = make_frame(ts, pgn::kDm1, source, 6);
  f.data[0] = static_cast<std::uint8_t>(((l.malfunction & 3u) << 6) | ((l.red_stop & 3u) << 4) |
                                        ((l.amber_warning & 3u) << 2) | (l.protect & 3u));
  f.data[1] = 0xFF;  // flash lamp status: not available
  if (dtc) {
    f.data[2] = static_cast<std::uint8_t>(dtc->spn & 0xFFu);
    f.data[3] = static_cast<std::uint8_t>((dtc->spn >> 8) & 0xFFu);
    f.data[4] = static_cast<std::uint8_t>((((dtc->spn >> 16) & 0x7u) << 5) | (dtc->fmi & 0x1Fu));
    f.data[5] = static_cast<std::uint8_t>((dtc->conversion_method ? 0x80u : 0u) | (dtc->occurrence & 0x7Fu));
  } else {
    f.data[2] = f.data[3] = f.data[4] = f.data[5] = 0;
  }
  f.data[6] = f.data[7] = 0xFF;
  return f;
}

const char* describe_fmi(std::uint8_t fmi) noexcept {
  switch (fmi) {
    case 0: return "data valid but above normal operational range (most severe)";
    case 1: return "data valid but below normal operational range (most severe)";
    case 2: return "data erratic, intermittent or incorrect";
    case 3: return "voltage above normal or shorted to high source";
    case 4: return "voltage below normal or shorted to low source";
    case 5: return "current below normal or open circuit";
    case 6: return "current above normal or grounded circuit";
    case 7: return "mechanical system not responding";
    case 8: return "abnormal frequency, pulse width or period";
    case 9: return "abnormal update rate";
    case 10: return "abnormal rate of change";
    case 11: return "root cause not known";
    case 12: return "bad intelligent device or component";
    case 13: return "out of calibration";
    case 14: return "special instructions";
    case 15: return "data valid but above normal operating range (least severe)";
    case 16: return "data valid but above normal operating range (moderately severe)";
    case 17: return "data valid but below normal operating range (least severe)";
    case 18: return "data valid but below normal operating range (moderately severe)";
    case 19: return "received network data in error";
    case 20: return "data drifted high";
    case 21: return "data drifted low";
    case 31: return "condition exists";
    default: return "reserved / proprietary";
  }
}

}  // namespace enginesense
