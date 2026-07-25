#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "WalEntry.pb.h"
#include "vemory/persist/AofWriter.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Config.h"

// Protobuf AOF: encode on the caller thread, then hand frames to an AofWriter
// (thread or io_uring background flush) under persistence.dir.
class WalManager {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotConfigured,
    kIoError,
    kError,
  };

  // dir empty or enable=false → Append is no-op (kNotConfigured).
  WalManager(VNodeIndex* vnode_index, KvStore* kv, std::string dir, bool enable,
             vemory::AofFsyncPolicy fsync = vemory::AofFsyncPolicy::kEverySec,
             vemory::AofIoMode io_mode = vemory::AofIoMode::kThread);
  ~WalManager();

  WalManager(const WalManager&) = delete;
  WalManager& operator=(const WalManager&) = delete;

  bool enabled() const { return enabled_; }
  const std::string& path() const { return path_; }
  vemory::AofFsyncPolicy fsync_policy() const { return fsync_; }
  vemory::AofIoMode io_mode() const { return io_mode_; }

  // Serialize entry and enqueue one frame. Returns after enqueue (not durable).
  // No-op if not enabled. Blocks if the writer queue is full (backpressure).
  Status Append(const vemory::WalEntry& entry);

  // Block until previously enqueued frames are written (+ fsync per policy).
  Status Flush();

  // Replay all complete frames (MutateSource::kAofReplay — does not Append).
  // Missing/empty file → kOk (start empty). Truncated tail ignored.
  Status Replay();

 private:
  static constexpr const char* kFileName = "appendonly.aof";

  static void WriteU32Le(unsigned char out[4], uint32_t v);
  static uint32_t ReadU32Le(const unsigned char in[4]);
  static bool ReadExact(FILE* fp, void* buf, std::size_t n);

  VNodeIndex* vnode_index_ = nullptr;
  KvStore* kv_ = nullptr;
  std::string dir_;
  std::string path_;
  bool enabled_ = false;
  vemory::AofFsyncPolicy fsync_ = vemory::AofFsyncPolicy::kEverySec;
  vemory::AofIoMode io_mode_ = vemory::AofIoMode::kThread;
  std::unique_ptr<AofWriter> writer_;
};
