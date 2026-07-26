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

bool ReplicationSlave::EnsureTmpDir() {
  std::error_code ec;
  std::filesystem::create_directories(kTmpDir, ec);
  return !ec;
}

bool ReplicationSlave::Start(const std::string& host, uint16_t port) {
  if (loop_ == nullptr || snapshot_ == nullptr) {
    return false;
  }
  if (!EnsureTmpDir()) {
    spdlog::error("slave: cannot create {}", kTmpDir);
    return false;
  }

  const int fd = TcpConnector::Connect(host, port);
  if (fd < 0) {
    spdlog::error("slave: connect {}:{} failed", host, port);
    return false;
  }

  conn_ = std::make_shared<TcpConn>(fd, *loop_);
  state_ = State::kWaitFullresync;

  const char psync[] = "*1\r\n$5\r\nPSYNC\r\n";
  conn_->Send(psync, sizeof(psync) - 1);
  conn_->SetReadCallback([this]() { OnReadable(); });
  conn_->SetCloseCallback([this]() {
    if (state_ != State::kDone) {
      Fail("connection closed");
    }
  });

  spdlog::info("slave: sent PSYNC to {}:{}", host, port);
  return true;
}

void ReplicationSlave::Fail(const std::string& msg) {
  state_ = State::kError;
  spdlog::error("slave: {}", msg);
  if (rdb_fp_ != nullptr) {
    std::fclose(rdb_fp_);
    rdb_fp_ = nullptr;
  }
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

bool ReplicationSlave::ApplyBacklog(const std::string& bytes) {
  std::size_t off = 0;
  while (off + 4 <= bytes.size()) {
    unsigned char len_buf[4];
    std::memcpy(len_buf, bytes.data() + off, 4);
    const uint32_t plen = ReadU32Le(len_buf);
    off += 4;
    if (off + plen > bytes.size()) {
      Fail("truncated backlog frame");
      return false;
    }
    vemory::WalEntry entry;
    if (!entry.ParseFromArray(bytes.data() + off, static_cast<int>(plen))) {
      Fail("bad backlog protobuf");
      return false;
    }
    off += plen;
    const auto r = ApplyMutation(entry, MutateSource::kAofReplay, vnode_index_,
                                 kv_, nullptr);
    if (!r.ok) {
      Fail("backlog apply: " + r.err);
      return false;
    }
  }
  if (off != bytes.size()) {
    Fail("backlog trailing garbage");
    return false;
  }
  return true;
}

void ReplicationSlave::OnReadable() {
  if (!conn_ || state_ == State::kError || state_ == State::kDone) {
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
        state_ = State::kDone;
        spdlog::info("slave: fullsync done (empty backlog)");
        return;
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
        state_ = State::kDone;
        spdlog::info("slave: fullsync done backlog_bytes={}",
                     backlog_buf_.size());
        return;
      }
      continue;
    }

    return;
  }
}
