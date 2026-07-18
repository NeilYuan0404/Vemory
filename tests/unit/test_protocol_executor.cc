#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vemory/net/MessageBuffer.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/ProtocolExecutor.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/resp/RespProtocolHandler.h"

namespace {

bool Feed(MessageBuffer& buf, std::string_view data) {
  int sv[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    return false;
  }
  const ssize_t wn = ::write(sv[0], data.data(), data.size());
  ::close(sv[0]);
  if (wn < 0 || static_cast<size_t>(wn) != data.size()) {
    ::close(sv[1]);
    return false;
  }
  int err = 0;
  const int n = buf.Recv(sv[1], &err);
  ::close(sv[1]);
  return n == static_cast<int>(data.size());
}

}  // namespace

TEST(RespProtocolHandler, TryParse_Vcard) {
  RespProtocolHandler handler;
  MessageBuffer buf;
  const char* frame = "*2\r\n$5\r\nVCARD\r\n$4\r\ndocs\r\n";
  ASSERT_TRUE(Feed(buf, frame));

  RequestContext ctx;
  size_t consumed = 0;
  auto st = handler.TryParse(9, buf, &ctx, &consumed);
  ASSERT_EQ(st, RespProtocolHandler::Status::kOk);
  EXPECT_EQ(consumed, std::strlen(frame));
  EXPECT_EQ(ctx.client_fd, 9);
  EXPECT_EQ(ctx.cmd, CommandType::kVcard);
  EXPECT_EQ(ctx.key, "docs");
}

TEST(RespProtocolHandler, TryParse_NeedMore) {
  RespProtocolHandler handler;
  MessageBuffer buf;
  ASSERT_TRUE(Feed(buf, "*2\r\n$5\r\nVCARD\r\n$3\r\nab"));

  RequestContext ctx;
  size_t consumed = 0;
  auto st = handler.TryParse(1, buf, &ctx, &consumed);
  EXPECT_EQ(st, RespProtocolHandler::Status::kNeedMore);
}

TEST(ProtocolExecutor, StickyPackets_TwoCommands) {
  auto handler = std::make_shared<RespProtocolHandler>();
  std::vector<RequestContext> seen;
  std::string flushed;
  ProtocolExecutor exec(
      handler,
      [&](RequestContext ctx, std::string* reply) {
        seen.push_back(std::move(ctx));
        if (reply != nullptr) {
          reply->assign("+OK\r\n");
        }
      },
      [&](std::string_view data) { flushed.append(data.data(), data.size()); });

  MessageBuffer buf;
  const char* frames =
      "*2\r\n$5\r\nVCARD\r\n$4\r\ndocs\r\n"
      "*2\r\n$4\r\nVDIM\r\n$4\r\ndocs\r\n";
  ASSERT_TRUE(Feed(buf, frames));

  exec.OnReadable(3, buf);
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0].cmd, CommandType::kVcard);
  EXPECT_EQ(seen[0].key, "docs");
  EXPECT_EQ(seen[1].cmd, CommandType::kVdim);
  EXPECT_TRUE(buf.Empty());
  // One write for both replies in the pipelined round.
  EXPECT_EQ(flushed, "+OK\r\n+OK\r\n");
}
