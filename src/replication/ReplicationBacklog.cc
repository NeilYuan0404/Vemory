#include "vemory/replication/ReplicationBacklog.h"

#include <algorithm>
#include <cstring>

namespace {

void WriteU32Le(unsigned char out[4], uint32_t v) {
  out[0] = static_cast<unsigned char>(v & 0xffu);
  out[1] = static_cast<unsigned char>((v >> 8) & 0xffu);
  out[2] = static_cast<unsigned char>((v >> 16) & 0xffu);
  out[3] = static_cast<unsigned char>((v >> 24) & 0xffu);
}

}  // namespace

ReplicationBacklog::ReplicationBacklog(std::size_t capacity)
    : buf_(capacity == 0 ? kDefaultCapacity : capacity) {}

bool ReplicationBacklog::EncodeFrame(const vemory::WalEntry& entry,
                                     std::string* frame) {
  if (frame == nullptr) {
    return false;
  }
  std::string payload;
  if (!entry.SerializeToString(&payload)) {
    return false;
  }
  if (payload.size() > 0xffffffffu) {
    return false;
  }
  unsigned char len_buf[4];
  WriteU32Le(len_buf, static_cast<uint32_t>(payload.size()));
  frame->assign(reinterpret_cast<const char*>(len_buf), 4);
  frame->append(payload);
  return true;
}

bool ReplicationBacklog::Feed(const vemory::WalEntry& entry) {
  std::string frame;
  if (!EncodeFrame(entry, &frame)) {
    return false;
  }
  return FeedEncoded(frame);
}

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
  const std::size_t start_idx =
      (head_ + static_cast<std::size_t>(start - base_)) % cap;
  std::size_t remaining = static_cast<std::size_t>(len);
  std::size_t idx = start_idx;
  while (remaining > 0) {
    const std::size_t chunk = std::min(remaining, cap - idx);
    out->append(buf_.data() + idx, chunk);
    remaining -= chunk;
    idx = (idx + chunk) % cap;
  }
  return true;
}

void ReplicationBacklog::AppendBytes(std::string_view bytes) {
  const std::size_t cap = buf_.size();
  const std::size_t used = static_cast<std::size_t>(tip_ - base_);
  std::size_t write_idx = (head_ + used) % cap;
  std::size_t remaining = bytes.size();
  std::size_t off = 0;
  while (remaining > 0) {
    const std::size_t chunk = std::min(remaining, cap - write_idx);
    std::memcpy(buf_.data() + write_idx, bytes.data() + off, chunk);
    remaining -= chunk;
    off += chunk;
    write_idx = (write_idx + chunk) % cap;
  }
}

void ReplicationBacklog::DropToMakeSpace(std::size_t need) {
  const std::size_t cap = buf_.size();
  std::size_t used = static_cast<std::size_t>(tip_ - base_);
  while (used + need > cap && used > 0) {
    // Drop at least one byte from the front; prefer dropping whole frames
    // when possible (u32le length). Best-effort: drop 1/8 of capacity.
    const std::size_t drop =
        std::min(used, std::max(need, cap / 8));
    head_ = (head_ + drop) % cap;
    base_ += drop;
    used = static_cast<std::size_t>(tip_ - base_);
  }
}
