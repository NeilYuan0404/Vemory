#pragma once

#include <chrono>
#include <functional>
#include <map>

class TimerNode {
 public:
  friend class Timer;
  TimerNode(uint64_t timeout, std::function<void()> callback)
      : timeout_(timeout), callback_(std::move(callback)) {}

 private:
  int id_;
  uint64_t timeout_;
  std::function<void()> callback_;
};

class Timer {
 public:
  static Timer* GetInstance() {
    static Timer instance;
    return &instance;
  }

  static uint64_t GetCurrentTime() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
  }

  TimerNode* AddTimeout(uint64_t diff, std::function<void()> cb) {
    auto* node = new TimerNode(GetCurrentTime() + diff, std::move(cb));
    if (timer_map_.empty() || node->timeout_ < timer_map_.rbegin()->first) {
      auto it = timer_map_.insert(std::make_pair(node->timeout_, node));
      return it->second;
    }
    auto it = timer_map_.emplace_hint(timer_map_.crbegin().base(),
                                      std::make_pair(node->timeout_, node));
    return it->second;
  }

  void DelTimeout(TimerNode* node) {
    auto it = timer_map_.equal_range(node->timeout_);
    for (auto iter = it.first; iter != it.second; ++iter) {
      if (iter->second == node) {
        delete iter->second;
        timer_map_.erase(iter);
        break;
      }
    }
  }

  int WaitTime() {
    auto iter = timer_map_.begin();
    if (iter == timer_map_.end()) {
      return -1;
    }
    uint64_t diff = iter->first - GetCurrentTime();
    return diff > 0 ? static_cast<int>(diff) : 0;
  }

  void HandleTimeout() {
    auto iter = timer_map_.begin();
    while (iter != timer_map_.end() && iter->first <= GetCurrentTime()) {
      iter->second->callback_();
      delete iter->second;
      iter = timer_map_.erase(iter);
    }
  }

 private:
  std::multimap<uint64_t, TimerNode*> timer_map_;

  Timer() = default;
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;
  ~Timer() {
    for (auto& pair : timer_map_) {
      delete pair.second;
    }
  }
};
