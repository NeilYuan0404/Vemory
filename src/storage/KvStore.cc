#include "vemory/storage/KvStore.h"

KvStore::Status KvStore::Set(std::string_view key, std::string_view value) {
  if (key.empty()) {
    return Status::kBadValue;
  }
  const std::string k(key);
  auto it = map_.find(k);
  if (it != map_.end()) {
    it->second.assign(value.data(), value.size());
    return Status::kOk;
  }
  map_.emplace(k, std::string(value.data(), value.size()));
  return Status::kOk;
}

KvStore::Status KvStore::Get(std::string_view key, std::string* out) const {
  if (out == nullptr || key.empty()) {
    return Status::kBadValue;
  }
  auto it = map_.find(std::string(key));
  if (it == map_.end()) {
    return Status::kNotFound;
  }
  *out = it->second;
  return Status::kOk;
}

KvStore::Status KvStore::Del(std::string_view key) {
  if (key.empty()) {
    return Status::kBadValue;
  }
  const auto n = map_.erase(std::string(key));
  return n > 0 ? Status::kOk : Status::kNotFound;
}
