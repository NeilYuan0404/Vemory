#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"

TEST(CommandType, Parse_WireNames) {
  EXPECT_EQ(ParseCommandType("VADD"), CommandType::kVadd);
  EXPECT_EQ(ParseCommandType("VSIM"), CommandType::kVsim);
  EXPECT_EQ(ParseCommandType("VDIM"), CommandType::kVdim);
  EXPECT_EQ(ParseCommandType("VEMB"), CommandType::kVemb);
  EXPECT_EQ(ParseCommandType("VCARD"), CommandType::kVcard);
  EXPECT_EQ(ParseCommandType("SET"), CommandType::kSet);
  EXPECT_EQ(ParseCommandType("DEL"), CommandType::kDel);
  EXPECT_EQ(ParseCommandType("GET"), CommandType::kGet);
  EXPECT_EQ(ParseCommandType("PING"), CommandType::kPing);
  EXPECT_EQ(ParseCommandType("ECHO"), CommandType::kEcho);
  EXPECT_EQ(ParseCommandType("vadd"), CommandType::kVadd);
  EXPECT_EQ(ParseCommandType("Vsim"), CommandType::kVsim);
  EXPECT_EQ(ParseCommandType("set"), CommandType::kSet);
  EXPECT_EQ(ParseCommandType("ping"), CommandType::kPing);
  EXPECT_EQ(ParseCommandType("VSET"), CommandType::kUnknown);
  EXPECT_EQ(ParseCommandType(""), CommandType::kUnknown);
}

TEST(RequestContext, FromArgv_Vadd) {
  RequestContext ctx;
  auto st = RequestContext::FromArgv(
      7,
      std::vector<std::string_view>{"VADD", "docs", "VALUES", "2", "1.0",
                                    "0.5", "apple"},
      &ctx);
  ASSERT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.client_fd, 7);
  EXPECT_EQ(ctx.cmd, CommandType::kVadd);
  EXPECT_EQ(ctx.key, "docs");
  EXPECT_EQ(ctx.element, "apple");
  ASSERT_EQ(ctx.embed.size(), 2u);
  EXPECT_FLOAT_EQ(ctx.embed[0], 1.0f);
  EXPECT_FLOAT_EQ(ctx.embed[1], 0.5f);
  EXPECT_NE(ctx.recv_time.time_since_epoch().count(), 0);
}

TEST(RequestContext, FromArgv_VsimEle) {
  RequestContext ctx;
  auto st = RequestContext::FromArgv(
      1,
      std::vector<std::string_view>{"VSIM", "docs", "ELE", "apple", "COUNT",
                                    "3", "WITHSCORES"},
      &ctx);
  ASSERT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kVsim);
  EXPECT_EQ(ctx.vsim_mode, VsimMode::kEle);
  EXPECT_EQ(ctx.element, "apple");
  EXPECT_EQ(ctx.count, 3u);
  EXPECT_TRUE(ctx.with_scores);
}

TEST(RequestContext, FromArgv_VsimValues) {
  RequestContext ctx;
  auto st = RequestContext::FromArgv(
      1,
      std::vector<std::string_view>{"VSIM", "docs", "VALUES", "2", "1", "0",
                                    "COUNT", "5"},
      &ctx);
  ASSERT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.vsim_mode, VsimMode::kValues);
  ASSERT_EQ(ctx.embed.size(), 2u);
  EXPECT_EQ(ctx.count, 5u);
  EXPECT_FALSE(ctx.with_scores);
}

TEST(RequestContext, FromArgv_VdimVembVcard) {
  RequestContext ctx;
  ASSERT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"VDIM", "k"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kVdim);
  EXPECT_EQ(ctx.key, "k");

  ASSERT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"VEMB", "k", "el"}, &ctx),
            RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kVemb);
  EXPECT_EQ(ctx.element, "el");

  ASSERT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"VCARD", "k"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kVcard);
}

TEST(RequestContext, FromArgv_SetGetDel) {
  RequestContext ctx;
  ASSERT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"SET", "k", "v"}, &ctx),
            RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kSet);
  EXPECT_EQ(ctx.key, "k");
  EXPECT_EQ(ctx.element, "v");

  ASSERT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"GET", "k"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kGet);
  EXPECT_EQ(ctx.key, "k");

  ASSERT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"DEL", "k"},
                               &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kDel);
  EXPECT_EQ(ctx.key, "k");

  EXPECT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"SET", "k"},
                               &ctx),
      RequestContext::Status::kWrongArity);
  EXPECT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"GET", "k", "extra"}, &ctx),
            RequestContext::Status::kWrongArity);
}

TEST(RequestContext, FromArgv_PingEcho) {
  RequestContext ctx;
  ASSERT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"PING"}, &ctx),
      RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kPing);
  EXPECT_TRUE(ctx.element.empty());

  ASSERT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"PING", "hello"}, &ctx),
            RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kPing);
  EXPECT_EQ(ctx.element, "hello");

  ASSERT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"ECHO", "world"}, &ctx),
            RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kEcho);
  EXPECT_EQ(ctx.element, "world");

  EXPECT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"ECHO"}, &ctx),
      RequestContext::Status::kWrongArity);
  EXPECT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"PING", "a", "b"}, &ctx),
            RequestContext::Status::kWrongArity);
}

TEST(RequestContext, FromArgv_Errors) {
  RequestContext ctx;
  EXPECT_EQ(RequestContext::FromArgv(1, {}, &ctx),
            RequestContext::Status::kEmpty);

  EXPECT_EQ(
      RequestContext::FromArgv(1, std::vector<std::string_view>{"FOO"}, &ctx),
      RequestContext::Status::kUnknownCommand);

  EXPECT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"VADD", "k", "VALUES"}, &ctx),
            RequestContext::Status::kWrongArity);

  EXPECT_EQ(
      RequestContext::FromArgv(
          1, std::vector<std::string_view>{"VADD", "k", "FP32", "x", "el"},
          &ctx),
      RequestContext::Status::kBadValue);

  EXPECT_EQ(RequestContext::FromArgv(
                1, std::vector<std::string_view>{"VSIM", "k", "ELE"}, &ctx),
            RequestContext::Status::kWrongArity);
}
