#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>

// Exact access-order tracker for semantic-cache node ids (MRU = back, LRU = front).
// Does not own values; VNodeStorage / USearch hold the data.
class LruOrder {
 public:
  LruOrder() = default;

  LruOrder(const LruOrder&) = delete;
  LruOrder& operator=(const LruOrder&) = delete;

  // Move existing id to MRU; missing id is a no-op.
  void Touch(uint16_t id) {
    auto it = map_.find(id);
    if (it == map_.end()) {
      return;
    }
    order_.erase(it->second);
    it->second = order_.insert(order_.end(), id);
  }

  // Insert as MRU, or Touch if already present.
  void Push(uint16_t id) {
    auto it = map_.find(id);
    if (it != map_.end()) {
      order_.erase(it->second);
      it->second = order_.insert(order_.end(), id);
      return;
    }
    auto list_it = order_.insert(order_.end(), id);
    map_.emplace(id, list_it);
  }

  bool Erase(uint16_t id) {
    auto it = map_.find(id);
    if (it == map_.end()) {
      return false;
    }
    order_.erase(it->second);
    map_.erase(it);
    return true;
  }

  bool PeekLru(uint16_t* out) const {
    if (out == nullptr || order_.empty()) {
      return false;
    }
    *out = order_.front();
    return true;
  }

  bool PopLru(uint16_t* out) {
    if (out == nullptr || order_.empty()) {
      return false;
    }
    const uint16_t id = order_.front();
    order_.pop_front();
    map_.erase(id);
    *out = id;
    return true;
  }

  void Clear() {
    order_.clear();
    map_.clear();
  }

  std::size_t size() const { return order_.size(); }

 private:
  using ListIter = std::list<uint16_t>::iterator;

  std::list<uint16_t> order_;
  std::unordered_map<uint16_t, ListIter> map_;
};



