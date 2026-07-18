#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "vemory/index/USearchEmbedIndex.h"

// One named Redis-style vector set: element name ↔ uint16 id + ANN index.
class VectorSet {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kBadValue,
    kFull,
    kError,
  };

  struct Hit {
    std::string element;
    float score = 0.f;  // cosine similarity
  };

  // dim must be > 0; locks the set dimension.
  explicit VectorSet(std::size_t dim, std::size_t capacity = 1024);

  std::size_t dimensions() const { return dim_; }
  std::size_t size() const { return name_to_id_.size(); }

  // Insert or replace element. *data has dim floats.
  Status Add(std::string_view element, const float* data, std::size_t dim);

  // kNN by query vector; returns element names (and similarities).
  Status Search(const float* query, std::size_t dim, std::size_t k,
                std::vector<Hit>* out) const;

  // kNN using an existing element's stored vector as the query.
  Status SearchByElement(std::string_view element, std::size_t k,
                         std::vector<Hit>* out) const;

  Status GetEmbedding(std::string_view element, std::vector<float>* out) const;

 private:
  std::size_t dim_ = 0;
  uint16_t next_id_ = 1;
  USearchEmbedIndex index_;
  std::unordered_map<std::string, uint16_t> name_to_id_;
  std::unordered_map<uint16_t, std::string> id_to_name_;
  std::unordered_map<uint16_t, std::vector<float>> id_to_vec_;
};
