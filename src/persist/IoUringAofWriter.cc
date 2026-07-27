#include "vemory/persist/IoUringAofWriter.h"

#include <spdlog/spdlog.h>

#if VEMORY_HAVE_LIBURING

#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

class InlineIoUringAofWriter final : public AofWriter {
 public:
  static constexpr std::size_t kSoftFlush = 4u * 1024u;
  static constexpr std::size_t kHardCap = 1u * 1024u * 1024u;
  static constexpr std::size_t kMaxPending = 64;
  static constexpr unsigned kRingEntries = 32;
  static constexpr int kWaitCqeRetries = 64;

  static std::unique_ptr<AofWriter> TryCreate(std::string path,
                                             vemory::AofFsyncPolicy fsync,
                                             int flush_interval_ms) {
    auto w = std::unique_ptr<InlineIoUringAofWriter>(new InlineIoUringAofWriter(
        std::move(path), fsync, flush_interval_ms));
    if (!w->Init()) {
      return nullptr;
    }
    return w;
  }

  ~InlineIoUringAofWriter() override { Stop(); }

  InlineIoUringAofWriter(const InlineIoUringAofWriter&) = delete;
  InlineIoUringAofWriter& operator=(const InlineIoUringAofWriter&) = delete;

  bool Enqueue(std::string frame) override {
    if (stopped_ || io_failed_) {
      return false;
    }
    if (frame.empty()) {
      return false;
    }

    if (frame.size() > kHardCap) {
      if (!aof_buf_.empty() && !FlushBuffer()) {
        return false;
      }
      std::size_t off = 0;
      while (off < frame.size()) {
        const std::size_t n = std::min(kSoftFlush, frame.size() - off);
        aof_buf_.append(frame.data() + off, n);
        off += n;
        if (!FlushBuffer()) {
          return false;
        }
      }
      if (fsync_ == vemory::AofFsyncPolicy::kAlways) {
        return SyncFile();
      }
      return true;
    }

    if (aof_buf_.size() + frame.size() > kHardCap) {
      if (!FlushBuffer()) {
        return false;
      }
      if (aof_buf_.size() + frame.size() > kHardCap) {
        if (!WaitForPendingSlot()) {
          return false;
        }
        if (!FlushBuffer()) {
          return false;
        }
        if (aof_buf_.size() + frame.size() > kHardCap) {
          return false;
        }
      }
    }

    aof_buf_.append(frame);
    if (aof_buf_.size() >= kSoftFlush) {
      if (!FlushBuffer()) {
        return false;
      }
    }
    if (fsync_ == vemory::AofFsyncPolicy::kAlways) {
      if (!aof_buf_.empty() && !FlushBuffer()) {
        return false;
      }
      return SyncFile();
    }
    return true;
  }

  bool Flush() override {
    if (io_failed_) {
      return false;
    }
    if (!aof_buf_.empty() && !FlushBuffer()) {
      return false;
    }
    if (!DrainPending(/*max_retries=*/200)) {
      io_failed_ = true;
      return false;
    }
    if (fsync_ != vemory::AofFsyncPolicy::kNo) {
      if (!SyncFile()) {
        io_failed_ = true;
        return false;
      }
    }
    return !io_failed_;
  }

  void Stop() override {
    if (stopped_) {
      return;
    }
    stopped_ = true;
    (void)Flush();
    if (ring_inited_) {
      io_uring_queue_exit(&ring_);
      ring_inited_ = false;
    }
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    for (auto& pw : pending_) {
      pw.buf.clear();
    }
    pending_.clear();
  }

  bool failed() const override { return io_failed_; }

  void Poll() override {
    if (stopped_ || io_failed_ || !ring_inited_) {
      return;
    }
    PeekCompletions();

    const auto now = std::chrono::steady_clock::now();
    if (!aof_buf_.empty() &&
        now - last_flush_ >=
            std::chrono::milliseconds(flush_interval_ms_)) {
      (void)FlushBuffer();
    }

    if (fsync_ == vemory::AofFsyncPolicy::kEverySec && dirty_ &&
        now - last_fsync_ >= std::chrono::seconds(1)) {
      if (!aof_buf_.empty()) {
        (void)FlushBuffer();
      }
      if (!pending_.empty()) {
        (void)DrainPending(/*max_retries=*/50);
      }
      if (dirty_ && !SyncFile()) {
        spdlog::error("AOF fsync failed path={}", path_);
        io_failed_ = true;
      }
    }
  }

