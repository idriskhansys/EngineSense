#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "enginesense/bus_load.hpp"
#include "enginesense/can_frame.hpp"
#include "enginesense/dtc.hpp"
#include "enginesense/fault.hpp"
#include "enginesense/rules.hpp"
#include "enginesense/signals.hpp"

namespace enginesense {

/// Deterministic, single-threaded fault detection core.
///
/// The engine is driven only by frame timestamps (and optional tick() calls) - it never reads a
/// wall clock - so a recorded log replays to byte-identical results. Between frames it evaluates
/// the time-based rules on a fixed grid (RuleConfig::tick_us), which gives dropout detection a
/// latency resolution of one tick even when the bus is completely silent.
class FaultEngine {
 public:
  explicit FaultEngine(RuleConfig cfg = RuleConfig{});

  /// Feed one frame; appends raised/cleared notifications to `out`.
  void process(const CanFrame& frame, std::vector<FaultNotification>& out);
  /// Advance time without a frame (e.g. from a wall-clock timer or the end of a log).
  void tick(std::uint64_t now_us, std::vector<FaultNotification>& out);

  /// Convenience overloads returning a fresh vector.
  std::vector<FaultNotification> process(const CanFrame& frame);
  std::vector<FaultNotification> tick(std::uint64_t now_us);

  /// Extend the rule set (rules are evaluated in insertion order).
  void add_rule(std::unique_ptr<Rule> rule);
  void reset();

  const SignalTable& signals() const noexcept { return signals_; }
  double bus_load() const noexcept { return bus_.load(now_us_); }
  std::uint64_t now_us() const noexcept { return now_us_; }
  std::uint64_t frames_processed() const noexcept { return frames_; }
  std::vector<FaultEvent> active_faults() const;
  const RuleConfig& config() const noexcept { return cfg_; }

 private:
  struct RuleState {
    bool pending = false;
    std::uint64_t pending_since = 0;
    bool active = false;
    bool clearing = false;
    std::uint64_t clear_since = 0;
    FaultEvent event;
  };
  struct DtcState {
    FaultEvent event;
    std::uint64_t last_seen = 0;
  };

  void advance_to(std::uint64_t ts, std::vector<FaultNotification>& out);
  void ingest(const CanFrame& frame, std::uint64_t ts, std::vector<FaultNotification>& out);
  void handle_dm1(const Dm1& dm1, std::uint64_t ts, std::vector<FaultNotification>& out);
  void evaluate(std::uint64_t now, std::vector<FaultNotification>& out);

  RuleConfig cfg_;
  std::vector<std::unique_ptr<Rule>> rules_;
  std::vector<RuleState> states_;
  std::vector<RuleResult> results_;  // scratch, avoids per-evaluation allocation
  SignalTable signals_{};
  std::unordered_map<std::uint32_t, std::uint64_t> pgn_last_seen_;
  BusLoadMeter bus_;
  BusLoadMeter bus_fast_;
  std::map<std::pair<std::uint32_t, std::uint8_t>, DtcState> dtcs_;
  bool started_ = false;
  std::uint64_t start_us_ = 0;
  std::uint64_t now_us_ = 0;
  std::uint64_t last_eval_us_ = 0;
  std::uint64_t next_id_ = 1;
  std::uint64_t frames_ = 0;
};

}  // namespace enginesense
