#include "vemory/index/VectorSetRegistry.h"

VectorSetRegistry::VectorSetRegistry(std::size_t default_capacity)
    : default_capacity_(default_capacity) {}

VectorSet* VectorSetRegistry::Find(std::string_view key) {
  auto it = sets_.find(std::string(key));
  if (it == sets_.end()) {
    return nullptr;
  }
  return it->second.get();
}

const VectorSet* VectorSetRegistry::Find(std::string_view key) const {
  auto it = sets_.find(std::string(key));
  if (it == sets_.end()) {
    return nullptr;
  }
  return it->second.get();
}

VectorSet* VectorSetRegistry::GetOrCreate(std::string_view key,
                                          std::size_t dim) {
  if (key.empty()) {
    return nullptr;
  }
  const std::string name(key);
  auto it = sets_.find(name);
  if (it != sets_.end()) {
    if (dim != 0 && dim != it->second->dimensions()) {
      return nullptr;
    }
    return it->second.get();
  }
  if (dim == 0) {
    return nullptr;
  }
  auto set = std::make_unique<VectorSet>(dim, default_capacity_);
  VectorSet* raw = set.get();
  sets_.emplace(name, std::move(set));
  return raw;
}
