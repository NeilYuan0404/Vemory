#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "vemory/storage/VNode.h"

// RESP codec for VNode ↔ bytes (RDB NODES section).
// Encodes as array of 4 bulks: [id_decimal, user_key, question, answer].
class RespVNodeCodec {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kError,
  };

  Status Encode(const VNode& node, std::string* out) const;
  Status Decode(std::string_view bytes, VNode* out) const;
};
