#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vemory/util/Config.h"

// AOF writer: enqueue RESP frames, flush, stop.
// Inline io_uring path runs on the reactor thread; thread path uses a queue.
class AofWriter {
 public:
  virtual ~AofWriter() = default;

  // Enqueue one frame. May block under backpressure. Returns false if stopped
  // or the writer has already failed.
  virtual bool Enqueue(std::string frame) = 0;

  // Wait until buffered/pending frames are written; then fdatasync when
  // policy != no.
  virtual bool Flush() = 0;

  // Cancel / join / close.
  virtual void Stop() = 0;

  virtual bool failed() const = 0;

  // Reactor idle hook (inline io_uring: peek + timed flush). Default no-op.
  virtual void Poll() {}
};

// Build a writer. For auto/iouring, falls back to ThreadAofWriter on failure.
std::unique_ptr<AofWriter> MakeAofWriter(
    std::string path, vemory::AofFsyncPolicy fsync, vemory::AofIoMode mode,
    int flush_interval_ms = 1000);
