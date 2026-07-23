#include "vemory/storage/KvStore.h"

#include <cstring>

namespace {

bool WriteExact(FILE* fp, const void* data, std::size_t n) {
  return std::fwrite(data, 1, n, fp) == n;
}

bool ReadExact(FILE* fp, void* data, std::size_t n) {
  return std::fread(data, 1, n, fp) == n;
}

}  // namespace

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

KvStore::Status KvStore::Dump(FILE* fp) const {
  if (fp == nullptr) {
    return Status::kBadValue;
  }
  const uint64_t count = map_.size();
  if (!WriteExact(fp, &count, sizeof(count))) {
    return Status::kIoError;
  }
  for (const auto& kv : map_) {
    const uint32_t key_len = static_cast<uint32_t>(kv.first.size());
    const uint32_t val_len = static_cast<uint32_t>(kv.second.size());
    if (!WriteExact(fp, &key_len, sizeof(key_len)) ||
        !WriteExact(fp, kv.first.data(), key_len) ||
        !WriteExact(fp, &val_len, sizeof(val_len)) ||
        !WriteExact(fp, kv.second.data(), val_len)) {
      return Status::kIoError;
    }
  }
  return Status::kOk;
}

KvStore::Status KvStore::Load(FILE* fp) {
  if (fp == nullptr) {
    return Status::kBadValue;
  }
  uint64_t count = 0;
  if (!ReadExact(fp, &count, sizeof(count))) {
    return Status::kIoError;
  }
  map_.clear();
  map_.reserve(static_cast<std::size_t>(count));
  for (uint64_t i = 0; i < count; ++i) {
    uint32_t key_len = 0;
    uint32_t val_len = 0;
    if (!ReadExact(fp, &key_len, sizeof(key_len))) {
      return Status::kIoError;
    }
    std::string key(key_len, '\0');
    if (key_len > 0 && !ReadExact(fp, key.data(), key_len)) {
      return Status::kIoError;
    }
    if (!ReadExact(fp, &val_len, sizeof(val_len))) {
      return Status::kIoError;
    }
    std::string val(val_len, '\0');
    if (val_len > 0 && !ReadExact(fp, val.data(), val_len)) {
      return Status::kIoError;
    }
    if (key.empty()) {
      return Status::kBadValue;
    }
    map_.emplace(std::move(key), std::move(val));
  }
  return Status::kOk;
}
