#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

// Bounded blocking queue (mutex + condvars).
// Intended SPSC-style use (e.g. reactor Encode → AOF flush thread), but safe for
// multiple producers/consumers. Does not lock storage engines — only the queue.
template <typename T>
class BlockingQueue {
 public:
  explicit BlockingQueue(std::size_t capacity) : capacity_(capacity < 1 ? 1 : capacity) {}

  BlockingQueue(const BlockingQueue&) = delete;
  BlockingQueue& operator=(const BlockingQueue&) = delete;

  // Move-friendly enqueue. Blocks while full.
  // Returns false if Cancel() was called (item is not enqueued).
  bool Push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] {
      return queue_.size() < capacity_ || cancelled_;
    });
    if (cancelled_) {
      return false;
    }
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  // Dequeue into *value (moved). Blocks while empty.
  // Returns false if Cancel() and the queue is empty.
  bool Pop(T* value) {
    if (value == nullptr) {
      return false;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || cancelled_; });
    if (queue_.empty()) {
      return false;
    }
    *value = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return true;
  }

  void Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  bool cancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancelled_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  std::size_t capacity() const { return capacity_; }

 private:
  std::size_t capacity_;
  bool cancelled_ = false;
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
};
