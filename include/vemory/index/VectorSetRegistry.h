#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vemory/index/VectorSet.h"

// Owns named VectorSet instances (Redis key → vector set).
class VectorSetRegistry {
 public:
  explicit VectorSetRegistry(std::size_t default_capacity = 1024);

  // Get existing set, or null if missing.
  VectorSet* Find(std::string_view key);

  const VectorSet* Find(std::string_view key) const;

  // Get or create set. On first create, dim must be > 0.
  // If set already exists, dim must match (or be ignored if 0).
  // Returns null on bad dim / mismatch / allocation failure.
  VectorSet* GetOrCreate(std::string_view key, std::size_t dim);

 private:
  std::size_t default_capacity_;
  std::unordered_map<std::string, std::unique_ptr<VectorSet>> sets_;
};
