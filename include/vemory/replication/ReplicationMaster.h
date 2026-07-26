#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "WalEntry.pb.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/replication/ReplicationBacklog.h"

// Master-side fullsync: tmp/repl-fullsync.rdb via sendfile + backlog bulk.
class ReplicationMaster {
 public:
  static constexpr const char* kTmpDir = "tmp";
  static constexpr const char* kFullsyncPath = "tmp/repl-fullsync.rdb";

  enum class SlaveState : uint8_t {
    kWaitRdb = 0,
    kSendingRdb,
    kSendingBacklog,
    kSynced,
  };

  explicit ReplicationMaster(SnapshotManager* snapshot);

  // Track accepted connections so PSYNC can resolve client_fd → TcpConn.
  void OnConnection(TcpConn::Ptr conn);

  // Called from PSYNC dispatcher; reply may be empty (async transfer).
  // On immediate error, writes RESP error into *reply.
  void OnPsync(int client_fd, std::string* reply);

  // Feed a successful client mutation into the backlog (no-op if idle).
  void FeedBacklog(const vemory::WalEntry& entry);

  bool has_slaves() const { return !slaves_.empty(); }
  bool fullsync_active() const { return fullsync_active_; }

 private:
  struct Slave {
    TcpConn::Ptr conn;
    SlaveState state = SlaveState::kWaitRdb;
    uint64_t backlog_start = 0;
  };

  void EnsureTmpDir();
  void StartFullsyncIfNeeded();
  void OnSaveDone(bool ok, const std::string& path);
  void BeginSendToWaiters();
  void SendRdbToSlave(Slave* slave);
  void SendBacklogToSlave(Slave* slave);
  void RemoveSlave(int fd);
  Slave* FindSlave(int fd);

  SnapshotManager* snapshot_;
  ReplicationBacklog backlog_;
  std::unordered_map<int, TcpConn::Ptr> conns_;
  std::unordered_map<int, Slave> slaves_;
  bool fullsync_active_ = false;
  uint64_t fullsync_backlog_start_ = 0;
  bool save_started_for_fullsync_ = false;
};
