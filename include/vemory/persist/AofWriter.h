#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vemory/util/Config.h"

// Background AOF writer: enqueue complete frames (u32le|payload), flush, stop.
// Implementations own the queue + flush thread + file descriptor.
class AofWriter {
 public:
  virtual ~AofWriter() = default;

  // Enqueue one frame. Blocks if the queue is full. Returns false if stopped
  // or the writer has already failed.
  virtual bool Enqueue(std::string frame) = 0;

  // Wait until previously enqueued frames are written; then fdatasync when
  // policy != no.
  virtual bool Flush() = 0;

  // Cancel queue, join thread, close file.
  virtual void Stop() = 0;

  virtual bool failed() const = 0;
};

// Build a writer. For auto/iouring, falls back to ThreadAofWriter on failure
// (missing liburing, kernel, or init error) and logs a warning.
std::unique_ptr<AofWriter> MakeAofWriter(std::string path,
                                        vemory::AofFsyncPolicy fsync,
                                        vemory::AofIoMode mode);
