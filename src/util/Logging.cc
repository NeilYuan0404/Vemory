#include "vemory/util/Logging.h"

#include <memory>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace vemory {

void InitLogging() {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("vemory", sink);
  logger->set_level(spdlog::level::info);
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
  spdlog::set_default_logger(std::move(logger));
  spdlog::set_level(spdlog::level::info);
}

}  // namespace vemory
