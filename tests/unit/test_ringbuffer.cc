#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "vemory/util/ringbuffer.h"

TEST(RingBuffer, PushPopMove) {
  RingBuffer<std::string, 4> q;
  std::string big(1024, 'x');
  ASSERT_TRUE(q.Push(std::move(big)));
  EXPECT_TRUE(big.empty());

  std::string out;
  ASSERT_TRUE(q.Pop(out));
  EXPECT_EQ(out, std::string(1024, 'x'));
  EXPECT_EQ(q.Size(), 0u);
}

TEST(RingBuffer, PushWaitBlocksUntilPop) {
  // Capacity 2 → 1 usable slot.
  RingBuffer<int, 2> q;
  ASSERT_TRUE(q.PushWait(1));

  std::atomic<bool> pushed{false};
  std::thread producer([&] {
    ASSERT_TRUE(q.PushWait(2));
    pushed.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(pushed.load());

  int v = 0;
  ASSERT_TRUE(q.Pop(v));
  EXPECT_EQ(v, 1);

  producer.join();
  EXPECT_TRUE(pushed.load());
  ASSERT_TRUE(q.Pop(v));
  EXPECT_EQ(v, 2);
}

TEST(RingBuffer, CancelUnblocksEmptyPopWaitFor) {
  RingBuffer<int, 4> q;
  std::atomic<bool> done{false};
  std::atomic<bool> ok{true};

  std::thread consumer([&] {
    int v = 0;
    ok.store(q.PopWaitFor(&v, std::chrono::seconds(5)));
    done.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_FALSE(done.load());
  q.Cancel();
  consumer.join();
  EXPECT_TRUE(done.load());
  EXPECT_FALSE(ok.load());
  EXPECT_TRUE(q.cancelled());
}

TEST(RingBuffer, CancelUnblocksFullPushWait) {
  RingBuffer<int, 2> q;
  ASSERT_TRUE(q.PushWait(1));

  std::atomic<bool> done{false};
  std::atomic<bool> ok{true};
  std::thread producer([&] {
    ok.store(q.PushWait(2));
    done.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_FALSE(done.load());
  q.Cancel();
  producer.join();
  EXPECT_TRUE(done.load());
  EXPECT_FALSE(ok.load());
  EXPECT_EQ(q.Size(), 1u);
}

TEST(RingBuffer, PopWaitForTimeout) {
  RingBuffer<int, 4> q;
  int v = -1;
  const auto t0 = std::chrono::steady_clock::now();
  EXPECT_FALSE(q.PopWaitFor(&v, std::chrono::milliseconds(40)));
  const auto elapsed = std::chrono::steady_clock::now() - t0;
  EXPECT_GE(elapsed, std::chrono::milliseconds(30));
  EXPECT_FALSE(q.cancelled());
  EXPECT_EQ(v, -1);
}

TEST(RingBuffer, PopWaitForGetsItem) {
  RingBuffer<int, 4> q;
  ASSERT_TRUE(q.Push(7));
  int v = 0;
  ASSERT_TRUE(q.PopWaitFor(&v, std::chrono::milliseconds(100)));
  EXPECT_EQ(v, 7);
}

TEST(RingBuffer, PopWaitForZeroTimeoutNonBlocking) {
  RingBuffer<int, 4> q;
  int v = -1;
  EXPECT_FALSE(q.PopWaitFor(&v, std::chrono::milliseconds(0)));
  ASSERT_TRUE(q.Push(3));
  ASSERT_TRUE(q.PopWaitFor(&v, std::chrono::milliseconds(0)));
  EXPECT_EQ(v, 3);
}
