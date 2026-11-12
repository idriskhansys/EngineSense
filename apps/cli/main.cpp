// enginesense_cli - headless front end: simulate, replay and decode J1939 logs.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "enginesense/fault_engine.hpp"
#include "enginesense/j1939.hpp"
#include "enginesense/log_io.hpp"
#include "enginesense/scenarios.hpp"
#include "enginesense/simulator.hpp"

using namespace enginesense;

namespace {

void usage() {
  std::puts(
      "EngineSense CLI\n"
      "  enginesense_cli list-scenarios\n"
      "  enginesense_cli simulate [--seed N] [--duration SEC] [--noise X] [--inject NAME@SEC]...\n"
      "                           [--out run.log] [--faults faults.csv]\n"
      "  enginesense_cli replay <log> [--faults faults.csv]\n"
      "  enginesense_cli decode <log> [--limit N]\n");
}

void print_notifications(const std::vector<FaultNotification>& notes) {
  for (const auto& n : notes) {
    const FaultEvent& e = n.event;
    const bool raised = n.kind == FaultNotification::Kind::Raised;
    std::printf("[%9.3f s] %-7s %-8s %-16s %s\n", static_cast<double>(raised ? e.raised_us : e.cleared_us) / 1e6,
                raised ? "RAISED" : "cleared", to_string(e.severity), to_string(e.type), e.message.c_str());
  }
}

int summarize(const std::vector<CanFrame>& frames, const std::vector<FaultNotification>& notes, const std::string& csv) {
  std::size_t raised = 0;
  for (const auto& n : notes)
    if (n.kind == FaultNotification::Kind::Raised) ++raised;
  std::printf("\n%zu frames, %zu faults raised\n", frames.size(), raised);
  if (!csv.empty()) {
    std::ofstream os(csv);
    if (!os) {
      std::fprintf(stderr, "cannot write %s\n", csv.c_str());
      return 1;
    }
    write_fault_csv(os, notes);
    std::printf("fault log written to %s\n", csv.c_str());
  }
  return 0;
}

int cmd_simulate(int argc, char** argv) {
  SimConfig sc;
  double duration_s = 60.0;
  std::string out_log, out_csv;
  std::vector<std::pair<const Scenario*, double>> injections;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--seed") sc.seed = static_cast<std::uint32_t>(std::strtoul(next(), nullptr, 10));
    else if (a == "--duration") duration_s = std::atof(next());
    else if (a == "--noise") sc.noise_scale = std::atof(next());
    else if (a == "--out") out_log = next();
    else if (a == "--faults") out_csv = next();
    else if (a == "--inject") {
      const std::string spec = next();
      const auto at = spec.find('@');
      const std::string name = spec.substr(0, at);
      const double when = at == std::string::npos ? 10.0 : std::atof(spec.c_str() + at + 1);
      const Scenario* s = find_scenario(name);
      if (!s) {
        std::fprintf(stderr, "unknown scenario '%s' (see list-scenarios)\n", name.c_str());
        return 2;
      }
      injections.emplace_back(s, when);
    } else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 2;
    }
  }
  Simulator sim(sc);
  for (const auto& [s, when] : injections) {
    Injection inj = s->injection;
    inj.start_us = static_cast<std::uint64_t>(when * 1e6);
    sim.add_injection(inj);
    std::printf("injecting %-26s at t = %.1f s : %s\n", s->name.c_str(), when, s->description.c_str());
  }
  const auto frames = sim.generate(static_cast<std::uint64_t>(duration_s * 1e6));
  if (!out_log.empty() && !write_log_file(out_log, frames)) {
    std::fprintf(stderr, "cannot write %s\n", out_log.c_str());
    return 1;
  }
  const auto notes = replay_frames(frames);
  print_notifications(notes);
  return summarize(frames, notes, out_csv);
}

int cmd_replay(int argc, char** argv) {
  if (argc < 3) return usage(), 2;
  std::string csv;
  for (int i = 3; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--faults") == 0) csv = argv[i + 1];
  LogReadResult log;
  if (!read_log_file(argv[2], log)) {
    std::fprintf(stderr, "cannot read %s\n", argv[2]);
    return 1;
  }
  if (log.bad_lines) std::fprintf(stderr, "warning: %zu malformed lines (first at line %zu)\n", log.bad_lines, log.first_bad_line);
  const auto notes = replay_frames(log.frames);
  print_notifications(notes);
  return summarize(log.frames, notes, csv);
}

int cmd_decode(int argc, char** argv) {
  if (argc < 3) return usage(), 2;
  std::size_t limit = 40;
  for (int i = 3; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--limit") == 0) limit = static_cast<std::size_t>(std::strtoul(argv[i + 1], nullptr, 10));
  LogReadResult log;
  if (!read_log_file(argv[2], log)) {
    std::fprintf(stderr, "cannot read %s\n", argv[2]);
    return 1;
  }
  std::size_t shown = 0;
  for (const auto& f : log.frames) {
    const J1939Id id = parse_id(f.id);
    const auto dec = decode_frame(f);
    if (dec.count == 0 && id.pgn != pgn::kDm1) continue;
    std::printf("%9.3f s  PGN 0x%05X %-6s SA %02X ", static_cast<double>(f.timestamp_us) / 1e6, id.pgn, pgn_name(id.pgn), id.source);
    for (std::size_t i = 0; i < dec.count; ++i) {
      const SignalDef& d = signal_def(dec.items[i].id);
      if (dec.items[i].status == SignalStatus::Valid) std::printf(" %s=%.2f%s", d.name, dec.items[i].value, d.unit);
      else std::printf(" %s=<%s>", d.name, to_string(dec.items[i].status));
    }
    if (const auto dm1 = parse_dm1(f)) {
      if (dm1->dtc) std::printf(" DTC SPN %u FMI %u OC %u", dm1->dtc->spn, dm1->dtc->fmi, dm1->dtc->occurrence);
      else std::printf(" no active DTC");
    }
    std::printf("\n");
    if (++shown >= limit) break;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage(), 2;
  const std::string cmd = argv[1];
  if (cmd == "list-scenarios") {
    for (const auto& s : standard_scenarios())
      std::printf("%-28s %-12s budget %6.0f ms  %s\n", s.name.c_str(), s.requirement.c_str(),
                  static_cast<double>(s.max_latency_us) / 1000.0, s.description.c_str());
    return 0;
  }
  if (cmd == "simulate") return cmd_simulate(argc, argv);
  if (cmd == "replay") return cmd_replay(argc, argv);
  if (cmd == "decode") return cmd_decode(argc, argv);
  usage();
  return cmd == "--help" || cmd == "help" ? 0 : 2;
}
