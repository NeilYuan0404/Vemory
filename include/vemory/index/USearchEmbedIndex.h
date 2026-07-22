#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Usearch-backed ANN embed index. Keyed by the same uint16_t id as VNodeIndex.
// Search is kNN (not id lookup). Public header stays free of usearch types.
class USearchEmbedIndex {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kBadValue,  // null args / wrong dim / k == 0
    kError,
  };

  struct Hit {
    uint16_t id = 0;
    float score = 0.f;  // distance from backend; lower is closer for cosine/l2
  };

  // Cosine metric; capacity is initial reserve for the index.
  explicit USearchEmbedIndex(std::size_t dimensions, std::size_t capacity = 1024);
  ~USearchEmbedIndex();

  USearchEmbedIndex(const USearchEmbedIndex&) = delete;
  USearchEmbedIndex& operator=(const USearchEmbedIndex&) = delete;
  USearchEmbedIndex(USearchEmbedIndex&&) noexcept;
  USearchEmbedIndex& operator=(USearchEmbedIndex&&) noexcept;

  // Insert or replace the vector for id. *data has dim floats.
  Status Add(uint16_t id, const float* data, std::size_t dim);

  // kNN: clears *out, then appends up to k hits (best first).
  Status Search(const float* query, std::size_t dim, std::size_t k,
                std::vector<Hit>* out) const;

  Status Del(uint16_t id);

  std::size_t dimensions() const { return dimensions_; }

 private:
  Status usearchAdd(uint16_t id, const float* data);
  Status usearchSearch(const float* query, std::size_t k,
                       std::vector<Hit>* out) const;
  Status usearchDel(uint16_t id);

  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::size_t dimensions_ = 0;
  std::size_t capacity_ = 0;
};
