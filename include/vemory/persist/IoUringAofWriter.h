#pragma once

#ifndef VEMORY_HAVE_LIBURING
#define VEMORY_HAVE_LIBURING 0
#endif

#include <memory>
#include <string>

#include "vemory/persist/AofWriter.h"
#include "vemory/util/Config.h"

// Returns an IoUringAofWriter, or nullptr if unavailable / init failed.
std::unique_ptr<AofWriter> TryMakeIoUringAofWriter(std::string path,
                                                  vemory::AofFsyncPolicy fsync);
