#include "vemory/storage/VNodeStorage.h"

VNodeStorage::Status VNodeStorage::Put(VNode node, uint16_t* out_id) {
  if (out_id == nullptr || node.user_key.empty()) {
    return Status::kBadValue;
  }

  auto it = by_user_key_.find(node.user_key);
  if (it != by_user_key_.end()) {
    const uint16_t id = it->second;
    node.id = id;
    by_id_[id] = std::move(node);
    *out_id = id;
    return Status::kOk;
  }

  if (next_id_ == 0) {
    return Status::kFull;
  }
  const uint16_t id = next_id_++;
  node.id = id;
  by_user_key_.emplace(node.user_key, id);
  by_id_[id] = std::move(node);
  *out_id = id;
  return Status::kOk;
}

VNodeStorage::Status VNodeStorage::GetById(uint16_t id, VNode* out) const {
  if (out == nullptr) {
    return Status::kBadValue;
  }
  auto it = by_id_.find(id);
  if (it == by_id_.end()) {
    return Status::kNotFound;
  }
  *out = it->second;
  return Status::kOk;
}

VNodeStorage::Status VNodeStorage::GetByUserKey(std::string_view user_key,
                                                VNode* out) const {
  if (out == nullptr) {
    return Status::kBadValue;
  }
  auto it = by_user_key_.find(std::string(user_key));
  if (it == by_user_key_.end()) {
    return Status::kNotFound;
  }
  return GetById(it->second, out);
}

VNodeStorage::Status VNodeStorage::DelByUserKey(std::string_view user_key) {
  auto it = by_user_key_.find(std::string(user_key));
  if (it == by_user_key_.end()) {
    return Status::kNotFound;
  }
  by_id_.erase(it->second);
  by_user_key_.erase(it);
  return Status::kOk;
}
