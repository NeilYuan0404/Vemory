#include "vemory/persist/AofWriter.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "vemory/persist/IoUringAofWriter.h"
#include "vemory/persist/ThreadAofWriter.h"

std::unique_ptr<AofWriter> MakeAofWriter(std::string path,
                                        vemory::AofFsyncPolicy fsync,
                                        vemory::AofIoMode mode) {
  if (mode == vemory::AofIoMode::kThread) {
    return std::make_unique<ThreadAofWriter>(std::move(path), fsync);
  }

  if (mode == vemory::AofIoMode::kAuto || mode == vemory::AofIoMode::kIoUring) {
    auto uring = TryMakeIoUringAofWriter(path, fsync);
    if (uring != nullptr) {
      spdlog::info("AOF writer: io_uring path={}", path);
      return uring;
    }
    if (mode == vemory::AofIoMode::kIoUring) {
      spdlog::warn(
          "AOF aof_io=iouring unavailable; falling back to thread path={}",
          path);
    } else {
      spdlog::info("AOF writer: thread (io_uring unavailable) path={}", path);
    }
  }

  return std::make_unique<ThreadAofWriter>(std::move(path), fsync);
}
