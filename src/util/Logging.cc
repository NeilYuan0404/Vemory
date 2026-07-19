#include "vemory/util/Logging.h"

#include <memory>
#include <string>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace vemory {

void InitLogging(std::string_view level) {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("vemory", sink);
  const std::string level_str(level.empty() ? "info" : level);
  const auto lvl = spdlog::level::from_str(level_str);
  logger->set_level(lvl);
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
  spdlog::set_default_logger(std::move(logger));
  spdlog::set_level(lvl);
}

}  // namespace vemory
