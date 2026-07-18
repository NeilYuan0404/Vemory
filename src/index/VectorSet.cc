#include "vemory/index/VectorSet.h"

VectorSet::VectorSet(std::size_t dim, std::size_t capacity)
    : dim_(dim), index_(dim, capacity) {}

VectorSet::Status VectorSet::Add(std::string_view element, const float* data,
                                 std::size_t dim) {
  if (element.empty() || data == nullptr || dim == 0 || dim != dim_) {
    return Status::kBadValue;
  }

  const std::string name(element);
  uint16_t id = 0;
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    id = it->second;
  } else {
    if (next_id_ == 0) {
      return Status::kFull;
    }
    id = next_id_++;
    name_to_id_.emplace(name, id);
    id_to_name_.emplace(id, name);
  }

  const auto ist = index_.Add(id, data, dim);
  if (ist != USearchEmbedIndex::Status::kOk) {
    if (it == name_to_id_.end()) {
      name_to_id_.erase(name);
      id_to_name_.erase(id);
    }
    return Status::kError;
  }

  id_to_vec_[id].assign(data, data + dim);
  return Status::kOk;
}

VectorSet::Status VectorSet::Search(const float* query, std::size_t dim,
                                    std::size_t k,
                                    std::vector<Hit>* out) const {
  if (out == nullptr) {
    return Status::kBadValue;
  }
  out->clear();
  if (query == nullptr || dim != dim_ || k == 0) {
    return Status::kBadValue;
  }

  std::vector<USearchEmbedIndex::Hit> hits;
  const auto ist = index_.Search(query, dim, k, &hits);
  if (ist != USearchEmbedIndex::Status::kOk) {
    return Status::kError;
  }

  out->reserve(hits.size());
  for (const auto& h : hits) {
    auto nit = id_to_name_.find(h.id);
    if (nit == id_to_name_.end()) {
      continue;
    }
    Hit hit;
    hit.element = nit->second;
    hit.score = 1.f - h.score;  // cosine distance → similarity
    out->push_back(std::move(hit));
  }
  return Status::kOk;
}

VectorSet::Status VectorSet::SearchByElement(std::string_view element,
                                             std::size_t k,
                                             std::vector<Hit>* out) const {
  std::vector<float> query;
  const auto gst = GetEmbedding(element, &query);
  if (gst != Status::kOk) {
    return gst;
  }
  return Search(query.data(), query.size(), k, out);
}

VectorSet::Status VectorSet::GetEmbedding(std::string_view element,
                                          std::vector<float>* out) const {
  if (out == nullptr || element.empty()) {
    return Status::kBadValue;
  }
  auto it = name_to_id_.find(std::string(element));
  if (it == name_to_id_.end()) {
    return Status::kNotFound;
  }
  auto vit = id_to_vec_.find(it->second);
  if (vit == id_to_vec_.end()) {
    return Status::kNotFound;
  }
  *out = vit->second;
  return Status::kOk;
}
