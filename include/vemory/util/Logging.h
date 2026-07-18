#pragma once

namespace vemory {

// Configure the default spdlog logger (stdout color, info level).
// Call once from main before other components log.
void InitLogging();

}  // namespace vemory
