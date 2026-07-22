#include "vemory/storage/VNodeIndex.h"

#include <cstring>
#include <vector>

VNodeIndex::VNodeIndex(std::size_t default_capacity)
    : default_capacity_(default_capacity == 0 ? 1024 : default_capacity) {}

bool VNodeIndex::ParseFloatBlob(std::string_view blob, const float** out_data,
                                std::size_t* out_dim) {
  if (out_data == nullptr || out_dim == nullptr) {
    return false;
  }
  if (blob.empty() || blob.size() % sizeof(float) != 0) {
    return false;
  }
  *out_dim = blob.size() / sizeof(float);
  if (*out_dim == 0) {
    return false;
  }
  *out_data = reinterpret_cast<const float*>(blob.data());
  return true;
}

namespace {

std::vector<float> CopyFloats(std::string_view blob, std::size_t dim) {
  std::vector<float> out(dim);
  std::memcpy(out.data(), blob.data(), dim * sizeof(float));
  return out;
}

}  // namespace

VNodeIndex::Status VNodeIndex::EnsureIndex(std::size_t dim) {
  if (index_ != nullptr) {
    if (dim != dim_) {
      return Status::kDimMismatch;
    }
    return Status::kOk;
  }
  auto idx = std::make_unique<USearchEmbedIndex>(dim, default_capacity_);
  std::vector<float> probe(dim, 0.f);
  std::vector<USearchEmbedIndex::Hit> hits;
  if (idx->Search(probe.data(), dim, 1, &hits) ==
      USearchEmbedIndex::Status::kError) {
    return Status::kIndexInitFailed;
  }
  dim_ = dim;
  index_ = std::move(idx);
  return Status::kOk;
}

VNodeIndex::Status VNodeIndex::Set(std::string_view vector_blob,
                                   std::string_view user_key,
                                   std::string_view question,
                                   std::string_view answer) {
  if (user_key.empty()) {
    return Status::kBadValue;
  }
  const float* raw = nullptr;
  std::size_t dim = 0;
  if (!ParseFloatBlob(vector_blob, &raw, &dim)) {
    return Status::kBadVectorSize;
  }
  (void)raw;

  const auto est = EnsureIndex(dim);
  if (est != Status::kOk) {
    return est;
  }

  VNode node;
  node.user_key.assign(user_key.data(), user_key.size());
  node.question.assign(question.data(), question.size());
  node.answer.assign(answer.data(), answer.size());

  uint16_t id = 0;
  const auto pst = storage_.Put(std::move(node), &id);
  if (pst == VNodeStorage::Status::kFull) {
    return Status::kError;
  }
  if (pst != VNodeStorage::Status::kOk) {
    return Status::kBadValue;
  }

  const auto floats = CopyFloats(vector_blob, dim);
  const auto ist = index_->Add(id, floats.data(), dim);
  if (ist != USearchEmbedIndex::Status::kOk) {
    storage_.DelByUserKey(user_key);
    return Status::kError;
  }
  return Status::kOk;
}

VNodeIndex::Status VNodeIndex::Get(std::string_view query_blob,
                                   float distance_threshold,
                                   std::string* out_answer) const {
  if (out_answer == nullptr) {
    return Status::kBadValue;
  }
  out_answer->clear();
  if (index_ == nullptr || dim_ == 0) {
    return Status::kNotFound;
  }

  const float* raw = nullptr;
  std::size_t dim = 0;
  if (!ParseFloatBlob(query_blob, &raw, &dim)) {
    return Status::kBadVectorSize;
  }
  (void)raw;
  if (dim != dim_) {
    return Status::kDimMismatch;
  }

  const auto floats = CopyFloats(query_blob, dim);
  std::vector<USearchEmbedIndex::Hit> hits;
  const auto ist = index_->Search(floats.data(), dim, 1, &hits);
  if (ist != USearchEmbedIndex::Status::kOk || hits.empty()) {
    return Status::kNotFound;
  }
  if (hits[0].score > distance_threshold) {
    return Status::kNotFound;
  }

  VNode node;
  if (storage_.GetById(hits[0].id, &node) != VNodeStorage::Status::kOk) {
    return Status::kNotFound;
  }
  *out_answer = std::move(node.answer);
  return Status::kOk;
}

VNodeIndex::Status VNodeIndex::Del(std::string_view user_key) {
  if (user_key.empty()) {
    return Status::kBadValue;
  }
  VNode node;
  if (storage_.GetByUserKey(user_key, &node) != VNodeStorage::Status::kOk) {
    return Status::kNotFound;
  }
  if (index_ != nullptr) {
    (void)index_->Del(node.id);
  }
  if (storage_.DelByUserKey(user_key) != VNodeStorage::Status::kOk) {
    return Status::kNotFound;
  }
  return Status::kOk;
}
