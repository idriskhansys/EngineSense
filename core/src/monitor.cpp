#include "enginesense/monitor.hpp"

#include <algorithm>

namespace enginesense {
namespace {
constexpr std::size_t kHistoryCap = 6000;
constexpr std::uint64_t kHistoryMinSpacingUs = 100'000;
constexpr std::uint64_t kSimChunkUs = 10'000;
}  // namespace

Monitor::Monitor() : history_(kSignalCount), history_last_us_(kSignalCount, 0), history_last_count_(kSignalCount, 0) {}

Monitor::~Monitor() { stop(); }

bool Monitor::begin(const Options& opt) {
  stop();
  stop_ = false;
  dropped_ = 0;
  speed_ = opt.speed;
  queue_ = std::make_unique<BoundedQueue<Item>>(std::max<std::size_t>(opt.queue_capacity, 16));
  {
    std::lock_guard<std::mutex> lk(state_mutex_);
    snapshot_ = MonitorSnapshot{};
    snapshot_.running = true;
    for (auto& h : history_) h.clear();
    std::fill(history_last_us_.begin(), history_last_us_.end(), 0);
    std::fill(history_last_count_.begin(), history_last_count_.end(), 0);
    pending_.clear();
    all_.clear();
  }
  {
    std::lock_guard<std::mutex> lk(inject_mutex_);
    inject_queue_.clear();
  }
  {
    std::lock_guard<std::mutex> lk(finish_mutex_);
    finished_ = false;
  }
  running_ = true;
  return true;
}

bool Monitor::start_simulation(const SimConfig& sim, const Options& opt) {
  begin(opt);
  consumer_thread_ = std::thread([this, opt] { consumer(opt); });
  producer_thread_ = std::thread([this, sim, opt] { producer_sim(sim, opt); });
  return true;
}

bool Monitor::start_replay(std::vector<CanFrame> frames, const Options& opt) {
  begin(opt);
  consumer_thread_ = std::thread([this, opt] { consumer(opt); });
  producer_thread_ = std::thread([this, f = std::move(frames), opt]() mutable { producer_replay(f, opt); });
  return true;
}

void Monitor::stop() {
  stop_ = true;
  if (queue_) queue_->close();
  if (producer_thread_.joinable()) producer_thread_.join();
  if (consumer_thread_.joinable()) consumer_thread_.join();
  running_ = false;
}

bool Monitor::running() const { return running_.load(); }

bool Monitor::wait_finished(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lk(finish_mutex_);
  return finish_cv_.wait_for(lk, timeout, [&] { return finished_; });
}

void Monitor::inject(Injection inj) {
  std::lock_guard<std::mutex> lk(inject_mutex_);
  inject_queue_.push_back(inj);
}

void Monitor::inject_scenario(const Scenario& s) { inject(s.injection); }

void Monitor::producer_sim(const SimConfig& cfg, const Options& opt) {
  Simulator sim(cfg);
  std::vector<CanFrame> frames;
  const auto wall0 = std::chrono::steady_clock::now();
  const bool realtime = opt.speed > 0.0;
  std::uint64_t t = 0;
  while (!stop_ && (opt.duration_us == 0 || t < opt.duration_us)) {
    {
      std::lock_guard<std::mutex> lk(inject_mutex_);
      for (auto inj : inject_queue_) {
        inj.start_us += sim.time_us();  // relative to "now"
        sim.add_injection(inj);
      }
      inject_queue_.clear();
    }
    t += kSimChunkUs;
    frames.clear();
    sim.advance(t, frames);
    if (realtime)
      std::this_thread::sleep_until(wall0 + std::chrono::microseconds(static_cast<std::int64_t>(static_cast<double>(t) / opt.speed)));
    for (const auto& f : frames) {
      Item it;
      it.frame = f;
      if (realtime) {
        if (!queue_->try_push(it)) ++dropped_;
      } else if (!queue_->push(it)) {
        break;
      }
    }
    Item tick;
    tick.tick_only = true;
    tick.tick_us = t;
    if (realtime) queue_->try_push(tick);
    else queue_->push(tick);
  }
  queue_->close();
}

void Monitor::producer_replay(const std::vector<CanFrame>& frames, const Options& opt) {
  const auto wall0 = std::chrono::steady_clock::now();
  const bool realtime = opt.speed > 0.0;
  std::uint64_t last = 0;
  for (const auto& f : frames) {
    if (stop_) break;
    if (realtime)
      std::this_thread::sleep_until(wall0 + std::chrono::microseconds(static_cast<std::int64_t>(static_cast<double>(f.timestamp_us) / opt.speed)));
    Item it;
    it.frame = f;
    last = std::max(last, f.timestamp_us);
    if (realtime) {
      if (!queue_->try_push(it)) ++dropped_;
    } else if (!queue_->push(it)) {
      break;
    }
  }
  if (!stop_ && !frames.empty() && opt.flush_us > 0) {  // flush so pending debounce/clear timers can complete
    for (std::uint64_t t = last + kSimChunkUs; t <= last + opt.flush_us; t += 100 * kSimChunkUs) {
      Item tick;
      tick.tick_only = true;
      tick.tick_us = t;
      if (!queue_->push(tick)) break;
    }
  }
  queue_->close();
}

void Monitor::consumer(const Options& opt) {
  FaultEngine engine(opt.rules);
  std::vector<FaultNotification> notes;
  std::uint64_t processed = 0;
  auto last_publish = std::chrono::steady_clock::now();
  Item item;
  while (queue_->pop(item)) {
    if (item.tick_only) {
      engine.tick(item.tick_us, notes);
    } else {
      engine.process(item.frame, notes);
      ++processed;
    }
    if (opt.debug_consumer_delay_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(opt.debug_consumer_delay_us));
    const auto now = std::chrono::steady_clock::now();
    if (!notes.empty() || now - last_publish >= std::chrono::milliseconds(5)) {
      publish(engine, notes, processed);
      last_publish = now;
    }
  }
  publish(engine, notes, processed);
  {
    std::lock_guard<std::mutex> lk(state_mutex_);
    snapshot_.running = false;
  }
  running_ = false;
  {
    std::lock_guard<std::mutex> lk(finish_mutex_);
    finished_ = true;
  }
  finish_cv_.notify_all();
}

void Monitor::publish(FaultEngine& engine, std::vector<FaultNotification>& notes, std::uint64_t processed) {
  // Gather everything that needs the engine *before* taking the lock.
  const SignalTable sigs = engine.signals();
  const double load = engine.bus_load();
  const std::uint64_t now_us = engine.now_us();
  std::vector<FaultEvent> active = engine.active_faults();
  const std::size_t depth = queue_ ? queue_->size() : 0;

  std::lock_guard<std::mutex> lk(state_mutex_);
  snapshot_.time_us = now_us;
  snapshot_.signals = sigs;
  snapshot_.bus_load = load;
  snapshot_.frames_processed = processed;
  snapshot_.frames_dropped = dropped_.load();
  snapshot_.queue_depth = depth;
  snapshot_.running = true;
  snapshot_.active_faults = std::move(active);
  for (std::size_t i = 0; i < kSignalCount; ++i) {
    const SignalValue& s = sigs[i];
    if (s.status != SignalStatus::Valid || s.update_count == history_last_count_[i]) continue;
    if (!history_[i].empty() && s.last_update_us < history_last_us_[i] + kHistoryMinSpacingUs) continue;
    history_last_count_[i] = s.update_count;
    history_last_us_[i] = s.last_update_us;
    history_[i].push_back({static_cast<double>(s.last_update_us) / 1e6, s.value});
    if (history_[i].size() > kHistoryCap) history_[i].pop_front();
  }
  for (auto& n : notes) {
    pending_.push_back(n);
    all_.push_back(std::move(n));
  }
  notes.clear();
}

MonitorSnapshot Monitor::snapshot() const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  return snapshot_;
}

std::vector<SamplePoint> Monitor::history(SignalId id, std::size_t max_points) const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  const auto& h = history_[index_of(id)];
  const std::size_t n = std::min(max_points, h.size());
  return std::vector<SamplePoint>(h.end() - static_cast<std::ptrdiff_t>(n), h.end());
}

std::vector<FaultNotification> Monitor::drain_notifications() {
  std::lock_guard<std::mutex> lk(state_mutex_);
  std::vector<FaultNotification> out;
  out.swap(pending_);
  return out;
}

std::vector<FaultNotification> Monitor::all_notifications() const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  return all_;
}

}  // namespace enginesense
