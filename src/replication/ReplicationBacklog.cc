#include "vemory/replication/ReplicationBacklog.h"

#include <algorithm>
#include <cstring>

ReplicationBacklog::ReplicationBacklog(std::size_t capacity)
    : buf_(capacity == 0 ? kDefaultCapacity : capacity) {}

bool ReplicationBacklog::FeedEncoded(std::string_view frame) {
  if (frame.empty()) {
    return true;
  }
  if (frame.size() > buf_.size()) {
    // Single frame larger than ring — drop all and keep only tip advance
    // with empty retention (Contains will fail for old offsets).
    head_ = 0;
    base_ = tip_;
    tip_ += frame.size();
    base_ = tip_;
    return true;
  }
  DropToMakeSpace(frame.size());
  AppendBytes(frame);
  tip_ += frame.size();
  return true;
}

bool ReplicationBacklog::Contains(uint64_t offset) const {
  return offset >= base_ && offset <= tip_;
}

bool ReplicationBacklog::CopyRange(uint64_t start, uint64_t end,
                                   std::string* out) const {
  if (out == nullptr || end < start || start < base_ || end > tip_) {
    return false;
  }
  const uint64_t len = end - start;
  out->clear();
  out->reserve(static_cast<std::size_t>(len));
  if (len == 0) {
    return true;
  }
  const std::size_t cap = buf_.size();
  const std::size_t start_off =
      static_cast<std::size_t>((start - base_ + head_) % cap);
  std::size_t remaining = static_cast<std::size_t>(len);
  std::size_t pos = start_off;
  while (remaining > 0) {
    const std::size_t n = std::min(remaining, cap - pos);
    out->append(buf_.data() + pos, n);
    remaining -= n;
    pos = (pos + n) % cap;
  }
  return true;
}

void ReplicationBacklog::AppendBytes(std::string_view bytes) {
  const std::size_t cap = buf_.size();
  const std::size_t used = static_cast<std::size_t>(tip_ - base_);
  const std::size_t write_pos = (head_ + used) % cap;
  const std::size_t first = std::min(bytes.size(), cap - write_pos);
  std::memcpy(buf_.data() + write_pos, bytes.data(), first);
  if (bytes.size() > first) {
    std::memcpy(buf_.data(), bytes.data() + first, bytes.size() - first);
  }
}

void ReplicationBacklog::DropToMakeSpace(std::size_t need) {
  const std::size_t cap = buf_.size();
  std::size_t used = static_cast<std::size_t>(tip_ - base_);
  while (used + need > cap && used > 0) {
    // Best-effort: drop 1/8 of capacity (or `need`) from the front.
    const std::size_t drop = std::min(used, std::max(need, cap / 8));
    head_ = (head_ + drop) % cap;
    base_ += drop;
    used = static_cast<std::size_t>(tip_ - base_);
  }
}
