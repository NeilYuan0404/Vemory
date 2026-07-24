#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "WalEntry.pb.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

// Sync protobuf AOF: length-prefixed WalEntry frames under persistence.dir.
class WalManager {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotConfigured,
    kIoError,
    kError,
  };

  // dir empty or enable=false → Append is no-op (kNotConfigured).
  WalManager(VNodeIndex* vnode_index, KvStore* kv, std::string dir, bool enable);
  ~WalManager();

  WalManager(const WalManager&) = delete;
  WalManager& operator=(const WalManager&) = delete;

  bool enabled() const { return enabled_; }
  const std::string& path() const { return path_; }

  // Serialize entry and append one frame. No-op if not enabled.
  Status Append(const vemory::WalEntry& entry);

  // Replay all complete frames (MutateSource::kAofReplay — does not Append).
  // Missing/empty file → kOk (start empty). Truncated tail ignored.
  Status Replay();

 private:
  static constexpr const char* kFileName = "appendonly.aof";

  Status EnsureOpenForAppend();
  Status WriteFrame(const std::string& payload);
  static bool ReadExact(FILE* fp, void* buf, std::size_t n);
  static bool WriteExact(FILE* fp, const void* buf, std::size_t n);
  static void WriteU32Le(unsigned char out[4], uint32_t v);
  static uint32_t ReadU32Le(const unsigned char in[4]);

  VNodeIndex* vnode_index_ = nullptr;
  KvStore* kv_ = nullptr;
  std::string dir_;
  std::string path_;
  bool enabled_ = false;
  FILE* fp_ = nullptr;
};
