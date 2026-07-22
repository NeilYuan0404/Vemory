#pragma once

#include <cstdint>
#include <string>

// One semantic-cache node (owned). Vectors live in USearchEmbedIndex by id.
struct VNode {
  uint16_t id = 0;
  std::string user_key;
  std::string question;
  std::string answer;
};
