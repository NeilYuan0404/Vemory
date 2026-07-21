#include <gtest/gtest.h>

#include "vemory/index/VectorSetRegistry.h"
#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/ProtobufVNodeCodec.h"
#include "vemory/storage/VNode.h"
#include "vemory/storage/VNodeStorage.h"

TEST(ProtobufVNodeCodec, EncodeDecodeRoundTrip) {
  ProtobufVNodeCodec codec;
  VNode in;
  in.id = 7;
  in.prompt = "p";
  in.answer = "a";

  std::string bytes;
  ASSERT_EQ(codec.Encode(in, &bytes), ProtobufVNodeCodec::Status::kOk);
  ASSERT_FALSE(bytes.empty());

  VNode out;
  ASSERT_EQ(codec.Decode(bytes, &out), ProtobufVNodeCodec::Status::kOk);
  EXPECT_EQ(out.id, 7);
  EXPECT_EQ(out.prompt, "p");
  EXPECT_EQ(out.answer, "a");
}

TEST(VNodeStorage, PutAssignsIdAndGetByPrompt) {
  VNodeStorage store;

  VNode node;
  node.prompt = "q";
  node.answer = "a";

  uint16_t id = 0;
  ASSERT_EQ(store.Put(node, &id), VNodeStorage::Status::kOk);
  EXPECT_EQ(id, 1);

  VNode got;
  ASSERT_EQ(store.GetByPrompt("q", &got), VNodeStorage::Status::kOk);
  EXPECT_EQ(got.id, 1);
  EXPECT_EQ(got.answer, "a");
}

TEST(KvStore, SetGetDelHashMap) {
  KvStore store;
  ASSERT_EQ(store.Set("a", "1"), KvStore::Status::kOk);
  ASSERT_EQ(store.Set("b", "2"), KvStore::Status::kOk);
  EXPECT_EQ(store.size(), 2u);

  std::string v;
  ASSERT_EQ(store.Get("a", &v), KvStore::Status::kOk);
  EXPECT_EQ(v, "1");
  ASSERT_EQ(store.Get("missing", &v), KvStore::Status::kNotFound);

  ASSERT_EQ(store.Set("a", "updated"), KvStore::Status::kOk);
  ASSERT_EQ(store.Get("a", &v), KvStore::Status::kOk);
  EXPECT_EQ(v, "updated");

  ASSERT_EQ(store.Del("b"), KvStore::Status::kOk);
  EXPECT_EQ(store.size(), 1u);
  ASSERT_EQ(store.Del("b"), KvStore::Status::kNotFound);
}

TEST(CommandHandler, VaddVdimVembVcard) {
  VectorSetRegistry registry;
  KvStore kv;
  CommandHandler handler(&registry, &kv);

  RequestContext add;
  add.cmd = CommandType::kVadd;
  add.key = "docs";
  add.element = "apple";
  add.embed = {1.f, 0.f};

  std::string reply;
  handler.Dispatch(add, &reply);
  EXPECT_EQ(reply, ":1\r\n");

  RequestContext dim;
  dim.cmd = CommandType::kVdim;
  dim.key = "docs";
  reply.clear();
  handler.Dispatch(dim, &reply);
  EXPECT_EQ(reply, ":2\r\n");

  RequestContext card;
  card.cmd = CommandType::kVcard;
  card.key = "docs";
  reply.clear();
  handler.Dispatch(card, &reply);
  EXPECT_EQ(reply, ":1\r\n");

  RequestContext emb;
  emb.cmd = CommandType::kVemb;
  emb.key = "docs";
  emb.element = "apple";
  reply.clear();
  handler.Dispatch(emb, &reply);
  EXPECT_NE(reply.find("*2\r\n"), std::string::npos);
  EXPECT_NE(reply.find("$1\r\n1\r\n"), std::string::npos);
  EXPECT_NE(reply.find("$1\r\n0\r\n"), std::string::npos);

  RequestContext missing;
  missing.cmd = CommandType::kVcard;
  missing.key = "other";
  reply.clear();
  handler.Dispatch(missing, &reply);
  EXPECT_EQ(reply, ":0\r\n");
}

