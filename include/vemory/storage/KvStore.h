#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

// In-memory string KVS: hash map (average O(1) Set/Get/Del).
class KvStore {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kBadValue,  // empty key / null out
    kIoError,
  };

  // Insert or replace. Empty value is allowed; empty key is not.
  Status Set(std::string_view key, std::string_view value);

  Status Get(std::string_view key, std::string* out) const;

  // Removes at most one entry. kNotFound if missing.
  Status Del(std::string_view key);

  // Pre-size the hash table to avoid rehash during load (e.g. bench -r).
  void Reserve(std::size_t n) { map_.reserve(n); }

  std::size_t size() const { return map_.size(); }

  void Clear() { map_.clear(); }

  // Binary dump: uint64 count + (u32 key_len, key, u32 val_len, val)*
  Status Dump(FILE* fp) const;
  Status Load(FILE* fp);

 private:
  std::unordered_map<std::string, std::string> map_;
};
