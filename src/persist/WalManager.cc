#include "vemory/persist/WalManager.h"

#include <cstdio>

#include <spdlog/spdlog.h>

#include "vemory/mutate/MutationApply.h"

namespace {

bool ReadExact(FILE* fp, void* buf, std::size_t n) {
  return std::fread(buf, 1, n, fp) == n;
}

void WriteU32Le(unsigned char out[4], uint32_t v) {
  out[0] = static_cast<unsigned char>(v & 0xffu);
  out[1] = static_cast<unsigned char>((v >> 8) & 0xffu);
  out[2] = static_cast<unsigned char>((v >> 16) & 0xffu);
  out[3] = static_cast<unsigned char>((v >> 24) & 0xffu);
}

uint32_t ReadU32Le(const unsigned char in[4]) {
  return static_cast<uint32_t>(in[0]) |
         (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

}  // namespace

WalManager::WalManager(VNodeIndex* vnode_index, KvStore* kv, std::string dir,
                       bool enable, vemory::AofFsyncPolicy fsync,
                       vemory::AofIoMode io_mode)
    : vnode_index_(vnode_index),
      kv_(kv),
      dir_(std::move(dir)),
      enabled_(enable && !dir_.empty()),
      fsync_(fsync),
      io_mode_(io_mode) {
  if (enabled_) {
    path_ = dir_ + "/" + kFileName;
    writer_ = MakeAofWriter(path_, fsync_, io_mode_);
  }
}

WalManager::~WalManager() {
  if (writer_ != nullptr) {
    writer_->Stop();
    writer_.reset();
  }
}

void WalManager::WriteU32Le(unsigned char out[4], uint32_t v) {
  ::WriteU32Le(out, v);
}

uint32_t WalManager::ReadU32Le(const unsigned char in[4]) {
  return ::ReadU32Le(in);
}

bool WalManager::ReadExact(FILE* fp, void* buf, std::size_t n) {
  return ::ReadExact(fp, buf, n);
}

WalManager::Status WalManager::Append(const vemory::WalEntry& entry) {
  if (!enabled_ || writer_ == nullptr) {
    return Status::kNotConfigured;
  }
  if (writer_->failed()) {
    return Status::kIoError;
  }

  std::string payload;
  if (!entry.SerializeToString(&payload)) {
    return Status::kError;
  }
  if (payload.size() > 0xffffffffu) {
    return Status::kError;
  }

  std::string frame;
  frame.resize(4);
  WriteU32Le(reinterpret_cast<unsigned char*>(&frame[0]),
             static_cast<uint32_t>(payload.size()));
  frame.append(payload);

  if (!writer_->Enqueue(std::move(frame))) {
    return writer_->failed() ? Status::kIoError : Status::kError;
  }
  return Status::kOk;
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

  std::size_t applied = 0;
  while (true) {
    unsigned char len_buf[4];
    if (!ReadExact(fp, len_buf, 4)) {
      break;  // EOF or truncated header
    }
    const uint32_t len = ReadU32Le(len_buf);
    if (len == 0) {
      break;
    }
    std::string payload(len, '\0');
    if (!ReadExact(fp, payload.data(), len)) {
      spdlog::warn("AOF truncated payload at {} (applied={})", path_, applied);
      break;
    }
    vemory::WalEntry entry;
    if (!entry.ParseFromString(payload)) {
      spdlog::warn("AOF corrupt protobuf at {} (applied={})", path_, applied);
      break;
    }
    const auto ar =
        ApplyMutation(entry, MutateSource::kAofReplay, vnode_index_, kv_,
                      /*wal=*/nullptr);
    if (!ar.ok) {
      std::fclose(fp);
      spdlog::error("AOF replay failed at {}: {}", path_, ar.err);
      return Status::kError;
    }
    ++applied;
  }

  std::fclose(fp);
  spdlog::info("AOF replayed {} entries from {}", applied, path_);
  return Status::kOk;
}
