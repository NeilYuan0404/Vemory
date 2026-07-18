#include <gtest/gtest.h>

#include <vector>

#include "vemory/index/USearchEmbedIndex.h"

TEST(USearchEmbedIndex, KnnReturnsNearestFirst) {
  USearchEmbedIndex index(2, 16);

  const float a[] = {1.f, 0.f};
  const float b[] = {0.f, 1.f};
  ASSERT_EQ(index.Add(1, a, 2), USearchEmbedIndex::Status::kOk);
  ASSERT_EQ(index.Add(2, b, 2), USearchEmbedIndex::Status::kOk);

  const float query[] = {0.9f, 0.1f};
  std::vector<USearchEmbedIndex::Hit> hits;
  ASSERT_EQ(index.Search(query, 2, 2, &hits), USearchEmbedIndex::Status::kOk);
  ASSERT_GE(hits.size(), 1u);
  EXPECT_EQ(hits[0].id, 1);
  EXPECT_LT(hits[0].score, hits.size() > 1 ? hits[1].score : 2.f);
}

TEST(USearchEmbedIndex, DelRemovesFromSearch) {
  USearchEmbedIndex index(2, 16);

  const float a[] = {1.f, 0.f};
  const float b[] = {0.f, 1.f};
  ASSERT_EQ(index.Add(1, a, 2), USearchEmbedIndex::Status::kOk);
  ASSERT_EQ(index.Add(2, b, 2), USearchEmbedIndex::Status::kOk);
  ASSERT_EQ(index.Del(1), USearchEmbedIndex::Status::kOk);
  EXPECT_EQ(index.Del(1), USearchEmbedIndex::Status::kNotFound);

  std::vector<USearchEmbedIndex::Hit> hits;
  ASSERT_EQ(index.Search(a, 2, 2, &hits), USearchEmbedIndex::Status::kOk);
  for (const auto& hit : hits) {
    EXPECT_NE(hit.id, 1);
  }
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits[0].id, 2);
}

TEST(USearchEmbedIndex, WrongDimIsBadValue) {
  USearchEmbedIndex index(3, 8);

  const float bad[] = {1.f, 0.f};
  EXPECT_EQ(index.Add(1, bad, 2), USearchEmbedIndex::Status::kBadValue);

  const float ok[] = {1.f, 0.f, 0.f};
  ASSERT_EQ(index.Add(1, ok, 3), USearchEmbedIndex::Status::kOk);

  std::vector<USearchEmbedIndex::Hit> hits;
  EXPECT_EQ(index.Search(bad, 2, 1, &hits), USearchEmbedIndex::Status::kBadValue);
  EXPECT_EQ(index.Search(ok, 3, 0, &hits), USearchEmbedIndex::Status::kBadValue);
  EXPECT_EQ(index.Search(ok, 3, 1, nullptr), USearchEmbedIndex::Status::kBadValue);
}
