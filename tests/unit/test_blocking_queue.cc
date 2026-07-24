#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "vemory/util/BlockingQueue.h"

TEST(BlockingQueue, PushPopMove) {
  BlockingQueue<std::string> q(4);
  std::string big(1024, 'x');
  q.Push(std::move(big));
  EXPECT_TRUE(big.empty());

  std::string out;
  ASSERT_TRUE(q.Pop(&out));
  EXPECT_EQ(out, std::string(1024, 'x'));
  EXPECT_EQ(q.size(), 0u);
  EXPECT_EQ(q.capacity(), 4u);
}

TEST(BlockingQueue, CapacityBlocksUntilPop) {
  BlockingQueue<int> q(1);
  q.Push(1);

  std::atomic<bool> pushed{false};
  std::thread producer([&] {
    q.Push(2);
    pushed.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(pushed.load());

  int v = 0;
  ASSERT_TRUE(q.Pop(&v));
  EXPECT_EQ(v, 1);

  producer.join();
  EXPECT_TRUE(pushed.load());
  ASSERT_TRUE(q.Pop(&v));
  EXPECT_EQ(v, 2);
}

TEST(BlockingQueue, CancelUnblocksEmptyPop) {
  BlockingQueue<int> q(2);
  std::atomic<bool> done{false};
  std::atomic<bool> ok{true};

  std::thread consumer([&] {
    int v = 0;
    ok.store(q.Pop(&v));
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

TEST(BlockingQueue, CancelUnblocksFullPush) {
  BlockingQueue<int> q(1);
  q.Push(1);

  std::atomic<bool> done{false};
  std::thread producer([&] {
    q.Push(2);  // blocks until Cancel
    done.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_FALSE(done.load());
  q.Cancel();
  producer.join();
  EXPECT_TRUE(done.load());
  EXPECT_EQ(q.size(), 1u);  // second Push aborted
}
