#include "vemory/replication/ReplicationMaster.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <system_error>
#include <string_view>

#include <sys/stat.h>

#include <spdlog/spdlog.h>

#include "vemory/protocol/resp/RespEncode.h"

namespace {

bool WantsFullsync(const std::string& replid, int64_t offset) {
  if (offset < 0) {
    return true;
  }
  return replid.empty() || replid == "?";
}

}  // namespace

std::string ReplicationMaster::GenerateReplid() {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::string id;
  id.resize(kReplidLen);
  for (std::size_t i = 0; i < kReplidLen; ++i) {
    id[i] = kHex[rd() & 0xfu];
  }
  return id;
}

ReplicationMaster::ReplicationMaster(SnapshotManager* snapshot)
    : snapshot_(snapshot), replid_(GenerateReplid()) {
  if (snapshot_ != nullptr) {
    snapshot_->SetSaveDoneCallback(
        [this](bool ok, const std::string& path) { OnSaveDone(ok, path); });
  }
  spdlog::info("replication master replid={}", replid_);
}

void ReplicationMaster::OnConnection(TcpConn::Ptr conn) {
  if (!conn) {
    return;
  }
  const int fd = conn->Fd();
  conns_[fd] = conn;
  conn->SetCloseCallback([this, fd]() {
    RemoveSlave(fd);
    conns_.erase(fd);
  });
}

bool ReplicationMaster::TryPartialResync(int client_fd, TcpConn::Ptr conn,
                                         uint64_t offset, std::string* reply) {
  if (!conn || conn->closed() || reply == nullptr) {
    return false;
  }
  if (!backlog_.Contains(offset)) {
    return false;
  }

  std::string payload;
  if (!backlog_.CopyRange(offset, backlog_.tip(), &payload)) {
    return false;
  }

  Slave slave;
  slave.conn = conn;
  slave.state = SlaveState::kSynced;
  slave.backlog_start = offset;
  slaves_[client_fd] = std::move(slave);

  std::string cont;
  RespEncode::AppendSimpleString(&cont, "CONTINUE");
  conn->Send(cont.data(), cont.size());
  if (!payload.empty()) {
    // Catch-up is raw frames (no RESP bulk); slave enters streaming immediately.
    conn->Send(payload.data(), payload.size());
  }
  reply->clear();
  spdlog::info(
      "PSYNC CONTINUE slave fd={} offset={} catchup_bytes={} tip={}",
      client_fd, offset, payload.size(), backlog_.tip());
  return true;
}

void ReplicationMaster::OnPsync(int client_fd, const std::string& replid,
                                int64_t offset, std::string* reply) {
  if (reply == nullptr) {
    return;
  }
  reply->clear();
  if (snapshot_ == nullptr) {
    RespEncode::AppendError(reply, "replication not available");
    return;
  }
  auto it = conns_.find(client_fd);
  if (it == conns_.end() || !it->second || it->second->closed()) {
    RespEncode::AppendError(reply, "unknown connection");
    return;
  }

  // Drop any previous slave registration for this fd.
  RemoveSlave(client_fd);

  if (!WantsFullsync(replid, offset) && replid == replid_ &&
      offset >= 0 &&
      TryPartialResync(client_fd, it->second, static_cast<uint64_t>(offset),
                       reply)) {
    return;
  }

  Slave slave;
  slave.conn = it->second;
  slave.state = SlaveState::kWaitRdb;
  slave.backlog_start = backlog_.tip();
  slaves_[client_fd] = std::move(slave);
  spdlog::info("PSYNC FULLRESYNC registered slave fd={} (replid_ok={} offset={})",
               client_fd, replid == replid_, offset);

  StartFullsyncIfNeeded();
  // Async: +FULLRESYNC sent when RDB is ready.
}

