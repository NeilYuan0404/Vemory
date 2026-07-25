#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "vemory/persist/AofWriter.h"
#include "vemory/util/BlockingQueue.h"
#include "vemory/util/Config.h"

// Classic AOF flush: BlockingQueue + background thread + fwrite/fflush/fdatasync.
class ThreadAofWriter final : public AofWriter {
 public:
  ThreadAofWriter(std::string path, vemory::AofFsyncPolicy fsync);
  ~ThreadAofWriter() override;

  ThreadAofWriter(const ThreadAofWriter&) = delete;
  ThreadAofWriter& operator=(const ThreadAofWriter&) = delete;

  bool Enqueue(std::string frame) override;
  bool Flush() override;
  void Stop() override;
  bool failed() const override {
    return io_failed_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr std::size_t kQueueCapacity = 1024;

  void FlushLoop();
  bool EnsureOpen();  // caller holds file_mu_
  bool WriteFrame(const std::string& frame);  // caller holds file_mu_
  bool SyncFile();  // caller holds file_mu_
  void MaybeSyncAfterWrite();  // caller holds file_mu_
  void IncPending();
  void DecPending();

  std::string path_;
  vemory::AofFsyncPolicy fsync_;
  FILE* fp_ = nullptr;

  BlockingQueue<std::string> queue_;
  std::thread thread_;
  std::atomic<bool> io_failed_{false};
  std::atomic<bool> stopped_{false};

  std::mutex file_mu_;
  bool dirty_ = false;
  std::chrono::steady_clock::time_point last_fsync_;

  std::mutex drain_mu_;
  std::condition_variable drain_cv_;
  std::size_t pending_ = 0;
};
