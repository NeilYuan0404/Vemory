#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <sys/types.h>

#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Timer.h"

// Single-file RDB snapshot: dump.rdb (Header + TOC + KV/NODES/USEARCH).
// SAVE forks a child to write; Load runs on the calling thread.
// BackgroundSaveToPath / LoadFromPath accept arbitrary paths (replication tmp/).
class SnapshotManager {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kBadValue,
    kNotConfigured,   // empty dir (Save/Load only)
    kInProgress,      // background save already running
    kIoError,
    kError,
  };

  using SaveDoneCallback = std::function<void(bool ok, const std::string& path)>;

  SnapshotManager(VNodeIndex* vnode_index, KvStore* kv, std::string dir);

  const std::string& dir() const { return dir_; }
  bool configured() const { return !dir_.empty(); }
  bool save_in_progress() const { return child_pid_ > 0; }

  void SetSaveDoneCallback(SaveDoneCallback cb) { save_done_cb_ = std::move(cb); }

  // Fork child to write dir/dump.rdb; requires configured dir.
  Status BackgroundSave();

  // Fork child to write an explicit path (.tmp + rename). Does not require dir_.
  Status BackgroundSaveToPath(std::string path);

  // Load dump.rdb from dir into stores (replaces in-memory state).
  Status Load();

  // Load from an explicit path. Does not require dir_.
  Status LoadFromPath(const std::string& path);

  // Non-blocking waitpid; clears child_pid_ when done.
  void ReapSaveChild();

  ~SnapshotManager();

 private:
  static constexpr uint32_t kFlagHasUsearch = 1u;

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

  static constexpr const char* kMagic = "VEMORYDB";
  static constexpr uint32_t kVersion = 3;
  static constexpr const char* kRdbName = "dump.rdb";
  // magic(8) + version(4) + flags(4) + dim(8) + next_id(4) + pad(4) +
  // kv_count(8) + node_count(8) + toc[3]*(8+8) = 96
  static constexpr long kHeaderBytes = 96;

  Status SaveToPath(const std::string& final_path) const;
  Status LoadFromFile(FILE* fp);

  std::string Path(std::string_view name) const;
  Status WriteHeader(FILE* fp, const Header& header) const;
  Status ReadHeader(FILE* fp, Header* header) const;
  Status FsyncFile(FILE* fp) const;
  Status AtomicRename(const std::string& tmp, const std::string& final_path) const;
  void RemoveLegacyDumpFiles() const;
  void EnsureReapTimer();

  VNodeIndex* vnode_index_;
  KvStore* kv_;
  std::string dir_;
  pid_t child_pid_ = -1;
  std::string pending_save_path_;
  SaveDoneCallback save_done_cb_;
  TimerNode* reap_timer_ = nullptr;
};
