#include "enginesense/fault_engine.hpp"

#include <algorithm>
#include <sstream>

#include "enginesense/j1939.hpp"

namespace enginesense {
namespace {
/// Cap on catch-up evaluations for a single gap (60 s at the default 10 ms grid); longer gaps
/// are evaluated once at the end so replaying a log with a huge hole stays O(1).
constexpr std::uint64_t kMaxCatchUpTicks = 6000;
}  // namespace

FaultEngine::FaultEngine(RuleConfig cfg) : cfg_(cfg), bus_(cfg.bitrate_bps), bus_fast_(cfg.bitrate_bps, 2'500) {
  rules_ = make_default_rules(cfg_);
  states_.resize(rules_.size());
  results_.resize(rules_.size());
}

void FaultEngine::add_rule(std::unique_ptr<Rule> rule) {
  rules_.push_back(std::move(rule));
  states_.emplace_back();
  results_.emplace_back();
}

void FaultEngine::reset() {
  for (auto& r : rules_) r->reset();
  for (auto& s : states_) s = RuleState{};
  signals_ = SignalTable{};
  pgn_last_seen_.clear();
  bus_.reset();
  bus_fast_.reset();
  dtcs_.clear();
  started_ = false;
  start_us_ = now_us_ = last_eval_us_ = 0;
  next_id_ = 1;
  frames_ = 0;
}

std::vector<FaultNotification> FaultEngine::process(const CanFrame& frame) {
  std::vector<FaultNotification> out;
  process(frame, out);
  return out;
}

std::vector<FaultNotification> FaultEngine::tick(std::uint64_t now_us) {
  std::vector<FaultNotification> out;
  tick(now_us, out);
  return out;
}

void FaultEngine::advance_to(std::uint64_t ts, std::vector<FaultNotification>& out) {
  if (!started_) {
    started_ = true;
    start_us_ = last_eval_us_ = now_us_ = ts;
    return;
  }
  if (ts <= last_eval_us_) return;
  const std::uint64_t tick = cfg_.tick_us;
  if (tick > 0) {
    if ((ts - last_eval_us_) / tick > kMaxCatchUpTicks) last_eval_us_ = ts - kMaxCatchUpTicks * tick;
    while (last_eval_us_ + tick < ts) {
      last_eval_us_ += tick;
      evaluate(last_eval_us_, out);
    }
  }
}

void FaultEngine::tick(std::uint64_t now_us, std::vector<FaultNotification>& out) {
  const std::uint64_t ts = std::max(now_us, now_us_);
  advance_to(ts, out);
  if (ts >= last_eval_us_) {
    last_eval_us_ = ts;
    now_us_ = ts;
    evaluate(ts, out);
  }
}

void FaultEngine::process(const CanFrame& frame, std::vector<FaultNotification>& out) {
  const std::uint64_t ts = std::max(frame.timestamp_us, now_us_);  // tolerate out-of-order stamps
  advance_to(ts, out);
  now_us_ = ts;
  last_eval_us_ = std::max(last_eval_us_, ts);
  ++frames_;
  ingest(frame, ts, out);
  evaluate(ts, out);
}

void FaultEngine::ingest(const CanFrame& frame, std::uint64_t ts, std::vector<FaultNotification>& out) {
  bus_.add(ts, estimated_frame_bits(frame));
  bus_fast_.add(ts, estimated_frame_bits(frame));
  const std::uint32_t p = parse_id(frame.id).pgn;

  const DecodeResult dec = decode_frame(frame);
  if (dec.count > 0) pgn_last_seen_[p] = ts;
  for (std::size_t i = 0; i < dec.count; ++i) {
    SignalValue& s = signals_[index_of(dec.items[i].id)];
    s.value = dec.items[i].value;
    s.status = dec.items[i].status;
    s.last_update_us = ts;
    ++s.update_count;
  }
  if (p == pgn::kDm1) {
    if (const auto dm1 = parse_dm1(frame)) {
      pgn_last_seen_[p] = ts;
      handle_dm1(*dm1, ts, out);
    }
  }
}

void FaultEngine::handle_dm1(const Dm1& dm1, std::uint64_t ts, std::vector<FaultNotification>& out) {
  if (!dm1.dtc) {  // ECU says "no active faults": everything it reported earlier is cleared
    for (auto& kv : dtcs_) {
      kv.second.event.active = false;
      kv.second.event.cleared_us = ts;
      out.push_back({FaultNotification::Kind::Cleared, kv.second.event});
    }
    dtcs_.clear();
    return;
  }
  const Dtc& d = *dm1.dtc;
  const auto key = std::make_pair(d.spn, d.fmi);
  auto it = dtcs_.find(key);
  if (it != dtcs_.end()) {
    it->second.last_seen = ts;
    return;
  }
  DtcState st;
  st.last_seen = ts;
  FaultEvent& e = st.event;
  e.id = next_id_++;
  e.type = FaultType::EcuDtc;
  e.severity = dm1.lamps.red_stop == 1 ? Severity::Critical
               : (dm1.lamps.amber_warning == 1 || dm1.lamps.malfunction == 1) ? Severity::Warning
                                                                                : Severity::Info;
  e.key = "dtc." + std::to_string(d.spn) + "." + std::to_string(d.fmi);
  e.signal = signal_from_spn(d.spn);
  std::ostringstream os;
  os << "ECU DTC SPN " << d.spn << " FMI " << static_cast<int>(d.fmi) << ": " << describe_fmi(d.fmi);
  e.message = os.str();
  e.raised_us = ts;
  e.active = true;
  out.push_back({FaultNotification::Kind::Raised, e});
  dtcs_.emplace(key, std::move(st));
}

void FaultEngine::evaluate(std::uint64_t now, std::vector<FaultNotification>& out) {
  const EvalContext ctx{now, start_us_, signals_, pgn_last_seen_, bus_.load(now), bus_fast_.load(now)};
  const std::size_t n = rules_.size();
  std::array<unsigned, kFaultTypeCount> present{};
  for (std::size_t i = 0; i < n; ++i) {
    results_[i] = rules_[i]->evaluate(ctx);
    if (results_[i].violated || states_[i].active) ++present[static_cast<std::size_t>(rules_[i]->spec().type)];
  }

  for (std::size_t i = 0; i < n; ++i) {
    const RuleSpec& spec = rules_[i]->spec();
    RuleState& st = states_[i];
    const RuleResult& r = results_[i];
    if (r.violated) {
      st.clearing = false;
      if (st.active) {
        st.event.value = r.value;
        continue;
      }
      if (!st.pending) {
        st.pending = true;
        st.pending_since = now;
      }
      const bool suppressed =
          spec.suppressed_by && present[static_cast<std::size_t>(*spec.suppressed_by)] > 0;
      if (!suppressed && now - st.pending_since >= spec.raise_after_us) {
        st.active = true;
        st.pending = false;
        FaultEvent& e = st.event;
        e = FaultEvent{};
        e.id = next_id_++;
        e.type = spec.type;
        e.severity = spec.severity;
        e.key = spec.key;
        e.signal = spec.signal;
        e.raised_us = now;
        e.active = true;
        e.value = r.value;
        e.message = rules_[i]->describe(r.value);
        out.push_back({FaultNotification::Kind::Raised, e});
      }
    } else {
      st.pending = false;
      if (!st.active) continue;
      if (!st.clearing) {
        st.clearing = true;
        st.clear_since = now;
      }
      if (now - st.clear_since >= spec.clear_after_us) {
        st.active = false;
        st.clearing = false;
        st.event.active = false;
        st.event.cleared_us = now;
        out.push_back({FaultNotification::Kind::Cleared, st.event});
      }
    }
  }

  for (auto it = dtcs_.begin(); it != dtcs_.end();) {  // DM1 refresh timeout
    if (now - it->second.last_seen > cfg_.dtc_timeout_us) {
      it->second.event.active = false;
      it->second.event.cleared_us = now;
      out.push_back({FaultNotification::Kind::Cleared, it->second.event});
      it = dtcs_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<FaultEvent> FaultEngine::active_faults() const {
  std::vector<FaultEvent> v;
  for (const auto& st : states_)
    if (st.active) v.push_back(st.event);
  for (const auto& kv : dtcs_) v.push_back(kv.second.event);
  std::sort(v.begin(), v.end(), [](const FaultEvent& a, const FaultEvent& b) { return a.id < b.id; });
  return v;
}

}  // namespace enginesense
