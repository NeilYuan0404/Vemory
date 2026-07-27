#include "vemory/persist/WalManager.h"

#include <cstdio>
#include <vector>

#include <spdlog/spdlog.h>

#include "vemory/mutate/MutationApply.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/resp/RespDecode.h"

WalManager::WalManager(VNodeIndex* vnode_index, KvStore* kv, std::string dir,
                       bool enable, vemory::AofFsyncPolicy fsync,
                       vemory::AofIoMode io_mode, int flush_interval_ms)
    : vnode_index_(vnode_index),
      kv_(kv),
      dir_(std::move(dir)),
      enabled_(enable && !dir_.empty()),
      fsync_(fsync),
      io_mode_(io_mode) {
  if (enabled_) {
    path_ = dir_ + "/" + kFileName;
    writer_ = MakeAofWriter(path_, fsync_, io_mode_, flush_interval_ms);
  }
}

WalManager::~WalManager() {
  if (writer_ != nullptr) {
    writer_->Stop();
    writer_.reset();
  }
}

WalManager::Status WalManager::AppendFrame(std::string frame) {
  if (!enabled_ || writer_ == nullptr) {
    return Status::kNotConfigured;
  }
  if (writer_->failed()) {
    return Status::kIoError;
  }
  if (frame.empty()) {
    return Status::kError;
  }
  if (!writer_->Enqueue(std::move(frame))) {
    return writer_->failed() ? Status::kIoError : Status::kError;
  }
  return Status::kOk;
}

void WalManager::Poll() {
  if (writer_ != nullptr) {
    writer_->Poll();
  }
}

WalManager::Status WalManager::Flush() {
  if (!enabled_ || writer_ == nullptr) {
    return Status::kOk;
  }
  if (!writer_->Flush()) {
    return Status::kIoError;
  }
  return Status::kOk;
}

WalManager::Status WalManager::Replay() {
  if (!enabled_) {
    return Status::kNotConfigured;
  }
  if (vnode_index_ == nullptr || kv_ == nullptr) {
    return Status::kError;
  }

  FILE* fp = std::fopen(path_.c_str(), "rb");
  if (fp == nullptr) {
    return Status::kOk;  // missing file → empty
  }

  std::string buf;
  char chunk[64 * 1024];
  while (true) {
    const std::size_t n = std::fread(chunk, 1, sizeof(chunk), fp);
    if (n > 0) {
      buf.append(chunk, n);
    }
    if (n < sizeof(chunk)) {
      break;
    }
  }
  std::fclose(fp);

  std::size_t applied = 0;
  std::size_t off = 0;
  std::vector<std::string_view> tokens;
  while (off < buf.size()) {
    std::size_t consumed = 0;
    tokens.clear();
    const auto st = RespDecode::DecodeArrayOfBulk(buf.data() + off,
                                                  buf.size() - off, &tokens,
                                                  &consumed);
    if (st == RespDecode::Status::kNeedMore) {
      if (off > 0) {
        spdlog::warn("AOF truncated RESP at {} (applied={})", path_, applied);
      }
      break;
    }
    if (st != RespDecode::Status::kOk) {
      spdlog::warn("AOF corrupt RESP at {} (applied={})", path_, applied);
      break;
    }

    RequestContext ctx;
    const auto fs =
        RequestContext::FromTokens(/*client_fd=*/-1, tokens, &ctx);
    if (fs != RequestContext::Status::kOk) {
      spdlog::warn("AOF bad command at {} (applied={})", path_, applied);
      break;
    }

    const auto ar =
        ApplyMutation(ctx, MutateSource::kAofReplay, vnode_index_, kv_,
                      /*wal=*/nullptr);
    if (!ar.ok) {
      spdlog::error("AOF replay failed at {}: {}", path_, ar.err);
      return Status::kError;
    }
    ++applied;
    off += consumed;
  }

  spdlog::info("AOF replayed {} entries from {}", applied, path_);
  return Status::kOk;
}