void ReplicationMaster::FeedEncodedFrame(std::string_view frame) {
  if (!backlog_.FeedEncoded(frame)) {
    spdlog::warn("replication backlog feed failed");
    return;
  }

  std::vector<int> drop;
  for (auto& [fd, slave] : slaves_) {
    if (slave.state == SlaveState::kSynced) {
      if (!slave.conn || slave.conn->closed()) {
        drop.push_back(fd);
        continue;
      }
      // Direct push (main thread); skip slaves still in fullsync.
      slave.conn->Send(frame.data(), frame.size());
      if (slave.conn->OutputBufferedBytes() > kReplicaOutputLimit) {
        spdlog::warn(
            "replica output buffer exceeded limit; kicking slave fd={} buffered={}",
            fd, slave.conn->OutputBufferedBytes());
        drop.push_back(fd);
      }
      continue;
    }
    // Fullsync waiters: drop if backlog start fell out of the ring.
    if (!backlog_.Contains(slave.backlog_start)) {
      drop.push_back(fd);
    }
  }
  for (int fd : drop) {
    auto* s = FindSlave(fd);
    if (s != nullptr && s->conn && !s->conn->closed()) {
      if (s->state != SlaveState::kSynced) {
        spdlog::warn("replication backlog overflow; dropping slave fd={}", fd);
        const std::string err = "-ERR backlog overflow\r\n";
        s->conn->Send(err.data(), err.size());
      }
      s->conn->ForceClose();
    } else {
      slaves_.erase(fd);
    }
  }
}

void ReplicationMaster::EnsureTmpDir() {
  std::error_code ec;
  std::filesystem::create_directories(kTmpDir, ec);
}

void ReplicationMaster::StartFullsyncIfNeeded() {
  if (save_started_for_fullsync_) {
    return;
  }
  bool need = false;
  for (const auto& [fd, slave] : slaves_) {
    if (slave.state == SlaveState::kWaitRdb) {
      need = true;
      break;
    }
  }
  if (!need) {
    return;
  }

  EnsureTmpDir();
  if (!fullsync_active_) {
    fullsync_backlog_start_ = backlog_.tip();
    fullsync_active_ = true;
  }
  // Align waiting slaves to the shared fullsync start.
  for (auto& [fd, slave] : slaves_) {
    if (slave.state == SlaveState::kWaitRdb) {
      slave.backlog_start = fullsync_backlog_start_;
    }
  }

  if (snapshot_->save_in_progress()) {
    // Wait for current save to finish; OnSaveDone will retry or send.
    return;
  }

  const auto st = snapshot_->BackgroundSaveToPath(kFullsyncPath);
  if (st == SnapshotManager::Status::kOk) {
    save_started_for_fullsync_ = true;
    spdlog::info("fullsync BGSAVE started path={}", kFullsyncPath);
    return;
  }
  if (st == SnapshotManager::Status::kInProgress) {
    return;
  }
  spdlog::error("fullsync BGSAVE failed status={}", static_cast<int>(st));
  std::vector<int> drop;
  for (auto& [fd, slave] : slaves_) {
    if (slave.state == SlaveState::kWaitRdb) {
      const std::string err = "-ERR fullsync save failed\r\n";
      if (slave.conn) {
        slave.conn->Send(err.data(), err.size());
      }
      drop.push_back(fd);
    }
  }
  for (int fd : drop) {
    slaves_.erase(fd);
  }
  fullsync_active_ = false;
}

void ReplicationMaster::OnSaveDone(bool ok, const std::string& path) {
  const bool was_fullsync = save_started_for_fullsync_ &&
                            path == kFullsyncPath;
  save_started_for_fullsync_ = false;

  if (was_fullsync) {
    if (!ok) {
      spdlog::error("fullsync RDB save failed");
      std::vector<int> drop;
      for (auto& [fd, slave] : slaves_) {
        if (slave.state == SlaveState::kWaitRdb) {
          const std::string err = "-ERR fullsync save failed\r\n";
          if (slave.conn) {
            slave.conn->Send(err.data(), err.size());
          }
          drop.push_back(fd);
        }
      }
      for (int fd : drop) {
        slaves_.erase(fd);
      }
      fullsync_active_ = false;
      return;
    }
    BeginSendToWaiters();
    return;
  }

  // A persistence SAVE finished while slaves were waiting — start fullsync RDB.
  StartFullsyncIfNeeded();
}

