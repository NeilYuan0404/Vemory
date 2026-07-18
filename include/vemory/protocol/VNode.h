#pragma once

#include <cstdint>
#include <string>

// One Q&A memory node (owned). Vectors live on RequestContext / the embed index.
struct VNode {
  uint16_t id = 0;
  std::string prompt;
  std::string answer;
};
