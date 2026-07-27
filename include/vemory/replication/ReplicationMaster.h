#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "WalEntry.pb.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/replication/ReplicationBacklog.h"

// Master-side: PSYNC fullsync (RDB + backlog) or partial (+CONTINUE from offset).
class ReplicationMaster {
 public:
  static constexpr const char* kTmpDir = "tmp";
  static constexpr const char* kFullsyncPath = "tmp/repl-fullsync.rdb";
  // Soft limit on replica output buffer (Redis-style kick).
  static constexpr std::size_t kReplicaOutputLimit = 32u * 1024u * 1024u;
  static constexpr std::size_t kReplidLen = 40;

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
  // replid empty / "?" with offset < 0 → fullsync; else try partial.
  void OnPsync(int client_fd, const std::string& replid, int64_t offset,
               std::string* reply);

  // Feed a successful client mutation into the backlog (always retained).
  void FeedBacklog(const vemory::WalEntry& entry);

  bool has_slaves() const { return !slaves_.empty(); }
  bool fullsync_active() const { return fullsync_active_; }
  const std::string& replid() const { return replid_; }

 private:
  struct Slave {
    TcpConn::Ptr conn;
    SlaveState state = SlaveState::kWaitRdb;
    uint64_t backlog_start = 0;
  };

  static std::string GenerateReplid();

  void EnsureTmpDir();
  void StartFullsyncIfNeeded();
  void OnSaveDone(bool ok, const std::string& path);
  void BeginSendToWaiters();
  void SendRdbToSlave(Slave* slave);
  void SendBacklogToSlave(Slave* slave);
  bool TryPartialResync(int client_fd, TcpConn::Ptr conn, uint64_t offset,
                        std::string* reply);
  void RemoveSlave(int fd);
  Slave* FindSlave(int fd);

  SnapshotManager* snapshot_;
  ReplicationBacklog backlog_;
  std::string replid_;
  std::unordered_map<int, TcpConn::Ptr> conns_;
  std::unordered_map<int, Slave> slaves_;
  bool fullsync_active_ = false;
  uint64_t fullsync_backlog_start_ = 0;
  bool save_started_for_fullsync_ = false;
};