void ReplicationMaster::BeginSendToWaiters() {
  std::vector<int> fds;
  for (auto& [fd, slave] : slaves_) {
    if (slave.state == SlaveState::kWaitRdb) {
      fds.push_back(fd);
    }
  }
  for (int fd : fds) {
    auto* slave = FindSlave(fd);
    if (slave != nullptr) {
      SendRdbToSlave(slave);
    }
  }
}

void ReplicationMaster::SendRdbToSlave(Slave* slave) {
  if (slave == nullptr || !slave->conn || slave->conn->closed()) {
    return;
  }
  struct stat st {};
  if (::stat(kFullsyncPath, &st) != 0 || st.st_size < 0) {
    const std::string err = "-ERR fullsync rdb missing\r\n";
    slave->conn->Send(err.data(), err.size());
    RemoveSlave(slave->conn->Fd());
    return;
  }

  slave->state = SlaveState::kSendingRdb;
  char fr_line[128];
  const int fr_n =
      std::snprintf(fr_line, sizeof(fr_line), "FULLRESYNC %s %llu",
                    replid_.c_str(),
                    static_cast<unsigned long long>(slave->backlog_start));
  std::string hdr;
  RespEncode::AppendSimpleString(
      &hdr, std::string_view(fr_line, static_cast<std::size_t>(fr_n)));
  char len_line[64];
  const int n = std::snprintf(len_line, sizeof(len_line), "$%lld\r\n",
                              static_cast<long long>(st.st_size));
  hdr.append(len_line, static_cast<std::size_t>(n));
  slave->conn->Send(hdr.data(), hdr.size());

  const int fd = slave->conn->Fd();
  const bool ok = slave->conn->SendFile(
      kFullsyncPath, [this, fd](bool send_ok) {
        auto* s = FindSlave(fd);
        if (s == nullptr) {
          return;
        }
        if (!send_ok) {
          spdlog::warn("sendfile to slave fd={} failed", fd);
          RemoveSlave(fd);
          return;
        }
        SendBacklogToSlave(s);
      });
  if (!ok) {
    const std::string err = "-ERR sendfile failed\r\n";
    slave->conn->Send(err.data(), err.size());
    RemoveSlave(fd);
  }
}

void ReplicationMaster::SendBacklogToSlave(Slave* slave) {
  if (slave == nullptr || !slave->conn || slave->conn->closed()) {
    return;
  }
  slave->state = SlaveState::kSendingBacklog;

  if (!backlog_.Contains(slave->backlog_start)) {
    const std::string err = "-ERR backlog lost\r\n";
    slave->conn->Send(err.data(), err.size());
    RemoveSlave(slave->conn->Fd());
    return;
  }

  std::string payload;
  if (!backlog_.CopyRange(slave->backlog_start, backlog_.tip(), &payload)) {
    const std::string err = "-ERR backlog copy failed\r\n";
    slave->conn->Send(err.data(), err.size());
    RemoveSlave(slave->conn->Fd());
    return;
  }

  std::string hdr;
  char len_line[64];
  const int n = std::snprintf(len_line, sizeof(len_line), "$%zu\r\n",
                              payload.size());
  hdr.append(len_line, static_cast<std::size_t>(n));
  slave->conn->Send(hdr.data(), hdr.size());
  if (!payload.empty()) {
    slave->conn->Send(payload.data(), payload.size());
  }

  slave->state = SlaveState::kSynced;
  spdlog::info("fullsync complete slave fd={} backlog_bytes={}",
               slave->conn->Fd(), payload.size());

  bool any_active = false;
  for (const auto& [fd, s] : slaves_) {
    if (s.state != SlaveState::kSynced) {
      any_active = true;
      break;
    }
  }
  if (!any_active) {
    fullsync_active_ = false;
  }
}

void ReplicationMaster::RemoveSlave(int fd) {
  slaves_.erase(fd);
  bool any_active = false;
  for (const auto& [idf, s] : slaves_) {
    (void)idf;
    if (s.state != SlaveState::kSynced) {
      any_active = true;
      break;
    }
  }
  if (!any_active && !save_started_for_fullsync_) {
    fullsync_active_ = false;
  }
}

ReplicationMaster::Slave* ReplicationMaster::FindSlave(int fd) {
  auto it = slaves_.find(fd);
  if (it == slaves_.end()) {
    return nullptr;
  }
  return &it->second;
}
