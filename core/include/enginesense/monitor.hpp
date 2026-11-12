#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "enginesense/bounded_queue.hpp"
#include "enginesense/fault_engine.hpp"
#include "enginesense/scenarios.hpp"
#include "enginesense/simulator.hpp"

namespace enginesense {

struct MonitorSnapshot {
  std::uint64_t time_us = 0;
  SignalTable signals{};
  double bus_load = 0.0;
  std::uint64_t frames_processed = 0;
  std::uint64_t frames_dropped = 0;
  std::size_t queue_depth = 0;
  bool running = false;
  std::vector<FaultEvent> active_faults;
};

struct SamplePoint {
  double t_s;
  double value;
};

/// Live monitoring pipeline: producer thread (simulator or log replay) -> bounded queue ->
/// consumer thread running the FaultEngine -> mutex-protected snapshot for the UI thread.
///
/// UI contract: snapshot(), history() and drain_notifications() hold the internal lock only for
/// a small copy, never while the engine is processing, so a UI thread polling at 30-60 Hz is
/// never blocked behind detection work (REQ-PERF-002).
class Monitor {
 public:
  struct Options {
    double speed = 1.0;                 ///< 1.0 = real time, 10 = 10x, 0 = as fast as possible
    std::size_t queue_capacity = 65'536;
    std::uint64_t duration_us = 0;      ///< simulation only: stop after this much sim time (0 = endless)
    std::uint64_t flush_us = 0;         ///< replay only: extra time ticked after the last frame
    RuleConfig rules{};
    /// Test hook: artificial per-item consumer delay to emulate an overloaded detection thread.
    std::uint64_t debug_consumer_delay_us = 0;
  };

  Monitor();
  ~Monitor();
  Monitor(const Monitor&) = delete;
  Monitor& operator=(const Monitor&) = delete;

  bool start_simulation(const SimConfig& sim, const Options& opt);
  bool start_replay(std::vector<CanFrame> frames, const Options& opt);
  void stop();

  /// True while the pipeline threads are active (becomes false after a finite run completes).
  bool running() const;
  /// Block until a finite run has completed (or timeout). Returns true if finished.
  bool wait_finished(std::chrono::milliseconds timeout);

  /// Inject a fault into a running simulation; start_us is interpreted relative to "now".
  void inject(Injection inj);
  void inject_scenario(const Scenario& s);

  MonitorSnapshot snapshot() const;
  std::vector<SamplePoint> history(SignalId id, std::size_t max_points = 600) const;
  std::vector<FaultNotification> drain_notifications();
  std::vector<FaultNotification> all_notifications() const;

 private:
  struct Item {
    bool tick_only = false;
    std::uint64_t tick_us = 0;
    CanFrame frame;
  };

  bool begin(const Options& opt);
  void producer_sim(const SimConfig& sim, const Options& opt);
  void producer_replay(const std::vector<CanFrame>& frames, const Options& opt);
  void consumer(const Options& opt);
  void publish(FaultEngine& engine, std::vector<FaultNotification>& notes, std::uint64_t processed);

  std::unique_ptr<BoundedQueue<Item>> queue_;
  std::thread producer_thread_, consumer_thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<double> speed_{1.0};

  mutable std::mutex state_mutex_;  // guards snapshot_, history_, pending_, all_
  MonitorSnapshot snapshot_;
  std::vector<std::deque<SamplePoint>> history_;
  std::vector<std::uint64_t> history_last_us_;
  std::vector<std::uint64_t> history_last_count_;
  std::vector<FaultNotification> pending_;
  std::vector<FaultNotification> all_;

  std::mutex inject_mutex_;
  std::vector<Injection> inject_queue_;

  mutable std::mutex finish_mutex_;
  std::condition_variable finish_cv_;
  bool finished_ = true;
};

}  // namespace enginesense