 private:
  struct PendingWrite {
    std::string buf;
    std::size_t bytes_total = 0;
    std::size_t bytes_written = 0;
    off_t offset = 0;
    uint64_t user_data = 0;
  };

  InlineIoUringAofWriter(std::string path, vemory::AofFsyncPolicy fsync,
                         int flush_interval_ms)
      : path_(std::move(path)),
        fsync_(fsync),
        flush_interval_ms_(flush_interval_ms < 1 ? 1000 : flush_interval_ms),
        last_flush_(std::chrono::steady_clock::now()),
        last_fsync_(std::chrono::steady_clock::now()) {
    aof_buf_.reserve(kSoftFlush);
  }

  bool Init() {
    std::error_code ec;
    const auto parent = std::filesystem::path(path_).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        spdlog::warn("AOF io_uring: mkdir failed path={}: {}", path_,
                     ec.message());
        return false;
      }
    }
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
      spdlog::warn("AOF io_uring: open failed path={}: errno={}", path_, errno);
      return false;
    }
    aof_offset_ = ::lseek(fd_, 0, SEEK_END);
    if (aof_offset_ < 0) {
      spdlog::warn("AOF io_uring: lseek failed path={}: errno={}", path_,
                   errno);
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    std::memset(&ring_, 0, sizeof(ring_));
    if (io_uring_queue_init(kRingEntries, &ring_, 0) < 0) {
      spdlog::warn("AOF io_uring: queue_init failed path={}: errno={}", path_,
                   errno);
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    ring_inited_ = true;
    return true;
  }

  bool SyncFile() {
    if (fsync_ == vemory::AofFsyncPolicy::kNo) {
      return true;
    }
    if (fd_ < 0 || !dirty_) {
      return true;
    }
#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
    if (::fdatasync(fd_) != 0) {
      return false;
    }
#else
    if (::fsync(fd_) != 0) {
      return false;
    }
#endif
    dirty_ = false;
    last_fsync_ = std::chrono::steady_clock::now();
    return true;
  }

  int FindPending(uint64_t user_data) const {
    for (std::size_t i = 0; i < pending_.size(); ++i) {
      if (pending_[i].user_data == user_data) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void RemovePending(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= pending_.size()) {
      return;
    }
    pending_.erase(pending_.begin() + index);
  }

  void PeekCompletions() {
    if (!ring_inited_) {
      return;
    }
    while (true) {
      struct io_uring_cqe* cqe = nullptr;
      if (io_uring_peek_cqe(&ring_, &cqe) != 0 || cqe == nullptr) {
        break;
      }
      const int idx = FindPending(cqe->user_data);
      if (idx < 0) {
        spdlog::warn("AOF io_uring: CQE for unknown user_data={}",
                     cqe->user_data);
        io_uring_cqe_seen(&ring_, cqe);
        continue;
      }
      PendingWrite& pw = pending_[static_cast<std::size_t>(idx)];
      if (cqe->res < 0) {
        spdlog::error("AOF io_uring write error path={} err={}", path_,
                      -cqe->res);
        io_failed_ = true;
        RemovePending(idx);
        io_uring_cqe_seen(&ring_, cqe);
        continue;
      }
      if (cqe->res == 0) {
        spdlog::warn("AOF io_uring zero-byte write path={}", path_);
        RemovePending(idx);
        io_uring_cqe_seen(&ring_, cqe);
        continue;
      }
      const auto wrote = static_cast<std::size_t>(cqe->res);
      if (wrote < pw.bytes_total - pw.bytes_written) {
        pw.bytes_written += wrote;
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr) {
          io_uring_submit(&ring_);
          sqe = io_uring_get_sqe(&ring_);
        }
        if (sqe != nullptr) {
          const std::size_t rem = pw.bytes_total - pw.bytes_written;
          io_uring_prep_write(sqe, fd_, pw.buf.data() + pw.bytes_written,
                              rem, pw.offset + static_cast<off_t>(pw.bytes_written));
          sqe->user_data = pw.user_data;
          if (io_uring_submit(&ring_) < 0) {
            io_failed_ = true;
            RemovePending(idx);
          }
        } else {
          io_failed_ = true;
          RemovePending(idx);
        }
        io_uring_cqe_seen(&ring_, cqe);
        continue;
      }
      dirty_ = true;
      RemovePending(idx);
      io_uring_cqe_seen(&ring_, cqe);
    }
  }

  bool WaitForPendingSlot() {
    for (int i = 0; i < kWaitCqeRetries; ++i) {
      PeekCompletions();
      if (pending_.size() < kMaxPending) {
        return true;
      }
      struct io_uring_cqe* cqe = nullptr;
      if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
        return false;
      }
      // Leave CQE for PeekCompletions.
      PeekCompletions();
      if (pending_.size() < kMaxPending) {
        return true;
      }
    }
    return false;
  }

  bool DrainPending(int max_retries) {
    int retries = 0;
    while (!pending_.empty() && (max_retries < 0 || retries < max_retries)) {
      PeekCompletions();
      if (!pending_.empty()) {
        struct io_uring_cqe* cqe = nullptr;
        if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
          return false;
        }
        PeekCompletions();
        ++retries;
      }
    }
    return pending_.empty();
  }

  bool FlushBuffer() {
    if (fd_ < 0 || !ring_inited_ || aof_buf_.empty()) {
      return aof_buf_.empty() || !io_failed_;
    }
    if (io_failed_) {
      return false;
    }

    if (pending_.size() >= kMaxPending) {
      if (!WaitForPendingSlot()) {
        return false;
      }
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
      if (io_uring_submit(&ring_) < 0) {
        return false;
      }
      sqe = io_uring_get_sqe(&ring_);
      if (sqe == nullptr) {
        if (!WaitForPendingSlot()) {
          return false;
        }
        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr) {
          return false;
        }
      }
    }

    PendingWrite pw;
    pw.buf = std::move(aof_buf_);
    aof_buf_.clear();
    aof_buf_.reserve(kSoftFlush);
    pw.bytes_total = pw.buf.size();
    pw.bytes_written = 0;
    pw.offset = aof_offset_;
    pw.user_data = next_user_data_++;
    aof_offset_ += static_cast<off_t>(pw.bytes_total);

    io_uring_prep_write(sqe, fd_, pw.buf.data(), pw.bytes_total, pw.offset);
    sqe->user_data = pw.user_data;
    pending_.push_back(std::move(pw));

    if (io_uring_submit(&ring_) < 0) {
      // Roll back: restore buffer from last pending.
      PendingWrite& last = pending_.back();
      aof_offset_ -= static_cast<off_t>(last.bytes_total);
      aof_buf_ = std::move(last.buf);
      pending_.pop_back();
      return false;
    }

    last_flush_ = std::chrono::steady_clock::now();
    return true;
  }

  std::string path_;
  vemory::AofFsyncPolicy fsync_;
  int flush_interval_ms_ = 1000;
  int fd_ = -1;
  off_t aof_offset_ = 0;
  struct io_uring ring_{};
  bool ring_inited_ = false;

  std::string aof_buf_;
  std::vector<PendingWrite> pending_;
  uint64_t next_user_data_ = 1;

  bool dirty_ = false;
  bool io_failed_ = false;
  bool stopped_ = false;
  std::chrono::steady_clock::time_point last_flush_;
  std::chrono::steady_clock::time_point last_fsync_;
};

}  // namespace

std::unique_ptr<AofWriter> TryMakeIoUringAofWriter(std::string path,
                                                  vemory::AofFsyncPolicy fsync,
                                                  int flush_interval_ms) {
  return InlineIoUringAofWriter::TryCreate(std::move(path), fsync,
                                           flush_interval_ms);
}

#else  // !VEMORY_HAVE_LIBURING

std::unique_ptr<AofWriter> TryMakeIoUringAofWriter(std::string /*path*/,
                                                  vemory::AofFsyncPolicy /*fsync*/,
                                                  int /*flush_interval_ms*/) {
  return nullptr;
}

#endif
