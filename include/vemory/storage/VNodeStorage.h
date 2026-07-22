#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vemory/storage/VNode.h"

// In-memory store: key = assigned uint16 id, value = owned VNode.
// Also indexes user_key → id for VDEL.
// Serialization (ProtobufVNodeCodec) is for replication, not Put/Get.
class VNodeStorage {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kFull,      // no free id
    kBadValue,  // empty user_key / null args
  };

  VNodeStorage() = default;

  // Insert or replace by user_key. Same user_key reuses id. *out_id set on kOk.
  Status Put(VNode node, uint16_t* out_id);

  Status GetById(uint16_t id, VNode* out) const;
  Status GetByUserKey(std::string_view user_key, VNode* out) const;
  Status DelByUserKey(std::string_view user_key);

  std::size_t size() const { return by_id_.size(); }

 private:
  uint16_t next_id_ = 1;
  std::unordered_map<uint16_t, VNode> by_id_;
  std::unordered_map<std::string, uint16_t> by_user_key_;
};
