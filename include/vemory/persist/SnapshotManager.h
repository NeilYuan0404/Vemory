#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <sys/types.h>

#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Timer.h"

// Single-file RDB snapshot: dump.rdb (Header + TOC + KV/NODES/USEARCH).
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

  // Fork child to write dump.rdb; parent returns immediately.
  Status BackgroundSave();

  // Load dump.rdb from dir into stores (replaces in-memory state).
  Status Load();

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
    uint32_t version = 2;
    uint32_t flags = 0;
    uint64_t dim = 0;
    uint32_t next_id = 1;
    uint32_t pad = 0;
    uint64_t kv_count = 0;
    uint64_t node_count = 0;
    TocEntry toc[3] = {};  // 0=KV, 1=NODES, 2=USEARCH
  };

  static constexpr const char* kMagic = "VEMORYDB";
  static constexpr uint32_t kVersion = 2;
  static constexpr const char* kRdbName = "dump.rdb";
  static constexpr const char* kRdbTmpName = "dump.rdb.tmp";
  // magic(8) + version(4) + flags(4) + dim(8) + next_id(4) + pad(4) +
  // kv_count(8) + node_count(8) + toc[3]*(8+8) = 8+4+4+8+4+4+8+8+48 = 96
  static constexpr long kHeaderBytes = 96;

  // Synchronous dump used by the forked child in BackgroundSave.
  Status SaveToDir() const;

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
  TimerNode* reap_timer_ = nullptr;
};
