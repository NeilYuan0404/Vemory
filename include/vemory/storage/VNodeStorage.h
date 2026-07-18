#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vemory/protocol/VNode.h"

// In-memory store: key = assigned uint16 id, value = owned VNode.
// Also indexes prompt → id for lookup / delete.
// Serialization (ProtobufVNodeCodec) is for replication, not Put/Get.
class VNodeStorage {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kFull,       // no free id
    kBadValue,   // empty prompt / null args
  };

  VNodeStorage() = default;

  // Assigns a new id and stores the node. *out_id set on kOk.
  Status Put(VNode node, uint16_t* out_id);

  Status GetById(uint16_t id, VNode* out) const;
  Status GetByPrompt(std::string_view prompt, VNode* out) const;
  Status DelByPrompt(std::string_view prompt);

 private:
  uint16_t next_id_ = 1;
  std::unordered_map<uint16_t, VNode> by_id_;
  std::unordered_map<std::string, uint16_t> by_prompt_;
};
