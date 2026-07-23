#include "vemory/index/USearchEmbedIndex.h"

#include <usearch/index_dense.hpp>

namespace {

using unum::usearch::index_dense_t;
using unum::usearch::metric_kind_t;
using unum::usearch::metric_punned_t;
using unum::usearch::scalar_kind_t;

}  // namespace

struct USearchEmbedIndex::Impl {
  index_dense_t index;
};

USearchEmbedIndex::USearchEmbedIndex(std::size_t dimensions, std::size_t capacity)
    : dimensions_(dimensions), capacity_(capacity == 0 ? 1 : capacity) {
  if (dimensions_ == 0) {
    return;
  }
  metric_punned_t metric(dimensions_, metric_kind_t::cos_k, scalar_kind_t::f32_k);
  auto made = index_dense_t::make(metric);
  if (!made) {
    return;
  }
  impl_ = std::make_unique<Impl>();
  impl_->index = std::move(made.index);
  if (!impl_->index.try_reserve(capacity_)) {
    impl_.reset();
  }
}

USearchEmbedIndex::~USearchEmbedIndex() = default;

USearchEmbedIndex::USearchEmbedIndex(USearchEmbedIndex&&) noexcept = default;
USearchEmbedIndex& USearchEmbedIndex::operator=(USearchEmbedIndex&&) noexcept =
    default;

USearchEmbedIndex::Status USearchEmbedIndex::Add(uint16_t id, const float* data,
                                                 std::size_t dim) {
  if (!impl_ || dimensions_ == 0) {
    return Status::kError;
  }
  if (data == nullptr || dim != dimensions_) {
    return Status::kBadValue;
  }
  return usearchAdd(id, data);
}

USearchEmbedIndex::Status USearchEmbedIndex::Search(
    const float* query, std::size_t dim, std::size_t k,
    std::vector<Hit>* out) const {
  if (out == nullptr) {
    return Status::kBadValue;
  }
  out->clear();
  if (!impl_ || dimensions_ == 0) {
    return Status::kError;
  }
  if (query == nullptr || dim != dimensions_ || k == 0) {
    return Status::kBadValue;
  }
  return usearchSearch(query, k, out);
}

USearchEmbedIndex::Status USearchEmbedIndex::Del(uint16_t id) {
  if (!impl_ || dimensions_ == 0) {
    return Status::kError;
  }
  return usearchDel(id);
}

USearchEmbedIndex::Status USearchEmbedIndex::usearchAdd(uint16_t id,
                                                        const float* data) {
  // Replace if present so Put-style re-index is idempotent.
  if (impl_->index.contains(id)) {
    auto removed = impl_->index.remove(id);
    if (!removed) {
      return Status::kError;
    }
  }

  if (impl_->index.size() >= impl_->index.capacity()) {
    std::size_t next = impl_->index.capacity() == 0
                           ? capacity_
                           : impl_->index.capacity() * 2;
    if (!impl_->index.try_reserve(next)) {
      return Status::kError;
    }
  }

  auto added = impl_->index.add(static_cast<index_dense_t::vector_key_t>(id),
                                data);
  if (!added) {
    return Status::kError;
  }
  return Status::kOk;
}

USearchEmbedIndex::Status USearchEmbedIndex::usearchSearch(
    const float* query, std::size_t k, std::vector<Hit>* out) const {
  auto results = impl_->index.search(query, k);
  if (!results) {
    return Status::kError;
  }
  out->reserve(results.size());
  for (std::size_t i = 0; i != results.size(); ++i) {
    Hit hit;
    hit.id = static_cast<uint16_t>(results[i].member.key);
    hit.score = static_cast<float>(results[i].distance);
    out->push_back(hit);
  }
  return Status::kOk;
}

USearchEmbedIndex::Status USearchEmbedIndex::usearchDel(uint16_t id) {
  if (!impl_->index.contains(id)) {
    return Status::kNotFound;
  }
  auto removed = impl_->index.remove(static_cast<index_dense_t::vector_key_t>(id));
  if (!removed) {
    return Status::kError;
  }
  if (removed.completed == 0) {
    return Status::kNotFound;
  }
  return Status::kOk;
}

USearchEmbedIndex::Status USearchEmbedIndex::Save(const char* path) const {
  if (path == nullptr || path[0] == '\0' || !impl_) {
    return Status::kBadValue;
  }
  auto result = impl_->index.save(path);
  return result ? Status::kOk : Status::kError;
}

USearchEmbedIndex::Status USearchEmbedIndex::Load(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return Status::kBadValue;
  }
  auto made = index_dense_t::make(path, /*view=*/false);
  if (!made) {
    return Status::kError;
  }
  impl_ = std::make_unique<Impl>();
  impl_->index = std::move(made.index);
  dimensions_ = impl_->index.dimensions();
  capacity_ = impl_->index.capacity() == 0 ? 1 : impl_->index.capacity();
  return Status::kOk;
}
