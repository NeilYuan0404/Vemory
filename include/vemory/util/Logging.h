#pragma once

#include <string_view>

namespace vemory {

// Configure the default spdlog logger (stdout color).
// level: trace/debug/info/warn/error/critical/off (default info).
// Call once from main before other components log.
void InitLogging(std::string_view level = "info");

}  // namespace vemory
