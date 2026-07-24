#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "WalEntry.pb.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/BlockingQueue.h"

// Protobuf AOF: reactor encodes frames into a bounded queue; one flush thread
// writes length-prefixed WalEntry frames under persistence.dir.
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

  // Serialize entry and enqueue one frame. Returns after enqueue (not durable).
  // No-op if not enabled. Blocks if the queue is full (backpressure).
  Status Append(const vemory::WalEntry& entry);

  // Block until all previously enqueued frames have been written (or IO failed).
  Status Flush();

  // Replay all complete frames (MutateSource::kAofReplay — does not Append).
  // Missing/empty file → kOk (start empty). Truncated tail ignored.
  Status Replay();

 private:
  static constexpr const char* kFileName = "appendonly.aof";
  static constexpr std::size_t kQueueCapacity = 1024;

  void FlushLoop();
  Status EnsureOpenForAppend();
  // Write a complete on-disk frame (u32le len | payload). Flush-thread only.
  Status WriteFrame(const std::string& frame);
  void IncPending();
  void DecPending();

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

  BlockingQueue<std::string> queue_;
  std::thread flush_thread_;
  std::atomic<bool> io_failed_{false};

  std::mutex drain_mu_;
  std::condition_variable drain_cv_;
  std::size_t pending_ = 0;
};
