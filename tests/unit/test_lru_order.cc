#include <gtest/gtest.h>

#include "vemory/util/LruOrder.h"

TEST(LruOrder, PushTouchPopErase) {
  LruOrder lru;
  lru.Push(1);
  lru.Push(2);
  lru.Push(3);
  EXPECT_EQ(lru.size(), 3u);

  uint16_t id = 0;
  ASSERT_TRUE(lru.PeekLru(&id));
  EXPECT_EQ(id, 1);

  lru.Touch(1);
  ASSERT_TRUE(lru.PeekLru(&id));
  EXPECT_EQ(id, 2);

  lru.Push(2);  // already present → Touch
  ASSERT_TRUE(lru.PeekLru(&id));
  EXPECT_EQ(id, 3);

  ASSERT_TRUE(lru.PopLru(&id));
  EXPECT_EQ(id, 3);
  EXPECT_EQ(lru.size(), 2u);

  EXPECT_TRUE(lru.Erase(1));
  EXPECT_FALSE(lru.Erase(1));
  EXPECT_EQ(lru.size(), 1u);

  ASSERT_TRUE(lru.PeekLru(&id));
  EXPECT_EQ(id, 2);

  lru.Clear();
  EXPECT_EQ(lru.size(), 0u);
  EXPECT_FALSE(lru.PeekLru(&id));
  EXPECT_FALSE(lru.PopLru(&id));
}

TEST(LruOrder, TouchMissingIsNoOp) {
  LruOrder lru;
  lru.Push(5);
  lru.Touch(99);
  EXPECT_EQ(lru.size(), 1u);
  uint16_t id = 0;
  ASSERT_TRUE(lru.PeekLru(&id));
  EXPECT_EQ(id, 5);
}
