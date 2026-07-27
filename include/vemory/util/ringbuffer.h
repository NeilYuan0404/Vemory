#pragma once

// SPSC ring: lock-free Push/Pop; PushWait/PopWaitFor use mutex+CV only for wake.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <utility>

template <typename T, std::size_t Capacity>
class RingBuffer {
 public:
  static_assert(Capacity && !(Capacity & (Capacity - 1)),
                "Capacity must be power of 2");

  RingBuffer() : read_(0), write_(0) {}

  ~RingBuffer() {
    std::size_t r = read_.load(std::memory_order_relaxed);
    std::size_t w = write_.load(std::memory_order_relaxed);
    while (r != w) {
      reinterpret_cast<T*>(&buffer_[r])->~T();
      r = (r + 1) & (Capacity - 1);
    }
  }

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  // Non-blocking push. Returns false if full.
  template <typename U>
  bool Push(U&& value) {
    const std::size_t w = write_.load(std::memory_order_relaxed);
    const std::size_t next_w = (w + 1) & (Capacity - 1);
    if (next_w == read_.load(std::memory_order_acquire)) {
      return false;
    }
    new (&buffer_[w]) T(std::forward<U>(value));
    write_.store(next_w, std::memory_order_release);
    not_empty_.notify_one();
    return true;
  }

  // Block while full. Returns false if Cancel() was called (item not enqueued).
  template <typename U>
  bool PushWait(U&& value) {
    T tmp(std::forward<U>(value));
    while (true) {
      if (cancelled_.load(std::memory_order_acquire)) {
        return false;
      }
      if (Push(std::move(tmp))) {
        return true;
      }
      std::unique_lock<std::mutex> lock(mu_);
      not_full_.wait(lock, [this] {
        return cancelled_.load(std::memory_order_relaxed) || !Full();
      });
      if (cancelled_.load(std::memory_order_relaxed)) {
        return false;
      }
    }
  }

  // Non-blocking pop. Returns false if empty.
  bool Pop(T& value) {
    const std::size_t r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire)) {
      return false;
    }
    T* ptr = reinterpret_cast<T*>(&buffer_[r]);
    value = std::move(*ptr);
    ptr->~T();
    read_.store((r + 1) & (Capacity - 1), std::memory_order_release);
    not_full_.notify_one();
    return true;
  }

  // Wait up to `timeout` for an item. Returns true and fills *value on success.
  // Returns false on timeout (still empty) or Cancel()+empty.
  template <typename Rep, typename Period>
  bool PopWaitFor(T* value,
                  const std::chrono::duration<Rep, Period>& timeout) {
    if (value == nullptr) {
      return false;
    }
    if (Pop(*value)) {
      return true;
    }
    if (timeout == std::chrono::duration<Rep, Period>::zero()) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(mu_);
    while (true) {
      if (Pop(*value)) {
        return true;
      }
      if (cancelled_.load(std::memory_order_relaxed)) {
        return false;
      }
      if (not_empty_.wait_until(lock, deadline) == std::cv_status::timeout) {
        // Final check after timeout (producer may have raced with notify).
        return Pop(*value);
      }
    }
  }

  void Cancel() {
    cancelled_.store(true, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(mu_);
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  bool cancelled() const {
    return cancelled_.load(std::memory_order_acquire);
  }

  std::size_t Size() const {
    const std::size_t r = read_.load(std::memory_order_acquire);
    const std::size_t w = write_.load(std::memory_order_acquire);
    return (w >= r) ? (w - r) : (Capacity - r + w);
  }

  // Usable slots before full (one slot reserved for empty/full distinction).
  static constexpr std::size_t usable_capacity() { return Capacity - 1; }

 private:
  bool Full() const {
    const std::size_t w = write_.load(std::memory_order_relaxed);
    const std::size_t next_w = (w + 1) & (Capacity - 1);
    return next_w == read_.load(std::memory_order_acquire);
  }

  alignas(64) std::atomic<std::size_t> read_;
  alignas(64) std::atomic<std::size_t> write_;
  alignas(64) typename std::aligned_storage<sizeof(T), alignof(T)>::type
      buffer_[Capacity];

  std::atomic<bool> cancelled_{false};
  mutable std::mutex mu_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
};
