#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vemory/storage/VNode.h"

// In-memory store: key = assigned uint16 id, value = owned VNode.
// Also indexes user_key → id for VDEL.
// Serialization (ProtobufVNodeCodec) is for replication / snapshot, not Put/Get.
class VNodeStorage {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kFull,      // no free id
    kBadValue,  // empty user_key / null args / bad id
  };

  VNodeStorage() = default;

  // Insert or replace by user_key. Same user_key reuses id. *out_id set on kOk.
  Status Put(VNode node, uint16_t* out_id);

  // Snapshot restore: insert exact id (must be non-zero, unused).
  Status Restore(VNode node);

  Status GetById(uint16_t id, VNode* out) const;
  Status GetByUserKey(std::string_view user_key, VNode* out) const;
  Status DelByUserKey(std::string_view user_key);

  void Clear();
  void SetNextId(uint16_t id) { next_id_ = id; }
  uint16_t next_id() const { return next_id_; }

  std::size_t size() const { return by_id_.size(); }

  // Invokes fn(id, node) for each entry. Stops early if fn returns false.
  void ForEach(const std::function<bool(uint16_t, const VNode&)>& fn) const;

 private:
  uint16_t next_id_ = 1;
  std::unordered_map<uint16_t, VNode> by_id_;
  std::unordered_map<std::string, uint16_t> by_user_key_;
};
