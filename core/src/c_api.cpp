#include "enginesense/c_api.h"

#include <algorithm>
#include <cstring>
#include <deque>

#include "enginesense/log_io.hpp"
#include "enginesense/monitor.hpp"

using namespace enginesense;

struct es_monitor {
  Monitor monitor;
  std::deque<FaultNotification> events;
};

namespace {
void fill(es_fault& o, const FaultEvent& e) {
  o = es_fault{};
  o.id = e.id;
  o.type = static_cast<int32_t>(e.type);
  o.severity = static_cast<int32_t>(e.severity);
  o.active = e.active ? 1 : 0;
  o.raised_us = e.raised_us;
  o.cleared_us = e.active ? 0 : e.cleared_us;
  o.value = e.value;
  std::strncpy(o.type_name, to_string(e.type), sizeof o.type_name - 1);
  std::strncpy(o.message, e.message.c_str(), sizeof o.message - 1);
}
}  // namespace

extern "C" {

const char* es_version(void) { return "1.0.0"; }

es_monitor* es_create(void) {
  try {
    return new es_monitor();
  } catch (...) {
    return nullptr;
  }
}

void es_destroy(es_monitor* m) { delete m; }

int es_start_simulation(es_monitor* m, uint32_t seed, double speed) {
  if (!m) return -1;
  try {
    SimConfig sc;
    sc.seed = seed;
    Monitor::Options o;
    o.speed = speed;
    m->events.clear();
    return m->monitor.start_simulation(sc, o) ? 0 : -1;
  } catch (...) {
    return -1;
  }
}

int es_start_replay(es_monitor* m, const char* path, double speed) {
  if (!m || !path) return -1;
  try {
    LogReadResult r;
    if (!read_log_file(path, r)) return -1;
    Monitor::Options o;
    o.speed = speed;
    m->events.clear();
    return m->monitor.start_replay(std::move(r.frames), o) ? 0 : -1;
  } catch (...) {
    return -1;
  }
}

void es_stop(es_monitor* m) {
  if (m) m->monitor.stop();
}

int es_inject_scenario(es_monitor* m, int32_t index) {
  const auto& all = standard_scenarios();
  if (!m || index < 0 || static_cast<std::size_t>(index) >= all.size()) return -1;
  m->monitor.inject_scenario(all[static_cast<std::size_t>(index)]);
  return 0;
}

void es_get_snapshot(es_monitor* m, es_snapshot* out) {
  if (!out) return;
  *out = es_snapshot{};
  if (!m) return;
  const MonitorSnapshot s = m->monitor.snapshot();
  out->time_us = s.time_us;
  out->bus_load = s.bus_load;
  out->frames_processed = s.frames_processed;
  out->frames_dropped = s.frames_dropped;
  out->running = s.running ? 1 : 0;
  out->active_fault_count = static_cast<int32_t>(s.active_faults.size());
  for (std::size_t i = 0; i < kSignalCount; ++i) {
    out->signals[i].value = s.signals[i].value;
    out->signals[i].status = static_cast<int32_t>(s.signals[i].status);
  }
}

int32_t es_get_active_faults(es_monitor* m, es_fault* out, int32_t capacity) {
  if (!m || !out || capacity <= 0) return 0;
  const MonitorSnapshot s = m->monitor.snapshot();
  const auto n = std::min<std::size_t>(s.active_faults.size(), static_cast<std::size_t>(capacity));
  for (std::size_t i = 0; i < n; ++i) fill(out[i], s.active_faults[i]);
  return static_cast<int32_t>(n);
}

int32_t es_poll_event(es_monitor* m, es_fault* out, int32_t* cleared) {
  if (!m || !out) return 0;
  if (m->events.empty()) {
    for (auto& n : m->monitor.drain_notifications()) m->events.push_back(std::move(n));
  }
  if (m->events.empty()) return 0;
  const FaultNotification n = std::move(m->events.front());
  m->events.pop_front();
  fill(*out, n.event);
  if (cleared) *cleared = n.kind == FaultNotification::Kind::Cleared ? 1 : 0;
  return 1;
}

int32_t es_get_history(es_monitor* m, int32_t signal, double* t_s, double* values, int32_t max_points) {
  if (!m || !t_s || !values || max_points <= 0 || signal < 0 || signal >= static_cast<int32_t>(kSignalCount)) return 0;
  const auto h = m->monitor.history(static_cast<SignalId>(signal), static_cast<std::size_t>(max_points));
  for (std::size_t i = 0; i < h.size(); ++i) {
    t_s[i] = h[i].t_s;
    values[i] = h[i].value;
  }
  return static_cast<int32_t>(h.size());
}

const char* es_signal_name(int32_t signal) {
  if (signal < 0 || signal >= static_cast<int32_t>(kSignalCount)) return "";
  return signal_def(static_cast<SignalId>(signal)).name;
}
const char* es_signal_unit(int32_t signal) {
  if (signal < 0 || signal >= static_cast<int32_t>(kSignalCount)) return "";
  return signal_def(static_cast<SignalId>(signal)).unit;
}
int32_t es_scenario_count(void) { return static_cast<int32_t>(standard_scenarios().size()); }
const char* es_scenario_name(int32_t i) {
  const auto& a = standard_scenarios();
  return (i < 0 || static_cast<std::size_t>(i) >= a.size()) ? "" : a[static_cast<std::size_t>(i)].name.c_str();
}
const char* es_scenario_description(int32_t i) {
  const auto& a = standard_scenarios();
  return (i < 0 || static_cast<std::size_t>(i) >= a.size()) ? "" : a[static_cast<std::size_t>(i)].description.c_str();
}

}  // extern "C"
