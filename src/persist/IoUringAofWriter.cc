#include "vemory/persist/IoUringAofWriter.h"

#include <spdlog/spdlog.h>

#if VEMORY_HAVE_LIBURING

#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

#include "vemory/util/BlockingQueue.h"

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
    if (!queue_.Push(std::move(frame))) {
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
  static constexpr unsigned kRingEntries = 32;

  IoUringAofWriter(std::string path, vemory::AofFsyncPolicy fsync)
      : path_(std::move(path)),
        fsync_(fsync),
        queue_(kQueueCapacity),
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

  void DecPending() {
    std::lock_guard<std::mutex> lock(drain_mu_);
    if (pending_ > 0) {
      --pending_;
    }
    if (pending_ == 0) {
      drain_cv_.notify_all();
    }
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

  bool WriteFrameUring(const std::string& frame) {
    if (frame.empty() || fd_ < 0 || !ring_inited_) {
      return false;
    }
    // Keep buffer alive until CQE (frame is a local in FlushLoop — wait inline).
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
      return false;
    }
    io_uring_prep_write(sqe, fd_, frame.data(), frame.size(), 0);
    io_uring_sqe_set_data(sqe, nullptr);
    if (io_uring_submit_and_wait(&ring_, 1) < 0) {
      return false;
    }
    struct io_uring_cqe* cqe = nullptr;
    if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
      return false;
    }
    const int res = cqe->res;
    io_uring_cqe_seen(&ring_, cqe);
    if (res < 0 || static_cast<std::size_t>(res) != frame.size()) {
      return false;
    }
    dirty_ = true;
    return true;
  }

  void MaybeSyncAfterWrite() {
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

  void FlushLoop() {
    std::string frame;
    while (true) {
      const bool got = queue_.PopWaitFor(&frame, std::chrono::seconds(1));
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

      if (io_failed_.load(std::memory_order_relaxed)) {
        DecPending();
        continue;
      }

      {
        std::lock_guard<std::mutex> lock(file_mu_);
        if (!WriteFrameUring(frame)) {
          spdlog::error("AOF io_uring write failed path={}", path_);
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

  std::string path_;
  vemory::AofFsyncPolicy fsync_;
  int fd_ = -1;
  struct io_uring ring_{};
  bool ring_inited_ = false;

  BlockingQueue<std::string> queue_;
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
