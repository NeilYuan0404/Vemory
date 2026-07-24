#pragma once

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "vemory/util/BlockingQueue.h"

class ThreadPool {
 public:
  // Construct and start worker threads.
  explicit ThreadPool(int num_threads) : task_queue_(kDefaultQueueCapacity) {
    if (num_threads < 1) {
      num_threads = 1;
    }
    for (int i = 0; i < num_threads; ++i) {
      workers_.emplace_back([this] { Worker(); });
    }
  }

  // Stop the pool and join workers.
  ~ThreadPool() {
    task_queue_.Cancel();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // Post a task to the pool.
  template <typename F, typename... Args>
  void Post(F&& f, Args&&... args) {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    task_queue_.Push(std::move(task));
  }

 private:
  static constexpr std::size_t kDefaultQueueCapacity = 1024;

  void Worker() {
    while (true) {
      std::function<void()> task;
      if (!task_queue_.Pop(&task)) {
        break;
      }
      task();
    }
  }

  BlockingQueue<std::function<void()>> task_queue_;
  std::vector<std::thread> workers_;
};
