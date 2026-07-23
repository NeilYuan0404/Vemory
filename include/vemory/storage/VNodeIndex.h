#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "vemory/index/USearchEmbedIndex.h"
#include "vemory/storage/VNodeStorage.h"

// Orchestrates VNodeStorage + USearchEmbedIndex for semantic cache.
class VNodeIndex {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNotFound,
    kBadValue,       // empty args / invalid blob
    kBadVectorSize,  // len % sizeof(float) != 0 or empty
    kDimMismatch,
    kIndexInitFailed,
    kIoError,
    kError,
  };

  explicit VNodeIndex(std::size_t default_capacity = 1024);

  // vector_blob: raw float bytes. Dim locked on first successful Set.
  Status Set(std::string_view vector_blob, std::string_view user_key,
             std::string_view question, std::string_view answer);

  // Hit if top-1 cosine distance <= threshold; writes answer on kOk.
  Status Get(std::string_view query_blob, float distance_threshold,
             std::string* out_answer) const;

  Status Del(std::string_view user_key);

  std::size_t dimensions() const { return dim_; }
  bool ready() const { return index_ != nullptr; }
  std::size_t node_count() const { return storage_.size(); }
  uint16_t next_id() const { return storage_.next_id(); }

  // Snapshot helpers (nodes file + usearch file).
  Status DumpNodes(FILE* fp) const;
  Status LoadNodes(FILE* fp, uint64_t node_count, uint16_t next_id);
  Status SaveIndex(const char* path) const;
  Status LoadIndex(const char* path, std::size_t dim);
  void Clear();

 private:
  static bool ParseFloatBlob(std::string_view blob, const float** out_data,
                             std::size_t* out_dim);

  Status EnsureIndex(std::size_t dim);

  std::size_t default_capacity_;
  std::size_t dim_ = 0;
  VNodeStorage storage_;
  std::unique_ptr<USearchEmbedIndex> index_;
};
