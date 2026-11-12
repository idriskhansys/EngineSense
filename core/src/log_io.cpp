#include "enginesense/log_io.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace enginesense {
namespace {

bool parse_hex(const std::string& s, std::uint64_t& out) {
  if (s.empty() || s.size() > 16) return false;
  std::uint64_t v = 0;
  for (char c : s) {
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | static_cast<std::uint64_t>(d);
  }
  out = v;
  return true;
}

/// Parses "(sec.usec)" into microseconds.
bool parse_timestamp(const std::string& tok, std::uint64_t& us) {
  if (tok.size() < 5 || tok.front() != '(' || tok.back() != ')') return false;
  const std::string body = tok.substr(1, tok.size() - 2);
  const auto dot = body.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= body.size()) return false;
  const std::string sec = body.substr(0, dot);
  std::string frac = body.substr(dot + 1);
  if (frac.size() > 6) frac.resize(6);
  while (frac.size() < 6) frac.push_back('0');
  std::uint64_t s = 0, f = 0;
  for (char c : sec) {
    if (!std::isdigit(static_cast<unsigned char>(c)) || s > 100'000'000'000ull) return false;
    s = s * 10 + static_cast<std::uint64_t>(c - '0');
  }
  for (char c : frac) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    f = f * 10 + static_cast<std::uint64_t>(c - '0');
  }
  us = s * 1'000'000ull + f;
  return true;
}

}  // namespace

std::string format_log_line(const CanFrame& f, const char* iface, std::uint64_t base_epoch_s) {
  char buf[96];
  const std::uint64_t sec = base_epoch_s + f.timestamp_us / 1'000'000;
  const std::uint64_t usec = f.timestamp_us % 1'000'000;
  int n = std::snprintf(buf, sizeof buf, "(%llu.%06llu) %s %08X#", static_cast<unsigned long long>(sec),
                        static_cast<unsigned long long>(usec), iface, static_cast<unsigned>(f.id & 0x1FFFFFFFu));
  std::string s(buf, static_cast<std::size_t>(n));
  static const char* hex = "0123456789ABCDEF";
  for (std::size_t i = 0; i < f.dlc && i < 8; ++i) {
    s.push_back(hex[f.data[i] >> 4]);
    s.push_back(hex[f.data[i] & 0xF]);
  }
  return s;
}

void write_log(std::ostream& os, const std::vector<CanFrame>& frames) {
  for (const auto& f : frames) os << format_log_line(f) << '\n';
}

bool write_log_file(const std::string& path, const std::vector<CanFrame>& frames) {
  std::ofstream os(path, std::ios::binary);
  if (!os) return false;
  write_log(os, frames);
  return static_cast<bool>(os);
}

LogReadResult read_log(std::istream& is) {
  LogReadResult r;
  std::string line;
  std::size_t line_no = 0;
  bool have_base = false;
  std::uint64_t base_us = 0;
  auto bad = [&] {
    if (r.bad_lines++ == 0) r.first_bad_line = line_no;
  };
  while (std::getline(is, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string ts_tok, iface, payload;
    if (!(ss >> ts_tok >> iface >> payload)) {
      bad();
      continue;
    }
    std::uint64_t ts_us = 0;
    if (!parse_timestamp(ts_tok, ts_us)) {
      bad();
      continue;
    }
    const auto hash = payload.find('#');
    if (hash == std::string::npos || hash == 0) {
      bad();
      continue;
    }
    std::uint64_t id = 0;
    if (!parse_hex(payload.substr(0, hash), id) || id > 0x1FFFFFFFull) {
      bad();
      continue;
    }
    const std::string data = payload.substr(hash + 1);
    if (data.size() % 2 != 0 || data.size() > 16) {
      bad();
      continue;
    }
    CanFrame f;
    f.id = static_cast<std::uint32_t>(id);
    f.dlc = static_cast<std::uint8_t>(data.size() / 2);
    bool ok = true;
    for (std::size_t i = 0; i < data.size() / 2 && ok; ++i) {
      std::uint64_t byte = 0;
      ok = parse_hex(data.substr(2 * i, 2), byte);
      f.data[i] = static_cast<std::uint8_t>(byte);
    }
    if (!ok) {
      bad();
      continue;
    }
    if (!have_base) {
      have_base = true;
      base_us = ts_us;
    }
    f.timestamp_us = ts_us >= base_us ? ts_us - base_us : 0;
    r.frames.push_back(f);
  }
  return r;
}

bool read_log_file(const std::string& path, LogReadResult& out) {
  std::ifstream is(path, std::ios::binary);
  if (!is) return false;
  out = read_log(is);
  return true;
}

void write_fault_csv(std::ostream& os, const std::vector<FaultNotification>& notes) {
  os << "id,kind,type,severity,key,signal,raised_us,cleared_us,value,message\n";
  for (const auto& n : notes) {
    const FaultEvent& e = n.event;
    os << e.id << ',' << (n.kind == FaultNotification::Kind::Raised ? "raised" : "cleared") << ',' << to_string(e.type)
       << ',' << to_string(e.severity) << ',' << e.key << ',' << (e.signal ? signal_def(*e.signal).name : "") << ','
       << e.raised_us << ',' << (e.active ? 0 : e.cleared_us) << ',' << std::fixed << std::setprecision(3) << e.value
       << ',' << '"' << e.message << '"' << '\n';
  }
}

}  // namespace enginesense
