#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "vemory/net/MessageBuffer.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/resp/RespDecode.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/protocol/resp/RespProtocolHandler.h"

namespace {

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

TEST(RespEncode, OkAndBulkRoundTripShape) {
  std::string out;
  RespEncode::AppendOk(&out);
  EXPECT_EQ(out, "+OK\r\n");

  out.clear();
  RespEncode::AppendBulkString(&out, "hi");
  EXPECT_EQ(out, "$2\r\nhi\r\n");

  out.clear();
  RespEncode::AppendArrayHeader(&out, 2);
  RespEncode::AppendBulkString(&out, "VGET");
  RespEncode::AppendBulkString(&out, "q");
  EXPECT_EQ(out, "*2\r\n$4\r\nVGET\r\n$1\r\nq\r\n");
}

TEST(RespDecode, ArrayOfBulk_Ok) {
  const char* frame = "*3\r\n$4\r\nVSET\r\n$1\r\na\r\n$1\r\nb\r\n";
  std::vector<std::string_view> bulks;
  size_t consumed = 0;
  auto st = RespDecode::DecodeArrayOfBulk(frame, std::strlen(frame), &bulks,
                                          &consumed);
  ASSERT_EQ(st, RespDecode::Status::kOk);
  EXPECT_EQ(consumed, std::strlen(frame));
  ASSERT_EQ(bulks.size(), 3u);
  EXPECT_EQ(bulks[0], "VSET");
  EXPECT_EQ(bulks[1], "a");
  EXPECT_EQ(bulks[2], "b");
}

TEST(RespDecode, ArrayOfBulk_NeedMore) {
  const char* partial = "*2\r\n$4\r\nVGET\r\n$3\r\nab";
  std::vector<std::string_view> bulks;
  size_t consumed = 0;
  auto st = RespDecode::DecodeArrayOfBulk(partial, std::strlen(partial), &bulks,
                                          &consumed);
  EXPECT_EQ(st, RespDecode::Status::kNeedMore);
  EXPECT_EQ(consumed, 0u);
}

TEST(RespDecode, ArrayOfBulk_Malformed) {
  const char* bad = "+OK\r\n";
  std::vector<std::string_view> bulks;
  size_t consumed = 0;
  auto st =
      RespDecode::DecodeArrayOfBulk(bad, std::strlen(bad), &bulks, &consumed);
  EXPECT_EQ(st, RespDecode::Status::kError);
}

TEST(RespProtocolHandler, TryParseFromMessageBuffer) {
  MessageBuffer buf;
  const char* frame = "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n";
  ASSERT_TRUE(FeedViaSocket(buf, frame, std::strlen(frame)));

  RespProtocolHandler handler;
  RequestContext ctx;
  size_t consumed = 0;
  auto st = handler.TryParse(1, buf, &ctx, &consumed);
  ASSERT_EQ(st, RespProtocolHandler::Status::kOk);
  EXPECT_EQ(consumed, std::strlen(frame));
  EXPECT_EQ(ctx.cmd, CommandType::kGet);
  EXPECT_EQ(ctx.key, "key");

  buf.ReadCompleted(consumed);
  EXPECT_TRUE(buf.Empty());
}
