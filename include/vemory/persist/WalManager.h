#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "vemory/persist/AofWriter.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Config.h"

// RESP AOF: enqueue encoded write commands under persistence.dir.
class WalManager {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotConfigured,
    kIoError,
    kError,
  };

  // dir empty or enable=false → AppendFrame is no-op (kNotConfigured).
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

  // Enqueue one RESP write-command frame. Returns after enqueue (not durable).
  // No-op if not enabled. Blocks if the writer queue is full (backpressure).
  Status AppendFrame(std::string frame);

  // Block until previously enqueued frames are written (+ fsync per policy).
  Status Flush();

  // Replay all complete RESP write commands (MutateSource::kAofReplay).
  // Missing/empty file → kOk (start empty). Truncated tail ignored.
  Status Replay();

 private:
  static constexpr const char* kFileName = "appendonly.aof";

  VNodeIndex* vnode_index_ = nullptr;
  KvStore* kv_ = nullptr;
  std::string dir_;
  std::string path_;
  bool enabled_ = false;
  vemory::AofFsyncPolicy fsync_ = vemory::AofFsyncPolicy::kEverySec;
  vemory::AofIoMode io_mode_ = vemory::AofIoMode::kThread;
  std::unique_ptr<AofWriter> writer_;
};