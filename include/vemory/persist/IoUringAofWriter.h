#pragma once

#ifndef VEMORY_HAVE_LIBURING
#define VEMORY_HAVE_LIBURING 0
#endif

#include <memory>
#include <string>

#include "vemory/persist/AofWriter.h"
#include "vemory/util/Config.h"

// Inline (reactor-thread) io_uring AOF writer, or nullptr if unavailable.
std::unique_ptr<AofWriter> TryMakeIoUringAofWriter(
    std::string path, vemory::AofFsyncPolicy fsync, int flush_interval_ms = 1000);
