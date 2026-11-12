// enginesense_bench - Monte-Carlo verification & tuning experiments. Writes CSV for tools/analyze.py.
//   latency    --trials N --out DIR     detection latency / detection rate per scenario
//   nominal    --runs N --minutes M     false-alarm rate on fault-free runs
//   sweep      --runs N                 coolant threshold x noise sweep (false alarms) and
//                                       confirm-samples sweep (latency)
//   throughput                          frames per second of the detection core
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "enginesense/scenarios.hpp"

using namespace enginesense;

namespace {

std::string arg(int argc, char** argv, const char* name, const char* def) {
  for (int i = 2; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  return def;
}

int cmd_latency(int argc, char** argv) {
  const int trials = std::atoi(arg(argc, argv, "--trials", "200").c_str());
  const std::string dir = arg(argc, argv, "--out", ".");
  std::ofstream os(dir + "/latency.csv");
  if (!os) {
    std::fprintf(stderr, "cannot write into %s\n", dir.c_str());
    return 1;
  }
  os << "scenario,requirement,seed,detected,latency_ms,budget_ms,false_alarms,unexpected\n";
  for (const auto& sc : standard_scenarios()) {
    int det = 0, bad = 0;
    for (int seed = 1; seed <= trials; ++seed) {
      TrialConfig cfg;
      cfg.seed = static_cast<std::uint32_t>(seed);
      const TrialResult r = run_trial(sc, cfg);
      os << sc.name << ',' << sc.requirement << ',' << seed << ',' << (r.detected ? 1 : 0) << ','
         << static_cast<double>(r.latency_us) / 1000.0 << ',' << static_cast<double>(sc.max_latency_us) / 1000.0 << ','
         << r.false_alarms << ',' << r.unexpected << '\n';
      det += r.detected;
      bad += (!r.detected || r.latency_us > sc.max_latency_us || r.false_alarms || r.unexpected);
    }
    std::printf("%-28s detected %3d/%d  out-of-spec trials: %d\n", sc.name.c_str(), det, trials, bad);
  }
  return 0;
}

int cmd_nominal(int argc, char** argv) {
  const int runs = std::atoi(arg(argc, argv, "--runs", "100").c_str());
  const double minutes = std::atof(arg(argc, argv, "--minutes", "10").c_str());
  const std::string dir = arg(argc, argv, "--out", ".");
  std::ofstream os(dir + "/nominal.csv");
  os << "seed,minutes,false_alarms,frames\n";
  std::size_t total = 0;
  for (int seed = 1; seed <= runs; ++seed) {
    const NominalResult r = run_nominal(static_cast<std::uint32_t>(seed), static_cast<std::uint64_t>(minutes * 60e6));
    os << seed << ',' << minutes << ',' << r.false_alarms << ',' << r.frames << '\n';
    total += r.false_alarms;
  }
  std::printf("nominal: %d runs x %.1f min = %.0f simulated minutes, %zu false alarms\n", runs, minutes, runs * minutes, total);
  return total == 0 ? 0 : 1;
}

int cmd_sweep(int argc, char** argv) {
  const int runs = std::atoi(arg(argc, argv, "--runs", "20").c_str());
  const std::string dir = arg(argc, argv, "--out", ".");
  {  // (a) false alarms vs coolant threshold for increasing sensor noise. Over-temperature alarms are
     // counted separately from every other rule so the threshold's own behaviour is visible.
    std::ofstream os(dir + "/sweep_threshold.csv");
    os << "noise_scale,threshold_c,runs,minutes_per_run,overtemp_false_alarms,other_false_alarms\n";
    const double noise[] = {1, 4, 8, 12};
    for (double ns : noise) {
      for (int thr_i = 90; thr_i <= 106; thr_i += 2) {
        const double thr = thr_i;
        std::size_t over = 0, other = 0;
        for (int seed = 1; seed <= runs; ++seed) {
          RuleConfig rc;
          rc.coolant_over_c = thr;
          const NominalResult r = run_nominal(static_cast<std::uint32_t>(seed), 10ull * 60'000'000, rc, ns);
          over += r.by_type[static_cast<std::size_t>(FaultType::OverTemperature)];
          other += r.false_alarms - r.by_type[static_cast<std::size_t>(FaultType::OverTemperature)];
        }
        os << ns << ',' << thr << ',' << runs << ",10," << over << ',' << other << '\n';
      }
      std::printf("threshold sweep noise x%.0f done\n", ns);
    }
  }
  {  // (b) detection latency vs confirmation sample count (coolant over-temperature)
    std::ofstream os(dir + "/sweep_confirm.csv");
    os << "confirm_samples,seed,detected,latency_ms\n";
    const Scenario* sc = find_scenario("coolant_overtemp");
    for (unsigned n = 1; n <= 5; ++n) {
      for (int seed = 1; seed <= 100; ++seed) {
        TrialConfig cfg;
        cfg.seed = static_cast<std::uint32_t>(seed);
        cfg.rules.confirm_samples_temp = n;
        const TrialResult r = run_trial(*sc, cfg);
        os << n << ',' << seed << ',' << (r.detected ? 1 : 0) << ',' << static_cast<double>(r.latency_us) / 1000.0 << '\n';
      }
    }
  }
  {  // (c) single-sample glitch immunity vs confirmation count: +8 degC spike lasting one sample at noise x1
    std::ofstream os(dir + "/sweep_glitch.csv");
    os << "confirm_samples,trials,false_overtemp\n";
    for (unsigned n = 1; n <= 4; ++n) {
      int fa = 0;
      const int trials = 200;
      for (int seed = 1; seed <= trials; ++seed) {
        Scenario sc = *find_scenario("coolant_overtemp");
        sc.injection.magnitude = 25.0;           // lands above the 105 degC limit...
        sc.injection.duration_us = 900'000;      // ...for < 1 sample period (a single reading)
        TrialConfig cfg;
        cfg.seed = static_cast<std::uint32_t>(seed);
        cfg.rules.confirm_samples_temp = n;
        const TrialResult r = run_trial(sc, cfg);
        fa += r.detected ? 1 : 0;
      }
      os << n << ',' << trials << ',' << fa << '\n';
    }
  }
  std::puts("sweeps written");
  return 0;
}

int cmd_throughput(int argc, char** argv) {
  (void)argc;
  (void)argv;
  SimConfig sc;
  Simulator sim(sc);
  const auto frames = sim.generate(600ull * 1'000'000);  // 10 simulated minutes
  FaultEngine engine;
  std::vector<FaultNotification> notes;
  const auto t0 = std::chrono::steady_clock::now();
  std::size_t total = 0;
  for (int rep = 0; rep < 5; ++rep) {
    engine.reset();
    for (const auto& f : frames) engine.process(f, notes);
    total += frames.size();
  }
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("throughput: %zu frames in %.3f s = %.0f frames/s\n", total, s, static_cast<double>(total) / s);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::puts("usage: enginesense_bench latency|nominal|sweep|throughput [--trials N] [--runs N] [--minutes M] [--out DIR]");
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "latency") return cmd_latency(argc, argv);
  if (cmd == "nominal") return cmd_nominal(argc, argv);
  if (cmd == "sweep") return cmd_sweep(argc, argv);
  if (cmd == "throughput") return cmd_throughput(argc, argv);
  std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
  return 2;
}
