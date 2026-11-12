#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace enginesense {

/// Multi-producer / multi-consumer bounded queue with close semantics.
/// try_push never blocks (used on real-time paths, caller counts drops); push blocks for
/// back-pressure (used for offline replay where no frame may be lost).
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  bool try_push(T v) {
    {
      std::lock_guard<std::mutex> lk(m_);
      if (closed_ || q_.size() >= capacity_) return false;
      q_.push_back(std::move(v));
    }
    not_empty_.notify_one();
    return true;
  }

  /// Blocks while full. Returns false if the queue was closed.
  bool push(T v) {
    std::unique_lock<std::mutex> lk(m_);
    not_full_.wait(lk, [&] { return closed_ || q_.size() < capacity_; });
    if (closed_) return false;
    q_.push_back(std::move(v));
    lk.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// Blocks until an item is available or the queue is closed *and* drained.
  bool pop(T& out) {
    std::unique_lock<std::mutex> lk(m_);
    not_empty_.wait(lk, [&] { return closed_ || !q_.empty(); });
    if (q_.empty()) return false;
    out = std::move(q_.front());
    q_.pop_front();
    lk.unlock();
    not_full_.notify_one();
    return true;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lk(m_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lk(m_);
    return q_.size();
  }

 private:
  mutable std::mutex m_;
  std::condition_variable not_empty_, not_full_;
  std::deque<T> q_;
  std::size_t capacity_;
  bool closed_ = false;
};

}  // namespace enginesense
