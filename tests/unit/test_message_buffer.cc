#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "vemory/net/MessageBuffer.h"

namespace {

// Feed MessageBuffer via a connected socket pair (exercises Recv).
bool FeedViaSocket(MessageBuffer& buf, const char* data, size_t len) {
  int sv[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    return false;
  }
  const ssize_t wn = ::write(sv[0], data, len);
  ::close(sv[0]);
  if (wn < 0 || static_cast<size_t>(wn) != len) {
    ::close(sv[1]);
    return false;
  }
  int err = 0;
  const int n = buf.Recv(sv[1], &err);
  ::close(sv[1]);
  return n == static_cast<int>(len);
}

}  // namespace

TEST(MessageBuffer, EmptyInitially) {
  MessageBuffer buf;
  EXPECT_TRUE(buf.Empty());
  EXPECT_EQ(buf.Size(), 0u);
  EXPECT_EQ(buf.GetDataUntilCRLF().first, nullptr);
  EXPECT_EQ(buf.GetAllData().first, nullptr);
}

TEST(MessageBuffer, GetDataUntilCRLF_CompleteLine) {
  MessageBuffer buf;
  ASSERT_TRUE(FeedViaSocket(buf, "hello\r\n", 7));

  auto line = buf.GetDataUntilCRLF();
  ASSERT_NE(line.first, nullptr);
  EXPECT_EQ(line.second, 5u);
  EXPECT_EQ(std::string(line.first, line.second), "hello");

  buf.ReadCompleted(line.second + 2);
  EXPECT_TRUE(buf.Empty());
}

TEST(MessageBuffer, GetDataUntilCRLF_PartialLine) {
  MessageBuffer buf;
  ASSERT_TRUE(FeedViaSocket(buf, "hel", 3));

  auto line = buf.GetDataUntilCRLF();
  EXPECT_EQ(line.first, nullptr);
  EXPECT_EQ(buf.Size(), 3u);
}

TEST(MessageBuffer, GetDataUntilCRLF_StickyPackets) {
  MessageBuffer buf;
  ASSERT_TRUE(FeedViaSocket(buf, "a\r\nb\r\n", 6));

  auto first = buf.GetDataUntilCRLF();
  ASSERT_NE(first.first, nullptr);
  EXPECT_EQ(std::string(first.first, first.second), "a");
  buf.ReadCompleted(first.second + 2);

  auto second = buf.GetDataUntilCRLF();
  ASSERT_NE(second.first, nullptr);
  EXPECT_EQ(std::string(second.first, second.second), "b");
  buf.ReadCompleted(second.second + 2);
  EXPECT_TRUE(buf.Empty());
}

TEST(MessageBuffer, GetAllData_ThenConsume) {
  MessageBuffer buf;
  ASSERT_TRUE(FeedViaSocket(buf, "xyz", 3));

  auto all = buf.GetAllData();
  ASSERT_NE(all.first, nullptr);
  EXPECT_EQ(all.second, 3u);
  buf.ReadCompleted(all.second);
  EXPECT_TRUE(buf.Empty());
}
