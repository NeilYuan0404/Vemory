#include "vemory/persist/WalManager.h"

#include <unistd.h>

#include <filesystem>
#include <utility>

#include <spdlog/spdlog.h>

#include "vemory/persist/MutationApply.h"

namespace {

bool WriteExact(FILE* fp, const void* buf, std::size_t n) {
  return std::fwrite(buf, 1, n, fp) == n;
}

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
                       bool enable, vemory::AofFsyncPolicy fsync)
    : vnode_index_(vnode_index),
      kv_(kv),
      dir_(std::move(dir)),
      enabled_(enable && !dir_.empty()),
      fsync_(fsync),
      queue_(kQueueCapacity),
      last_fsync_(std::chrono::steady_clock::now()) {
  if (enabled_) {
    path_ = dir_ + "/" + kFileName;
    flush_thread_ = std::thread([this] { FlushLoop(); });
  }
}

WalManager::~WalManager() {
  if (enabled_) {
    Flush();
    queue_.Cancel();
    if (flush_thread_.joinable()) {
      flush_thread_.join();
    }
  }
  std::lock_guard<std::mutex> lock(file_mu_);
  if (fp_ != nullptr) {
    std::fclose(fp_);
    fp_ = nullptr;
  }
}

bool WalManager::ReadExact(FILE* fp, void* buf, std::size_t n) {
  return ::ReadExact(fp, buf, n);
}

bool WalManager::WriteExact(FILE* fp, const void* buf, std::size_t n) {
  return ::WriteExact(fp, buf, n);
}

void WalManager::WriteU32Le(unsigned char out[4], uint32_t v) {
  ::WriteU32Le(out, v);
}

uint32_t WalManager::ReadU32Le(const unsigned char in[4]) {
  return ::ReadU32Le(in);
}

void WalManager::IncPending() {
  std::lock_guard<std::mutex> lock(drain_mu_);
  ++pending_;
}

void WalManager::DecPending() {
  std::lock_guard<std::mutex> lock(drain_mu_);
  if (pending_ > 0) {
    --pending_;
  }
  if (pending_ == 0) {
    drain_cv_.notify_all();
  }
}

WalManager::Status WalManager::EnsureOpenForAppend() {
  if (!enabled_) {
    return Status::kNotConfigured;
  }
  if (fp_ != nullptr) {
    return Status::kOk;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    return Status::kIoError;
  }
  fp_ = std::fopen(path_.c_str(), "ab+");
  if (fp_ == nullptr) {
    return Status::kIoError;
  }
  return Status::kOk;
}

WalManager::Status WalManager::WriteFrame(const std::string& frame) {
  if (frame.empty()) {
    return Status::kError;
  }
  if (!WriteExact(fp_, frame.data(), frame.size())) {
    return Status::kIoError;
  }
  if (std::fflush(fp_) != 0) {
    return Status::kIoError;
  }
  dirty_ = true;
  return Status::kOk;
}

WalManager::Status WalManager::SyncFile() {
  if (fsync_ == vemory::AofFsyncPolicy::kNo) {
    return Status::kOk;
  }
  if (fp_ == nullptr || !dirty_) {
    return Status::kOk;
  }
  const int fd = ::fileno(fp_);
  if (fd < 0) {
    return Status::kIoError;
  }
#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
  if (::fdatasync(fd) != 0) {
    return Status::kIoError;
  }
#else
  if (::fsync(fd) != 0) {
    return Status::kIoError;
  }
#endif
  dirty_ = false;
  last_fsync_ = std::chrono::steady_clock::now();
  return Status::kOk;
}

void WalManager::MaybeSyncAfterWrite() {
  if (fsync_ == vemory::AofFsyncPolicy::kAlways) {
    const auto st = SyncFile();
    if (st != Status::kOk) {
      spdlog::error("AOF fsync failed path={} status={}", path_,
                    static_cast<int>(st));
      io_failed_.store(true, std::memory_order_relaxed);
    }
    return;
  }
  if (fsync_ != vemory::AofFsyncPolicy::kEverySec) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - last_fsync_ < std::chrono::seconds(1)) {
    return;
  }
  const auto st = SyncFile();
  if (st != Status::kOk) {
    spdlog::error("AOF fsync failed path={} status={}", path_,
                  static_cast<int>(st));
    io_failed_.store(true, std::memory_order_relaxed);
  }
}

void WalManager::FlushLoop() {
  std::string frame;
  while (true) {
    const bool got =
        queue_.PopWaitFor(&frame, std::chrono::seconds(1));
    if (!got) {
      if (queue_.cancelled()) {
        break;
      }
      // Idle timeout: sync dirty tail under everysec.
      if (fsync_ == vemory::AofFsyncPolicy::kEverySec) {
        std::lock_guard<std::mutex> lock(file_mu_);
        if (!io_failed_.load(std::memory_order_relaxed)) {
          const auto st = SyncFile();
          if (st != Status::kOk) {
            spdlog::error("AOF fsync failed path={} status={}", path_,
                          static_cast<int>(st));
            io_failed_.store(true, std::memory_order_relaxed);
            queue_.Cancel();
          }
        }
      }
      continue;
    }

    if (io_failed_.load(std::memory_order_relaxed)) {
      DecPending();
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(file_mu_);
      const auto open_st = EnsureOpenForAppend();
      if (open_st != Status::kOk) {
        spdlog::error("AOF open failed path={} status={}", path_,
                      static_cast<int>(open_st));
        io_failed_.store(true, std::memory_order_relaxed);
        DecPending();
        queue_.Cancel();
        while (queue_.Pop(&frame)) {
          DecPending();
        }
        return;
      }
      const auto st = WriteFrame(frame);
      if (st != Status::kOk) {
        spdlog::error("AOF write failed path={} status={}", path_,
                      static_cast<int>(st));
        io_failed_.store(true, std::memory_order_relaxed);
        DecPending();
        queue_.Cancel();
        while (queue_.Pop(&frame)) {
          DecPending();
        }
        return;
      }
      MaybeSyncAfterWrite();
      if (io_failed_.load(std::memory_order_relaxed)) {
        DecPending();
        queue_.Cancel();
        while (queue_.Pop(&frame)) {
          DecPending();
        }
        return;
      }
    }
    DecPending();
  }
}

WalManager::Status WalManager::Append(const vemory::WalEntry& entry) {
  if (!enabled_) {
    return Status::kNotConfigured;
  }
  if (io_failed_.load(std::memory_order_relaxed)) {
    return Status::kIoError;
  }
  if (queue_.cancelled()) {
    return Status::kError;
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

  IncPending();
  if (!queue_.Push(std::move(frame))) {
    DecPending();
    return Status::kError;
  }
  return Status::kOk;
}

WalManager::Status WalManager::Flush() {
  if (!enabled_) {
    return Status::kOk;
  }
  {
    std::unique_lock<std::mutex> lock(drain_mu_);
    drain_cv_.wait(lock, [this] { return pending_ == 0; });
  }
  if (io_failed_.load(std::memory_order_relaxed)) {
    return Status::kIoError;
  }
  if (fsync_ != vemory::AofFsyncPolicy::kNo) {
    std::lock_guard<std::mutex> lock(file_mu_);
    // Force a durable sync of any written-but-not-yet-fsynced data.
    if (fp_ != nullptr && dirty_) {
      const auto st = SyncFile();
      if (st != Status::kOk) {
        io_failed_.store(true, std::memory_order_relaxed);
        return st;
      }
    }
  }
  if (io_failed_.load(std::memory_order_relaxed)) {
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
