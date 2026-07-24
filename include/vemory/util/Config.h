#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vemory {

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
  // Append-only protobuf log under persistence_dir/appendonly.aof
  bool aof = false;

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
