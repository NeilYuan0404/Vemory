#include "vemory/persist/IoUringAofWriter.h"

#include <spdlog/spdlog.h>

#if VEMORY_HAVE_LIBURING

#include <fcntl.h>
#include <liburing.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

#include "vemory/util/ringbuffer.h"

namespace {

class IoUringAofWriter final : public AofWriter {
 public:
  static std::unique_ptr<AofWriter> TryCreate(std::string path,
                                             vemory::AofFsyncPolicy fsync) {
    auto w = std::unique_ptr<IoUringAofWriter>(
        new IoUringAofWriter(std::move(path), fsync));
    if (!w->Init()) {
      return nullptr;
    }
    w->thread_ = std::thread([raw = w.get()] { raw->FlushLoop(); });
    return w;
  }

  ~IoUringAofWriter() override { Stop(); }

  IoUringAofWriter(const IoUringAofWriter&) = delete;
  IoUringAofWriter& operator=(const IoUringAofWriter&) = delete;

  bool Enqueue(std::string frame) override {
    if (stopped_.load(std::memory_order_relaxed) ||
        io_failed_.load(std::memory_order_relaxed) || queue_.cancelled()) {
      return false;
    }
    IncPending();
    if (!queue_.PushWait(std::move(frame))) {
      DecPending();
      return false;
    }
    return true;
  }

  bool Flush() override {
    {
      std::unique_lock<std::mutex> lock(drain_mu_);
      drain_cv_.wait(lock, [this] { return pending_ == 0; });
    }
    if (io_failed_.load(std::memory_order_relaxed)) {
      return false;
    }
    if (fsync_ != vemory::AofFsyncPolicy::kNo) {
      std::lock_guard<std::mutex> lock(file_mu_);
      if (fd_ >= 0 && dirty_) {
        if (!SyncFile()) {
          io_failed_.store(true, std::memory_order_relaxed);
          return false;
        }
      }
    }
    return !io_failed_.load(std::memory_order_relaxed);
  }

