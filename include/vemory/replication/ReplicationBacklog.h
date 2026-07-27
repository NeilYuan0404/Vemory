#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// In-memory ring buffer of RESP write-command frames (same as AOF).
class ReplicationBacklog {
 public:
  static constexpr std::size_t kDefaultCapacity = 16u * 1024u * 1024u;

  explicit ReplicationBacklog(std::size_t capacity = kDefaultCapacity);

  uint64_t tip() const { return tip_; }
  uint64_t base() const { return base_; }
  std::size_t capacity() const { return buf_.size(); }

  // Append a pre-encoded RESP frame. On overflow, advances base_ (drops oldest).
  bool FeedEncoded(std::string_view frame);

  // True if logical offset is still retained in the ring.
  bool Contains(uint64_t offset) const;

  // Copy bytes in [start, end) into *out. Requires Contains(start) and end<=tip_.
  bool CopyRange(uint64_t start, uint64_t end, std::string* out) const;

 private:
  void AppendBytes(std::string_view bytes);
  void DropToMakeSpace(std::size_t need);

  std::vector<char> buf_;
  std::size_t head_ = 0;  // physical index of logical base_
  uint64_t base_ = 0;
  uint64_t tip_ = 0;
};