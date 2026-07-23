#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>

#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Timer.h"

// Multi-file RDB snapshot: dump.meta / dump.kv / dump.nodes / dump.usearch.
// SAVE forks a child to write; Load runs on the calling thread.
class SnapshotManager {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kBadValue,
    kNotConfigured,   // empty dir
    kInProgress,      // background save already running
    kIoError,
    kError,
  };

  SnapshotManager(VNodeIndex* vnode_index, KvStore* kv, std::string dir);

  const std::string& dir() const { return dir_; }
  bool configured() const { return !dir_.empty(); }
  bool save_in_progress() const { return child_pid_ > 0; }

  // Synchronous dump (used by child after fork, and by unit tests).
  Status SaveToDir() const;

  // Fork child to SaveToDir; parent returns immediately.
  Status BackgroundSave();

  // Load dump.* from dir into stores (replaces in-memory state).
  Status Load();

  // Non-blocking waitpid; clears child_pid_ when done.
  void ReapSaveChild();

  ~SnapshotManager();

 private:
  struct Meta {
    uint32_t version = 1;
    uint64_t dim = 0;
    uint32_t next_id = 1;
    uint64_t kv_count = 0;
    uint64_t node_count = 0;
  };

  static constexpr const char* kMagic = "VEMORYSN";
  static constexpr uint32_t kVersion = 1;

  std::string Path(std::string_view name) const;
  Status WriteMeta(const std::string& path, const Meta& meta) const;
  Status ReadMeta(const std::string& path, Meta* meta) const;
  Status FsyncFile(FILE* fp) const;
  Status AtomicRename(const std::string& tmp, const std::string& final_path) const;
  void EnsureReapTimer();

  VNodeIndex* vnode_index_;
  KvStore* kv_;
  std::string dir_;
  pid_t child_pid_ = -1;
  TimerNode* reap_timer_ = nullptr;
};