  void Stop() override {
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
    if (ring_inited_) {
      io_uring_queue_exit(&ring_);
      ring_inited_ = false;
    }
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool failed() const override {
    return io_failed_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr std::size_t kQueueCapacity = 1024;
  static constexpr std::size_t kMaxBatch = 32;
  static constexpr std::size_t kMaxInFlight = 8;
  static constexpr unsigned kRingEntries = 32;

  struct PendingOp {
    std::string frames[kMaxBatch];
    struct iovec iov[kMaxBatch];
    std::size_t n = 0;
    std::size_t total = 0;
    bool in_use = false;
  };

  IoUringAofWriter(std::string path, vemory::AofFsyncPolicy fsync)
      : path_(std::move(path)),
        fsync_(fsync),
        last_fsync_(std::chrono::steady_clock::now()) {}

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

  void IncPending() {
    std::lock_guard<std::mutex> lock(drain_mu_);
    ++pending_;
  }

  void DecPending() { DecPendingN(1); }

  void DecPendingN(std::size_t n) {
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

  PendingOp* AcquireOp() {
    for (std::size_t i = 0; i < kMaxInFlight; ++i) {
      if (!ops_[i].in_use) {
        ops_[i].in_use = true;
        ops_[i].n = 0;
        ops_[i].total = 0;
        return &ops_[i];
      }
    }
    return nullptr;
  }

  void ReleaseOp(PendingOp* op) {
    if (op == nullptr) {
      return;
    }
    for (std::size_t i = 0; i < op->n; ++i) {
      op->frames[i].clear();
    }
    op->n = 0;
    op->total = 0;
    op->in_use = false;
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

  // Non-blocking CQE reclaim. Returns false on IO/fsync failure.
  bool ReclaimCompletions() {
    while (true) {
      struct io_uring_cqe* cqe = nullptr;
      if (io_uring_peek_cqe(&ring_, &cqe) != 0 || cqe == nullptr) {
        break;
      }
      auto* op = static_cast<PendingOp*>(io_uring_cqe_get_data(cqe));
      const int res = cqe->res;
      io_uring_cqe_seen(&ring_, cqe);
      if (in_flight_ > 0) {
        --in_flight_;
      }

      if (op == nullptr || res < 0 ||
          static_cast<std::size_t>(res) != op->total) {
        spdlog::error("AOF io_uring write failed path={} res={}", path_, res);
        if (op != nullptr) {
          DecPendingN(op->n);
          ReleaseOp(op);
        }
        return false;
      }

      {
        std::lock_guard<std::mutex> lock(file_mu_);
        dirty_ = true;
      }
      DecPendingN(op->n);
      ReleaseOp(op);

      if (fsync_ == vemory::AofFsyncPolicy::kAlways) {
        std::lock_guard<std::mutex> lock(file_mu_);
        if (!SyncFile()) {
          spdlog::error("AOF fsync failed path={}", path_);
          return false;
        }
      }
    }

    if (fsync_ == vemory::AofFsyncPolicy::kEverySec) {
      std::lock_guard<std::mutex> lock(file_mu_);
      const auto now = std::chrono::steady_clock::now();
      if (dirty_ && now - last_fsync_ >= std::chrono::seconds(1)) {
        if (!SyncFile()) {
          spdlog::error("AOF fsync failed path={}", path_);
          return false;
        }
      }
    }
    return true;
  }

  bool WaitOneCqe() {
    struct io_uring_cqe* cqe = nullptr;
    if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
      spdlog::error("AOF io_uring wait_cqe failed path={}", path_);
      return false;
    }
    // Leave CQE for ReclaimCompletions (peek + seen).
    return true;
  }

  void DrainInFlight() {
    while (in_flight_ > 0) {
      struct io_uring_cqe* cqe = nullptr;
      if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
        break;
      }
      auto* op = static_cast<PendingOp*>(io_uring_cqe_get_data(cqe));
      io_uring_cqe_seen(&ring_, cqe);
      --in_flight_;
      if (op != nullptr) {
        DecPendingN(op->n);
        ReleaseOp(op);
      }
    }
  }

  void FailAndDrain() {
    io_failed_.store(true, std::memory_order_relaxed);
    queue_.Cancel();
    DrainInFlight();
    std::string frame;
    while (queue_.Pop(frame)) {
      DecPending();
    }
  }

  bool SubmitOp(PendingOp* op) {
    if (op == nullptr || op->n == 0 || fd_ < 0 || !ring_inited_) {
      return false;
    }
    op->total = 0;
    for (std::size_t i = 0; i < op->n; ++i) {
      if (op->frames[i].empty()) {
        return false;
      }
      op->iov[i].iov_base = const_cast<char*>(op->frames[i].data());
      op->iov[i].iov_len = op->frames[i].size();
      op->total += op->frames[i].size();
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    while (sqe == nullptr) {
      if (!ReclaimCompletions()) {
        return false;
      }
      sqe = io_uring_get_sqe(&ring_);
      if (sqe != nullptr) {
        break;
      }
      if (in_flight_ == 0) {
        return false;
      }
      if (!WaitOneCqe()) {
        return false;
      }
    }

    io_uring_prep_writev(sqe, fd_, op->iov, static_cast<unsigned>(op->n), 0);
    io_uring_sqe_set_data(sqe, op);
    if (io_uring_submit(&ring_) < 0) {
      return false;
    }
    ++in_flight_;
    return true;
  }

  void FlushLoop() {
    while (true) {
      if (!ReclaimCompletions()) {
        FailAndDrain();
        return;
      }
      if (io_failed_.load(std::memory_order_relaxed)) {
        FailAndDrain();
        return;
      }

      PendingOp* op = AcquireOp();
      if (op == nullptr) {
        if (in_flight_ == 0) {
          // Should be unreachable (all ops free when none in flight).
          std::this_thread::yield();
          continue;
        }
        if (!WaitOneCqe()) {
          FailAndDrain();
          return;
        }
        continue;
      }

      // Keep reclaiming while work is outstanding; long wait only when idle.
      const bool got =
          in_flight_ > 0
              ? queue_.PopWaitFor(&op->frames[0], std::chrono::milliseconds(0))
              : queue_.PopWaitFor(&op->frames[0], std::chrono::seconds(1));

      if (!got) {
        ReleaseOp(op);
        if (queue_.cancelled() && queue_.Size() == 0 && in_flight_ == 0) {
          break;
        }
        if (in_flight_ > 0) {
          if (!WaitOneCqe()) {
            FailAndDrain();
            return;
          }
          continue;
        }
        // Idle timeout: everysec sync of dirty tail.
        if (!queue_.cancelled() &&
            fsync_ == vemory::AofFsyncPolicy::kEverySec) {
          std::lock_guard<std::mutex> lock(file_mu_);
          if (!SyncFile()) {
            spdlog::error("AOF fsync failed path={}", path_);
            FailAndDrain();
            return;
          }
        }
        continue;
      }

      op->n = 1;
      while (op->n < kMaxBatch &&
             queue_.PopWaitFor(&op->frames[op->n],
                               std::chrono::milliseconds(0))) {
        ++op->n;
      }

      if (!SubmitOp(op)) {
        spdlog::error("AOF io_uring submit failed path={}", path_);
        DecPendingN(op->n);
        ReleaseOp(op);
        FailAndDrain();
        return;
      }
    }

    // Shutdown: reclaim any stragglers (should already be idle after Flush).
    if (!ReclaimCompletions()) {
      FailAndDrain();
    }
  }

  std::string path_;
  vemory::AofFsyncPolicy fsync_;
  int fd_ = -1;
  struct io_uring ring_{};
  bool ring_inited_ = false;

  RingBuffer<std::string, kQueueCapacity> queue_;
  PendingOp ops_[kMaxInFlight];
  std::size_t in_flight_ = 0;

  std::thread thread_;
  std::atomic<bool> io_failed_{false};
  std::atomic<bool> stopped_{false};

  std::mutex file_mu_;
  bool dirty_ = false;
  std::chrono::steady_clock::time_point last_fsync_;

  std::mutex drain_mu_;
  std::condition_variable drain_cv_;
  std::size_t pending_ = 0;
};

}  // namespace

std::unique_ptr<AofWriter> TryMakeIoUringAofWriter(std::string path,
                                                  vemory::AofFsyncPolicy fsync) {
  return IoUringAofWriter::TryCreate(std::move(path), fsync);
}

#else  // !VEMORY_HAVE_LIBURING

std::unique_ptr<AofWriter> TryMakeIoUringAofWriter(std::string /*path*/,
                                                  vemory::AofFsyncPolicy /*fsync*/) {
  return nullptr;
}

#endif
