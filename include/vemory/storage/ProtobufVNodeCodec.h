#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "vemory/protocol/VNode.h"

// Protobuf codec for VNode ↔ bytes (replication / future persist).
// Not used by VNodeStorage Put/Get.
class ProtobufVNodeCodec {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kError,
  };

  Status Encode(const VNode& node, std::string* out) const;
  Status Decode(std::string_view bytes, VNode* out) const;
};
