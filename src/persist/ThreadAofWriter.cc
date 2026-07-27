#include "vemory/persist/ThreadAofWriter.h"

#include <unistd.h>

#include <filesystem>
#include <utility>

#include <spdlog/spdlog.h>

namespace {

bool WriteExact(FILE* fp, const void* buf, std::size_t n) {
  return std::fwrite(buf, 1, n, fp) == n;
}

}  // namespace

ThreadAofWriter::ThreadAofWriter(std::string path, vemory::AofFsyncPolicy fsync)
    : path_(std::move(path)),
      fsync_(fsync),
      queue_(kQueueCapacity),
      last_fsync_(std::chrono::steady_clock::now()) {
  thread_ = std::thread([this] { FlushLoop(); });
}

ThreadAofWriter::~ThreadAofWriter() { Stop(); }

void ThreadAofWriter::IncPending() {
  std::lock_guard<std::mutex> lock(drain_mu_);
  ++pending_;
}

void ThreadAofWriter::DecPending() {
  DecPendingN(1);
}

void ThreadAofWriter::DecPendingN(std::size_t n) {
  std::lock_guard<std::mutex> lock(drain_mu_);
  if (n >= pending_) {
    pending_ = 0;
  } else {
    pending_ -= n;
  }
  if (pending_ == 0) {
    drain_cv_.notify_all();
  }
}

bool ThreadAofWriter::EnsureOpen() {
  if (fp_ != nullptr) {
    return true;
  }
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return false;
    }
  }
  fp_ = std::fopen(path_.c_str(), "ab+");
  return fp_ != nullptr;
}

bool ThreadAofWriter::WriteBatch(const std::string* frames, std::size_t n) {
  if (frames == nullptr || n == 0) {
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (frames[i].empty()) {
      return false;
    }
    if (!WriteExact(fp_, frames[i].data(), frames[i].size())) {
      return false;
    }
  }
  if (std::fflush(fp_) != 0) {
    return false;
  }
  dirty_ = true;
  return true;
}

bool ThreadAofWriter::SyncFile() {
  if (fsync_ == vemory::AofFsyncPolicy::kNo) {
    return true;
  }
  if (fp_ == nullptr || !dirty_) {
    return true;
  }
  const int fd = ::fileno(fp_);
  if (fd < 0) {
    return false;
  }
#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
  if (::fdatasync(fd) != 0) {
    return false;
  }
#else
  if (::fsync(fd) != 0) {
    return false;
  }
#endif
  dirty_ = false;
  last_fsync_ = std::chrono::steady_clock::now();
  return true;
}

void ThreadAofWriter::MaybeSyncAfterBatch() {
  if (fsync_ == vemory::AofFsyncPolicy::kAlways) {
    if (!SyncFile()) {
      spdlog::error("AOF fsync failed path={}", path_);
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
  if (!SyncFile()) {
    spdlog::error("AOF fsync failed path={}", path_);
    io_failed_.store(true, std::memory_order_relaxed);
  }
}

void ThreadAofWriter::FlushLoop() {
  std::string batch[kMaxBatch];
  while (true) {
    const bool got = queue_.PopWaitFor(&batch[0], std::chrono::seconds(1));
    if (!got) {
      if (queue_.cancelled()) {
        break;
      }
      if (fsync_ == vemory::AofFsyncPolicy::kEverySec) {
        std::lock_guard<std::mutex> lock(file_mu_);
        if (!io_failed_.load(std::memory_order_relaxed)) {
          if (!SyncFile()) {
            spdlog::error("AOF fsync failed path={}", path_);
            io_failed_.store(true, std::memory_order_relaxed);
            queue_.Cancel();
          }
        }
      }
      continue;
    }

    std::size_t n = 1;
    while (n < kMaxBatch &&
           queue_.PopWaitFor(&batch[n], std::chrono::milliseconds(0))) {
      ++n;
    }

    if (io_failed_.load(std::memory_order_relaxed)) {
      DecPendingN(n);
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(file_mu_);
      if (!EnsureOpen()) {
        spdlog::error("AOF open failed path={}", path_);
        io_failed_.store(true, std::memory_order_relaxed);
        DecPendingN(n);
        queue_.Cancel();
        std::string frame;
        while (queue_.Pop(&frame)) {
          DecPending();
        }
        return;
      }
      if (!WriteBatch(batch, n)) {
        spdlog::error("AOF write failed path={}", path_);
        io_failed_.store(true, std::memory_order_relaxed);
        DecPendingN(n);
        queue_.Cancel();
        std::string frame;
        while (queue_.Pop(&frame)) {
          DecPending();
        }
        return;
      }
      MaybeSyncAfterBatch();
      if (io_failed_.load(std::memory_order_relaxed)) {
        DecPendingN(n);
        queue_.Cancel();
        std::string frame;
        while (queue_.Pop(&frame)) {
          DecPending();
        }
        return;
      }
    }
    DecPendingN(n);
  }
}

bool ThreadAofWriter::Enqueue(std::string frame) {
  if (stopped_.load(std::memory_order_relaxed) ||
      io_failed_.load(std::memory_order_relaxed) || queue_.cancelled()) {
    return false;
  }
  IncPending();
  if (!queue_.Push(std::move(frame))) {
    DecPending();
    return false;
  }
  return true;
}

bool ThreadAofWriter::Flush() {
  {
    std::unique_lock<std::mutex> lock(drain_mu_);
    drain_cv_.wait(lock, [this] { return pending_ == 0; });
  }
  if (io_failed_.load(std::memory_order_relaxed)) {
    return false;
  }
  if (fsync_ != vemory::AofFsyncPolicy::kNo) {
    std::lock_guard<std::mutex> lock(file_mu_);
    if (fp_ != nullptr && dirty_) {
      if (!SyncFile()) {
        io_failed_.store(true, std::memory_order_relaxed);
        return false;
      }
    }
  }
  return !io_failed_.load(std::memory_order_relaxed);
}

void ThreadAofWriter::Stop() {
  bool expected = false;
  if (!stopped_.compare_exchange_strong(expected, true)) {
    return;
  }
  Flush();
  queue_.Cancel();
  if (thread_.joinable()) {
    thread_.join();
  }
  std::lock_guard<std::mutex> lock(file_mu_);
  if (fp_ != nullptr) {
    std::fclose(fp_);
    fp_ = nullptr;
  }
}
