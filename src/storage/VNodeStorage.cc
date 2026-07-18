#include "vemory/storage/VNodeStorage.h"

VNodeStorage::Status VNodeStorage::Put(VNode node, uint16_t* out_id) {
  if (out_id == nullptr || node.prompt.empty()) {
    return Status::kBadValue;
  }

  if (next_id_ == 0) {
    return Status::kFull;
  }
  const uint16_t id = next_id_++;
  node.id = id;

  auto it = by_prompt_.find(node.prompt);
  if (it != by_prompt_.end()) {
    by_id_.erase(it->second);
    it->second = id;
  } else {
    by_prompt_.emplace(node.prompt, id);
  }
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

VNodeStorage::Status VNodeStorage::GetByPrompt(std::string_view prompt,
                                               VNode* out) const {
  if (out == nullptr) {
    return Status::kBadValue;
  }
  auto it = by_prompt_.find(std::string(prompt));
  if (it == by_prompt_.end()) {
    return Status::kNotFound;
  }
  return GetById(it->second, out);
}

VNodeStorage::Status VNodeStorage::DelByPrompt(std::string_view prompt) {
  auto it = by_prompt_.find(std::string(prompt));
  if (it == by_prompt_.end()) {
    return Status::kNotFound;
  }
  by_id_.erase(it->second);
  by_prompt_.erase(it);
  return Status::kOk;
}
