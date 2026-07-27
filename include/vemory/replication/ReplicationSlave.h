#pragma once

#include <cstdint>
#include <string>

#include "vemory/net/EventLoop.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Timer.h"

// Slave-side: PSYNC fullsync or partial (+CONTINUE), then stream apply.
// On link failure, reconnects with exponential backoff; retries with
// replid+offset when known (partial if still in master backlog).
class ReplicationSlave {
 public:
  static constexpr const char* kTmpDir = "tmp";
  static constexpr const char* kInPath = "tmp/repl-in.rdb";
  static constexpr const char* kInTmpPath = "tmp/repl-in.rdb.tmp";

  static constexpr uint64_t kMinBackoffMs = 1000;
  static constexpr uint64_t kMaxBackoffMs = 60000;

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
  ~ReplicationSlave();

  ReplicationSlave(const ReplicationSlave&) = delete;
  ReplicationSlave& operator=(const ReplicationSlave&) = delete;

  // Remember master address and attempt connect + PSYNC. Returns false only on
  // fatal setup errors (null deps / tmpdir). Connect failure schedules reconnect
  // and still returns true.
  bool Start(const std::string& host, uint16_t port);

  State state() const { return state_; }
  uint64_t repl_offset() const { return repl_offset_; }
  const std::string& master_replid() const { return master_replid_; }
  bool have_offset() const { return have_offset_; }

  // Double current backoff, capped at kMaxBackoffMs.
  static uint64_t NextBackoffMs(uint64_t current_ms);

  // Parse and apply zero or more complete u32le+WalEntry frames from *bytes.
  // Consumes complete frames from the front of *bytes; leaves a partial frame.
  // On success, *bytes_consumed (if non-null) is set to erased byte count.
  // Returns false on corrupt frame.
  static bool ConsumeStreamFrames(std::string* bytes, VNodeIndex* vnode_index,
                                  KvStore* kv, std::string* err,
                                  std::size_t* bytes_consumed = nullptr);

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
  bool HandleSyncReply(const std::string& reply);

  void CancelReconnectTimer();
  void ResetLink();
  void ScheduleReconnect(const std::string& reason);
  void TryConnect();
  void EnterStreaming();
  void AdvanceOffset(std::size_t n);

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

  std::string master_host_;
  uint16_t master_port_ = 0;
  TimerNode* reconnect_timer_ = nullptr;
  uint64_t backoff_ms_ = kMinBackoffMs;
  bool closing_ = false;
  bool stopping_ = false;

  // Survives ResetLink / Fail for partial resync on reconnect.
  std::string master_replid_;
  uint64_t repl_offset_ = 0;
  bool have_offset_ = false;
};
