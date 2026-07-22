#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"

namespace {

std::string FloatBlob(std::initializer_list<float> vals) {
  std::string out(vals.size() * sizeof(float), '\0');
  std::memcpy(out.data(), vals.begin(), out.size());
  return out;
}

}  // namespace

TEST(CommandType, Parse_WireNames) {
  EXPECT_EQ(ParseCommandType("VSET"), CommandType::kVset);
  EXPECT_EQ(ParseCommandType("VGET"), CommandType::kVget);
  EXPECT_EQ(ParseCommandType("VDEL"), CommandType::kVdel);
  EXPECT_EQ(ParseCommandType("SET"), CommandType::kSet);
  EXPECT_EQ(ParseCommandType("DEL"), CommandType::kDel);
  EXPECT_EQ(ParseCommandType("GET"), CommandType::kGet);
  EXPECT_EQ(ParseCommandType("PING"), CommandType::kPing);
  EXPECT_EQ(ParseCommandType("ECHO"), CommandType::kEcho);
  EXPECT_EQ(ParseCommandType("vset"), CommandType::kVset);
  EXPECT_EQ(ParseCommandType("VADD"), CommandType::kUnknown);
  EXPECT_EQ(ParseCommandType("VSIM"), CommandType::kUnknown);
  EXPECT_EQ(ParseCommandType(""), CommandType::kUnknown);
}

TEST(RequestContext, FromTokens_Vset) {
  const std::string blob = FloatBlob({1.f, 0.5f});
  RequestContext ctx;
  auto st = RequestContext::FromTokens(
      7,
      std::vector<std::string_view>{"VSET", blob, "uk1", "q?", "a!"},
      &ctx);
  ASSERT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.client_fd, 7);
  EXPECT_EQ(ctx.cmd, CommandType::kVset);
  EXPECT_EQ(ctx.vector_blob, blob);
  EXPECT_EQ(ctx.user_key, "uk1");
  EXPECT_EQ(ctx.question, "q?");
  EXPECT_EQ(ctx.answer, "a!");
  EXPECT_NE(ctx.recv_time.time_since_epoch().count(), 0);
}

TEST(RequestContext, FromTokens_Vget) {
  const std::string blob = FloatBlob({0.1f, 0.2f});
  RequestContext ctx;
  auto st = RequestContext::FromTokens(
      1, std::vector<std::string_view>{"VGET", blob, "0.08"}, &ctx);
  ASSERT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kVget);
  EXPECT_EQ(ctx.vector_blob, blob);
  EXPECT_FLOAT_EQ(ctx.threshold, 0.08f);
}

TEST(RequestContext, FromTokens_Vdel) {
  RequestContext ctx;
  ASSERT_EQ(
      RequestContext::FromTokens(1, std::vector<std::string_view>{"VDEL", "uk"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kVdel);
  EXPECT_EQ(ctx.user_key, "uk");
}

TEST(RequestContext, FromTokens_SetGetDel) {
  RequestContext ctx;
  ASSERT_EQ(RequestContext::FromTokens(
                1, std::vector<std::string_view>{"SET", "k", "v"}, &ctx),
            RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kSet);
  EXPECT_EQ(ctx.key, "k");
  EXPECT_EQ(ctx.element, "v");

  ASSERT_EQ(
      RequestContext::FromTokens(1, std::vector<std::string_view>{"GET", "k"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kGet);

  ASSERT_EQ(
      RequestContext::FromTokens(1, std::vector<std::string_view>{"DEL", "k"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kDel);
}

TEST(RequestContext, FromTokens_PingEcho) {
  RequestContext ctx;
  ASSERT_EQ(
      RequestContext::FromTokens(1, std::vector<std::string_view>{"PING"}, &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kPing);

  ASSERT_EQ(RequestContext::FromTokens(
                1, std::vector<std::string_view>{"ECHO", "world"}, &ctx),
            RequestContext::Status::kOk);
  EXPECT_EQ(ctx.element, "world");
}

TEST(RequestContext, FromTokens_Errors) {
  RequestContext ctx;
  EXPECT_EQ(RequestContext::FromTokens(1, {}, &ctx),
            RequestContext::Status::kEmpty);
  EXPECT_EQ(
      RequestContext::FromTokens(1, std::vector<std::string_view>{"FOO"}, &ctx),
      RequestContext::Status::kUnknownCommand);
  EXPECT_EQ(RequestContext::FromTokens(
                1, std::vector<std::string_view>{"VSET", "b", "k"}, &ctx),
            RequestContext::Status::kWrongArity);
  EXPECT_EQ(RequestContext::FromTokens(
                1, std::vector<std::string_view>{"VGET", "b", "x"}, &ctx),
            RequestContext::Status::kBadValue);
  EXPECT_EQ(
      RequestContext::FromTokens(1, std::vector<std::string_view>{"VDEL", ""},
                               &ctx),
      RequestContext::Status::kBadValue);
}
