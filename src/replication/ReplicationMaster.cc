#include "vemory/replication/ReplicationMaster.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include <sys/stat.h>

#include <spdlog/spdlog.h>

#include "vemory/protocol/resp/RespEncode.h"

ReplicationMaster::ReplicationMaster(SnapshotManager* snapshot)
    : snapshot_(snapshot) {
  if (snapshot_ != nullptr) {
    snapshot_->SetSaveDoneCallback(
        [this](bool ok, const std::string& path) { OnSaveDone(ok, path); });
  }
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

void ReplicationMaster::OnPsync(int client_fd, std::string* reply) {
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

  Slave slave;
  slave.conn = it->second;
  slave.state = SlaveState::kWaitRdb;
  slave.backlog_start = backlog_.tip();
  slaves_[client_fd] = std::move(slave);
  spdlog::info("PSYNC registered slave fd={}", client_fd);

  StartFullsyncIfNeeded();
  // Async: +FULLRESYNC sent when RDB is ready.
}

void ReplicationMaster::FeedBacklog(const vemory::WalEntry& entry) {
  if (!fullsync_active_ && slaves_.empty()) {
    return;
  }
  if (!backlog_.Feed(entry)) {
    spdlog::warn("replication backlog encode failed");
    return;
  }
  // Disconnect slaves whose start offset fell out of the ring.
  std::vector<int> drop;
  for (auto& [fd, slave] : slaves_) {
    if (slave.state == SlaveState::kSynced) {
      continue;
    }
    if (!backlog_.Contains(slave.backlog_start)) {
      drop.push_back(fd);
    }
  }
  for (int fd : drop) {
    spdlog::warn("replication backlog overflow; dropping slave fd={}", fd);
    auto* s = FindSlave(fd);
    if (s != nullptr && s->conn) {
      // Close triggers RemoveSlave via close_cb.
      // Intentionally leave Close private — erase and let peer timeout,
      // or send error then rely on erase.
      const std::string err = "-ERR backlog overflow\r\n";
      s->conn->Send(err.data(), err.size());
    }
    slaves_.erase(fd);
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
  // If nobody left waiting/sending, clear fullsync_active when all done.
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
  std::string hdr;
  RespEncode::AppendSimpleString(&hdr, "FULLRESYNC");
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
