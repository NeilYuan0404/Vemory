#include "vemory/replication/ReplicationSlave.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <spdlog/spdlog.h>

#include "WalEntry.pb.h"
#include "vemory/mutate/MutationApply.h"
#include "vemory/net/TcpConnector.h"

namespace {

uint32_t ReadU32Le(const unsigned char in[4]) {
  return static_cast<uint32_t>(in[0]) |
         (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

}  // namespace

ReplicationSlave::ReplicationSlave(EventLoop* loop, SnapshotManager* snapshot,
                                   VNodeIndex* vnode_index, KvStore* kv)
    : loop_(loop),
      snapshot_(snapshot),
      vnode_index_(vnode_index),
      kv_(kv) {}

ReplicationSlave::~ReplicationSlave() {
  stopping_ = true;
  CancelReconnectTimer();
  ResetLink();
}

uint64_t ReplicationSlave::NextBackoffMs(uint64_t current_ms) {
  if (current_ms >= kMaxBackoffMs) {
    return kMaxBackoffMs;
  }
  const uint64_t next = current_ms * 2;
  return next > kMaxBackoffMs ? kMaxBackoffMs : next;
}

bool ReplicationSlave::EnsureTmpDir() {
  std::error_code ec;
  std::filesystem::create_directories(kTmpDir, ec);
  return !ec;
}

void ReplicationSlave::CancelReconnectTimer() {
  if (reconnect_timer_ != nullptr) {
    Timer::GetInstance()->DelTimeout(reconnect_timer_);
    reconnect_timer_ = nullptr;
  }
}

void ReplicationSlave::ResetLink() {
  CancelReconnectTimer();

  closing_ = true;
  if (conn_) {
    conn_->SetReadCallback({});
    conn_->SetCloseCallback({});
    if (!conn_->closed()) {
      conn_->ForceClose();
    }
    conn_.reset();
  }
  closing_ = false;

  if (rdb_fp_ != nullptr) {
    std::fclose(rdb_fp_);
    rdb_fp_ = nullptr;
  }
  backlog_buf_.clear();
  stream_buf_.clear();
  rdb_remaining_ = -1;
  backlog_remaining_ = -1;
  state_ = State::kInit;
}

void ReplicationSlave::ScheduleReconnect(const std::string& reason) {
  if (stopping_) {
    return;
  }
  CancelReconnectTimer();
  const uint64_t delay = backoff_ms_;
  backoff_ms_ = NextBackoffMs(backoff_ms_);
  state_ = State::kError;
  spdlog::warn("slave: {}; reconnect in {}ms to {}:{}", reason, delay,
               master_host_, master_port_);
  reconnect_timer_ = Timer::GetInstance()->AddTimeout(delay, [this]() {
    reconnect_timer_ = nullptr;
    TryConnect();
  });
}

void ReplicationSlave::TryConnect() {
  if (stopping_ || loop_ == nullptr || snapshot_ == nullptr) {
    return;
  }
  ResetLink();

  const int fd = TcpConnector::Connect(master_host_, master_port_);
  if (fd < 0) {
    spdlog::error("slave: connect {}:{} failed", master_host_, master_port_);
    ScheduleReconnect("connect failed");
    return;
  }

  conn_ = std::make_shared<TcpConn>(fd, *loop_);
  state_ = State::kWaitFullresync;

  const char psync[] = "*1\r\n$5\r\nPSYNC\r\n";
  conn_->Send(psync, sizeof(psync) - 1);
  conn_->SetReadCallback([this]() { OnReadable(); });
  conn_->SetCloseCallback([this]() {
    if (closing_ || stopping_) {
      return;
    }
    Fail("connection closed");
  });

  spdlog::info("slave: sent PSYNC to {}:{}", master_host_, master_port_);
}

void ReplicationSlave::EnterStreaming() {
  state_ = State::kStreaming;
  backoff_ms_ = kMinBackoffMs;
}

bool ReplicationSlave::Start(const std::string& host, uint16_t port) {
  if (loop_ == nullptr || snapshot_ == nullptr) {
    return false;
  }
  if (!EnsureTmpDir()) {
    spdlog::error("slave: cannot create {}", kTmpDir);
    return false;
  }
  if (host.empty() || port == 0) {
    spdlog::error("slave: invalid master address");
    return false;
  }

  master_host_ = host;
  master_port_ = port;
  backoff_ms_ = kMinBackoffMs;
  TryConnect();
  return true;
}

void ReplicationSlave::Fail(const std::string& msg) {
  if (stopping_ || state_ == State::kError) {
    return;
  }
  spdlog::error("slave: {}", msg);
  state_ = State::kError;
  if (rdb_fp_ != nullptr) {
    std::fclose(rdb_fp_);
    rdb_fp_ = nullptr;
  }
  // Keep conn_ alive so callers can still touch InputBuffer after Fail returns;
  // TryConnect/ResetLink drops it on the next attempt.
  closing_ = true;
  if (conn_) {
    conn_->SetReadCallback({});
    conn_->SetCloseCallback({});
    if (!conn_->closed()) {
      conn_->ForceClose();
    }
  }
  closing_ = false;
  ScheduleReconnect(msg);
}

bool ReplicationSlave::ConsumeSimpleString(std::string* out) {
  MessageBuffer& buf = conn_->InputBuffer();
  auto [data, len] = buf.GetAllData();
  if (data == nullptr || len < 3) {
    return false;
  }
  if (data[0] == '-') {
    const char* end = static_cast<const char*>(std::memchr(data, '\n', len));
    if (end == nullptr) {
      return false;
    }
    Fail(std::string(data + 1, end - data - 1));
    buf.ReadCompleted(static_cast<std::size_t>(end - data + 1));
    return false;
  }
  if (data[0] != '+') {
    Fail("expected simple string");
    return false;
  }
  const char* end = static_cast<const char*>(std::memchr(data, '\n', len));
  if (end == nullptr || end == data || *(end - 1) != '\r') {
    return false;
  }
  if (out != nullptr) {
    out->assign(data + 1, end - data - 2);
  }
  buf.ReadCompleted(static_cast<std::size_t>(end - data + 1));
  return true;
}

bool ReplicationSlave::TryConsumeBulkHeader(int64_t* len) {
  MessageBuffer& buf = conn_->InputBuffer();
  auto [data, n] = buf.GetAllData();
  if (data == nullptr || n < 4 || data[0] != '$') {
    return false;
  }
  const char* end = static_cast<const char*>(std::memchr(data, '\n', n));
  if (end == nullptr || end == data || *(end - 1) != '\r') {
    return false;
  }
  char* parse_end = nullptr;
  const long long v = std::strtoll(data + 1, &parse_end, 10);
  if (parse_end == data + 1) {
    Fail("bad bulk length");
    return false;
  }
  if (len != nullptr) {
    *len = static_cast<int64_t>(v);
  }
  buf.ReadCompleted(static_cast<std::size_t>(end - data + 1));
  return true;
}

bool ReplicationSlave::WriteRdbChunk(const char* data, std::size_t n) {
  if (rdb_fp_ == nullptr) {
    rdb_fp_ = std::fopen(kInTmpPath, "wb");
    if (rdb_fp_ == nullptr) {
      Fail("open repl-in tmp failed");
      return false;
    }
  }
  if (n > 0 && std::fwrite(data, 1, n, rdb_fp_) != n) {
    Fail("write repl-in tmp failed");
    return false;
  }
  return true;
}

bool ReplicationSlave::FinishRdbAndLoad() {
  if (rdb_fp_ != nullptr) {
    std::fflush(rdb_fp_);
    std::fclose(rdb_fp_);
    rdb_fp_ = nullptr;
  }
  std::error_code ec;
  std::filesystem::rename(kInTmpPath, kInPath, ec);
  if (ec) {
    Fail("rename repl-in failed");
    return false;
  }
  const auto st = snapshot_->LoadFromPath(kInPath);
  if (st != SnapshotManager::Status::kOk) {
    Fail("LoadFromPath failed");
    return false;
  }
  spdlog::info("slave: loaded RDB from {}", kInPath);
  return true;
}

bool ReplicationSlave::ConsumeStreamFrames(std::string* bytes,
                                           VNodeIndex* vnode_index, KvStore* kv,
                                           std::string* err) {
  if (bytes == nullptr) {
    return false;
  }
  std::size_t off = 0;
  while (off + 4 <= bytes->size()) {
    unsigned char len_buf[4];
    std::memcpy(len_buf, bytes->data() + off, 4);
    const uint32_t plen = ReadU32Le(len_buf);
    if (off + 4 + plen > bytes->size()) {
      break;  // partial frame
    }
    off += 4;
    vemory::WalEntry entry;
    if (!entry.ParseFromArray(bytes->data() + off, static_cast<int>(plen))) {
      if (err != nullptr) {
        *err = "bad wal protobuf";
      }
      return false;
    }
    off += plen;
    const auto r = ApplyMutation(entry, MutateSource::kAofReplay, vnode_index,
                                 kv, nullptr);
    if (!r.ok) {
      if (err != nullptr) {
        *err = "apply: " + r.err;
      }
      return false;
    }
  }
  if (off > 0) {
    bytes->erase(0, off);
  }
  return true;
}

bool ReplicationSlave::ApplyBacklog(const std::string& bytes) {
  std::string buf = bytes;
  std::string err;
  if (!ConsumeStreamFrames(&buf, vnode_index_, kv_, &err)) {
    Fail(err.empty() ? "backlog apply failed" : err);
    return false;
  }
  if (!buf.empty()) {
    Fail("truncated backlog frame");
    return false;
  }
  return true;
}

bool ReplicationSlave::DrainStreaming() {
  auto [pdata, avail] = conn_->InputBuffer().GetAllData();
  if (pdata != nullptr && avail > 0) {
    stream_buf_.append(pdata, avail);
    conn_->InputBuffer().ReadCompleted(avail);
  }
  std::string err;
  if (!ConsumeStreamFrames(&stream_buf_, vnode_index_, kv_, &err)) {
    Fail(err.empty() ? "stream apply failed" : err);
    return false;
  }
  return true;
}

void ReplicationSlave::OnReadable() {
  if (!conn_ || state_ == State::kError) {
    return;
  }

  while (true) {
    MessageBuffer& buf = conn_->InputBuffer();

    if (state_ == State::kWaitFullresync) {
      std::string s;
      if (!ConsumeSimpleString(&s)) {
        return;
      }
      if (s != "FULLRESYNC") {
        Fail("unexpected reply: " + s);
        return;
      }
      state_ = State::kRecvRdbHeader;
      continue;
    }

    if (state_ == State::kRecvRdbHeader) {
      int64_t len = 0;
      if (!TryConsumeBulkHeader(&len)) {
        return;
      }
      if (len < 0) {
        Fail("null rdb bulk");
        return;
      }
      rdb_remaining_ = len;
      state_ = State::kRecvRdbBody;
      if (rdb_remaining_ == 0) {
        if (!FinishRdbAndLoad()) {
          return;
        }
        state_ = State::kRecvBacklogHeader;
      }
      continue;
    }

    if (state_ == State::kRecvRdbBody) {
      auto [pdata, avail] = buf.GetAllData();
      if (pdata == nullptr || avail == 0) {
        return;
      }
      const std::size_t take =
          avail < static_cast<std::size_t>(rdb_remaining_)
              ? avail
              : static_cast<std::size_t>(rdb_remaining_);
      if (!WriteRdbChunk(pdata, take)) {
        return;
      }
      buf.ReadCompleted(take);
      rdb_remaining_ -= static_cast<int64_t>(take);
      if (rdb_remaining_ == 0) {
        if (!FinishRdbAndLoad()) {
          return;
        }
        state_ = State::kRecvBacklogHeader;
        continue;
      }
      continue;
    }

    if (state_ == State::kRecvBacklogHeader) {
      int64_t len = 0;
      if (!TryConsumeBulkHeader(&len)) {
        return;
      }
      if (len < 0) {
        Fail("null backlog bulk");
        return;
      }
      backlog_remaining_ = len;
      backlog_buf_.clear();
      backlog_buf_.reserve(static_cast<std::size_t>(len));
      state_ = State::kRecvBacklogBody;
      if (backlog_remaining_ == 0) {
        EnterStreaming();
        spdlog::info("slave: fullsync done (empty backlog); streaming");
        continue;
      }
      continue;
    }

    if (state_ == State::kRecvBacklogBody) {
      auto [pdata, avail] = buf.GetAllData();
      if (pdata == nullptr || avail == 0) {
        return;
      }
      const std::size_t take =
          avail < static_cast<std::size_t>(backlog_remaining_)
              ? avail
              : static_cast<std::size_t>(backlog_remaining_);
      backlog_buf_.append(pdata, take);
      buf.ReadCompleted(take);
      backlog_remaining_ -= static_cast<int64_t>(take);
      if (backlog_remaining_ == 0) {
        if (!ApplyBacklog(backlog_buf_)) {
          return;
        }
        backlog_buf_.clear();
        EnterStreaming();
        spdlog::info("slave: fullsync done; streaming");
        continue;
      }
      continue;
    }

    if (state_ == State::kStreaming) {
      if (!DrainStreaming()) {
        return;
      }
      return;
    }

    return;
  }
}
