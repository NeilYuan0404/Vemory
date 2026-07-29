#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Shared RDB v3 header layout (dump.rdb / repl fullsync).
namespace rdb {

inline constexpr const char* kMagic = "VEMORYDB";
inline constexpr uint32_t kVersion = 3;
inline constexpr std::size_t kHeaderBytes = 96;
inline constexpr uint32_t kFlagHasUsearch = 1u;

struct TocEntry {
  uint64_t offset = 0;
  uint64_t length = 0;
};

struct Header {
  uint32_t version = 3;
  uint32_t flags = 0;
  uint64_t dim = 0;
  uint32_t next_id = 1;
  uint32_t pad = 0;
  uint64_t kv_count = 0;
  uint64_t node_count = 0;
  TocEntry toc[3] = {};  // 0=KV, 1=NODES, 2=USEARCH
};

enum class ParseStatus : uint8_t {
  kOk = 0,
  kBadValue,
  kIoError,  // truncated
  kError,    // bad magic/version / toc OOB
};

// Parse header from the start of a mapped (or buffered) RDB image.
inline ParseStatus ParseHeader(const uint8_t* data, std::size_t size,
                               Header* out) {
  if (data == nullptr || out == nullptr) {
    return ParseStatus::kBadValue;
  }
  if (size < kHeaderBytes) {
    return ParseStatus::kIoError;
  }
  char magic[8] = {};
  Header h;
  std::size_t off = 0;
  auto take = [&](void* dst, std::size_t n) {
    std::memcpy(dst, data + off, n);
    off += n;
  };
  take(magic, 8);
  take(&h.version, sizeof(h.version));
  take(&h.flags, sizeof(h.flags));
  take(&h.dim, sizeof(h.dim));
  take(&h.next_id, sizeof(h.next_id));
  take(&h.pad, sizeof(h.pad));
  take(&h.kv_count, sizeof(h.kv_count));
  take(&h.node_count, sizeof(h.node_count));
  take(&h.toc[0], sizeof(h.toc));
  if (std::memcmp(magic, kMagic, 8) != 0 || h.version != kVersion) {
    return ParseStatus::kError;
  }
  for (const auto& e : h.toc) {
    if (e.length == 0) {
      continue;
    }
    if (e.offset > size || e.length > size - e.offset) {
      return ParseStatus::kError;
    }
  }
  *out = h;
  return ParseStatus::kOk;
}

}  // namespace rdb
