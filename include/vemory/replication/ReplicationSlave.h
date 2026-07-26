#pragma once

#include <cstdint>
#include <string>

#include "vemory/net/EventLoop.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

// Slave-side: connect master, PSYNC, receive tmp RDB + backlog, then stream.
class ReplicationSlave {
 public:
  static constexpr const char* kTmpDir = "tmp";
  static constexpr const char* kInPath = "tmp/repl-in.rdb";
  static constexpr const char* kInTmpPath = "tmp/repl-in.rdb.tmp";

  enum class State : uint8_t {
    kInit = 0,
    kWaitFullresync,
    kRecvRdbHeader,
    kRecvRdbBody,
    kRecvBacklogHeader,
    kRecvBacklogBody,
    kStreaming,
    kError,
  };

  ReplicationSlave(EventLoop* loop, SnapshotManager* snapshot,
                   VNodeIndex* vnode_index, KvStore* kv);

  // Blocking connect + send PSYNC; returns false on connect failure.
  bool Start(const std::string& host, uint16_t port);

  State state() const { return state_; }

  // Parse and apply zero or more complete u32le+WalEntry frames from *bytes.
  // Consumes complete frames from the front of *bytes; leaves a partial frame.
  // Returns false on corrupt frame.
  static bool ConsumeStreamFrames(std::string* bytes, VNodeIndex* vnode_index,
                                  KvStore* kv, std::string* err);

 private:
  void OnReadable();
  bool EnsureTmpDir();
  bool ConsumeSimpleString(std::string* out);
  bool TryConsumeBulkHeader(int64_t* len);
  bool WriteRdbChunk(const char* data, std::size_t n);
  bool FinishRdbAndLoad();
  bool ApplyBacklog(const std::string& bytes);
  bool DrainStreaming();
  void Fail(const std::string& msg);

  EventLoop* loop_;
  SnapshotManager* snapshot_;
  VNodeIndex* vnode_index_;
  KvStore* kv_;
  TcpConn::Ptr conn_;
  State state_ = State::kInit;
  int64_t rdb_remaining_ = -1;
  int64_t backlog_remaining_ = -1;
  FILE* rdb_fp_ = nullptr;
  std::string backlog_buf_;
  std::string stream_buf_;
};