TEST(CommandHandler, PingEcho) {
  VectorSetRegistry registry;
  KvStore kv;
  CommandHandler handler(&registry, &kv);

  RequestContext ping;
  ping.cmd = CommandType::kPing;
  std::string reply;
  handler.Dispatch(ping, &reply);
  EXPECT_EQ(reply, "+PONG\r\n");

  RequestContext ping_msg;
  ping_msg.cmd = CommandType::kPing;
  ping_msg.element = "hi";
  reply.clear();
  handler.Dispatch(ping_msg, &reply);
  EXPECT_EQ(reply, "$2\r\nhi\r\n");

  RequestContext echo;
  echo.cmd = CommandType::kEcho;
  echo.element = "world";
  reply.clear();
  handler.Dispatch(echo, &reply);
  EXPECT_EQ(reply, "$5\r\nworld\r\n");
}

TEST(CommandHandler, SetGetDel) {
  VectorSetRegistry registry;
  KvStore kv;
  CommandHandler handler(&registry, &kv);

  RequestContext set;
  set.cmd = CommandType::kSet;
  set.key = "k";
  set.element = "hello";
  std::string reply;
  handler.Dispatch(set, &reply);
  EXPECT_EQ(reply, "+OK\r\n");

  RequestContext get;
  get.cmd = CommandType::kGet;
  get.key = "k";
  reply.clear();
  handler.Dispatch(get, &reply);
  EXPECT_EQ(reply, "$5\r\nhello\r\n");

  RequestContext miss;
  miss.cmd = CommandType::kGet;
  miss.key = "missing";
  reply.clear();
  handler.Dispatch(miss, &reply);
  EXPECT_EQ(reply, "$-1\r\n");

  RequestContext del;
  del.cmd = CommandType::kDel;
  del.key = "k";
  reply.clear();
  handler.Dispatch(del, &reply);
  EXPECT_EQ(reply, ":1\r\n");

  reply.clear();
  handler.Dispatch(del, &reply);
  EXPECT_EQ(reply, ":0\r\n");

  RequestContext add;
  add.cmd = CommandType::kVadd;
  add.key = "docs";
  add.element = "apple";
  add.embed = {1.f, 0.f};
  reply.clear();
  handler.Dispatch(add, &reply);
  EXPECT_EQ(reply, ":1\r\n");
}

TEST(CommandHandler, VsimReturnsNearest) {
  VectorSetRegistry registry;
  KvStore kv;
  CommandHandler handler(&registry, &kv);

  RequestContext a;
  a.cmd = CommandType::kVadd;
  a.key = "docs";
  a.element = "near-x";
  a.embed = {1.f, 0.f};
  std::string reply;
  handler.Dispatch(a, &reply);
  EXPECT_EQ(reply, ":1\r\n");

  RequestContext b;
  b.cmd = CommandType::kVadd;
  b.key = "docs";
  b.element = "near-y";
  b.embed = {0.f, 1.f};
  reply.clear();
  handler.Dispatch(b, &reply);
  EXPECT_EQ(reply, ":1\r\n");

  RequestContext search;
  search.cmd = CommandType::kVsim;
  search.key = "docs";
  search.vsim_mode = VsimMode::kValues;
  search.embed = {0.9f, 0.1f};
  search.count = 1;
  search.with_scores = false;
  reply.clear();
  handler.Dispatch(search, &reply);
  EXPECT_NE(reply.find("*1\r\n"), std::string::npos);
  EXPECT_NE(reply.find("$6\r\nnear-x\r\n"), std::string::npos);

  RequestContext by_ele;
  by_ele.cmd = CommandType::kVsim;
  by_ele.key = "docs";
  by_ele.vsim_mode = VsimMode::kEle;
  by_ele.element = "near-x";
  by_ele.count = 2;
  by_ele.with_scores = true;
  reply.clear();
  handler.Dispatch(by_ele, &reply);
  EXPECT_NE(reply.find("near-x"), std::string::npos);
  EXPECT_NE(reply.find("near-y"), std::string::npos);
}
