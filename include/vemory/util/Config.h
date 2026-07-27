#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vemory {

// Redis-style AOF fsync policy (persistence.aof_fsync).
enum class AofFsyncPolicy : uint8_t {
  kNo = 0,       // fflush only (no fdatasync)
  kEverySec = 1, // fdatasync at most once per second (default)
  kAlways = 2,   // fdatasync after every flush batch
};

// AOF flush backend (persistence.aof_io).
enum class AofIoMode : uint8_t {
  kAuto = 0,     // try io_uring, else thread (experimental path via auto)
  kThread = 1,   // BlockingQueue + fwrite flush thread (default / recommended)
  kIoUring = 2,  // RingBuffer + pipelined io_uring writev (experimental; fallback on fail)
};

// Runtime settings loaded from an INI file (or left at built-in defaults).
struct Config {
  uint16_t port = 6379;
  std::string bind = "0.0.0.0";
  std::string log_level = "info";
  std::size_t kv_reserve = 100000;
  std::size_t default_capacity = 1024;
  // Snapshot directory; empty disables SAVE. Default: data/
  std::string persistence_dir = "data";
  bool load_on_startup = false;
  // Append-only RESP write-command log under persistence_dir/appendonly.aof
  bool aof = false;
  AofFsyncPolicy aof_fsync = AofFsyncPolicy::kEverySec;
  AofIoMode aof_io = AofIoMode::kThread;

  // CLI --slaveof only (not INI). When set, process acts as replica.
  bool slaveof = false;
  std::string slaveof_host;
  uint16_t slaveof_port = 0;

  // Soft issues from the last LoadConfig (unknown keys/sections).
  std::vector<std::string> warnings;
};

// Load INI from path into *out (starts from built-in defaults, then overlays).
// On failure returns false and sets *error when non-null.
bool LoadConfig(std::string_view path, Config* out, std::string* error);

// Map logging.level string to a recognized name; empty on unknown.
// Accepted: trace, debug, info, warn, error, critical, off.
bool IsValidLogLevel(std::string_view level);

}  // namespace vemory
