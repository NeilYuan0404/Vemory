#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/ProtobufVNodeCodec.h"
#include "vemory/storage/VNode.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/storage/VNodeStorage.h"

namespace {

std::string FloatBlob(const std::vector<float>& vals) {
  std::string out(vals.size() * sizeof(float), '\0');
  std::memcpy(out.data(), vals.data(), out.size());
  return out;
}

}  // namespace

TEST(ProtobufVNodeCodec, EncodeDecodeRoundTrip) {
  ProtobufVNodeCodec codec;
  VNode in;
  in.id = 7;
  in.user_key = "uk";
  in.question = "q";
  in.answer = "a";

  std::string bytes;
  ASSERT_EQ(codec.Encode(in, &bytes), ProtobufVNodeCodec::Status::kOk);
  ASSERT_FALSE(bytes.empty());

  VNode out;
  ASSERT_EQ(codec.Decode(bytes, &out), ProtobufVNodeCodec::Status::kOk);
  EXPECT_EQ(out.id, 7);
  EXPECT_EQ(out.user_key, "uk");
  EXPECT_EQ(out.question, "q");
  EXPECT_EQ(out.answer, "a");
}

TEST(VNodeStorage, PutReusesIdOnUserKeyOverwrite) {
  VNodeStorage store;

  VNode node;
  node.user_key = "uk";
  node.question = "q1";
  node.answer = "a1";

  uint16_t id1 = 0;
  ASSERT_EQ(store.Put(node, &id1), VNodeStorage::Status::kOk);
  EXPECT_EQ(id1, 1);

  node.user_key = "uk";
  node.question = "q2";
  node.answer = "a2";
  uint16_t id2 = 0;
  ASSERT_EQ(store.Put(std::move(node), &id2), VNodeStorage::Status::kOk);
  EXPECT_EQ(id2, id1);

  VNode got;
  ASSERT_EQ(store.GetByUserKey("uk", &got), VNodeStorage::Status::kOk);
  EXPECT_EQ(got.id, id1);
  EXPECT_EQ(got.answer, "a2");
  EXPECT_EQ(store.size(), 1u);
}

TEST(KvStore, SetGetDelHashMap) {
  KvStore store;
  ASSERT_EQ(store.Set("a", "1"), KvStore::Status::kOk);
  std::string v;
  ASSERT_EQ(store.Get("a", &v), KvStore::Status::kOk);
  EXPECT_EQ(v, "1");
  ASSERT_EQ(store.Del("a"), KvStore::Status::kOk);
  EXPECT_EQ(store.Get("a", &v), KvStore::Status::kNotFound);
}

TEST(VNodeIndex, SetGetDelSemantic) {
  VNodeIndex index(64);
  const auto a = FloatBlob({1.f, 0.f});
  const auto b = FloatBlob({0.f, 1.f});
  const auto near_a = FloatBlob({0.99f, 0.01f});

  ASSERT_EQ(index.Set(a, "k1", "q1", "ans-a"), VNodeIndex::Status::kOk);
  ASSERT_EQ(index.Set(b, "k2", "q2", "ans-b"), VNodeIndex::Status::kOk);
  EXPECT_EQ(index.dimensions(), 2u);

  std::string answer;
  ASSERT_EQ(index.Get(near_a, 0.2f, &answer), VNodeIndex::Status::kOk);
  EXPECT_EQ(answer, "ans-a");

  // Far from both stored vectors → miss under moderate distance gate
  EXPECT_EQ(index.Get(FloatBlob({-1.f, 0.f}), 0.3f, &answer),
            VNodeIndex::Status::kNotFound);

  EXPECT_EQ(index.Set(FloatBlob({1.f, 0.f, 0.f}), "k3", "q", "a"),
            VNodeIndex::Status::kDimMismatch);
  EXPECT_EQ(index.Set(std::string("xyz"), "k3", "q", "a"),
            VNodeIndex::Status::kBadVectorSize);

  ASSERT_EQ(index.Del("k1"), VNodeIndex::Status::kOk);
  EXPECT_EQ(index.Del("k1"), VNodeIndex::Status::kNotFound);
}

TEST(CommandHandler, VsetVgetVdel) {
  VNodeIndex vnode(64);
  KvStore kv;
  CommandHandler commands(&vnode, &kv);

  const auto blob = FloatBlob({1.f, 0.f});
  RequestContext set;
  set.cmd = CommandType::kVset;
  set.vector_blob = blob;
  set.user_key = "u1";
  set.question = "hello?";
  set.answer = "world";

  std::string reply;
  commands.Dispatch(set, &reply);
  EXPECT_EQ(reply, "+OK\r\n");

  RequestContext get;
  get.cmd = CommandType::kVget;
  get.vector_blob = FloatBlob({0.95f, 0.05f});
  get.threshold = 0.5f;
  reply.clear();
  commands.Dispatch(get, &reply);
  EXPECT_EQ(reply, "$5\r\nworld\r\n");

  RequestContext miss;
  miss.cmd = CommandType::kVget;
  miss.vector_blob = FloatBlob({0.f, 1.f});
  miss.threshold = 0.01f;
  reply.clear();
  commands.Dispatch(miss, &reply);
  EXPECT_EQ(reply, "$-1\r\n");

  RequestContext del;
  del.cmd = CommandType::kVdel;
  del.user_key = "u1";
  reply.clear();
  commands.Dispatch(del, &reply);
  EXPECT_EQ(reply, ":1\r\n");
  reply.clear();
  commands.Dispatch(del, &reply);
  EXPECT_EQ(reply, ":0\r\n");
}

TEST(CommandHandler, PingEchoAndKv) {
  VNodeIndex vnode(16);
  KvStore kv;
  CommandHandler commands(&vnode, &kv);

  RequestContext ping;
  ping.cmd = CommandType::kPing;
  std::string reply;
  commands.Dispatch(ping, &reply);
  EXPECT_EQ(reply, "+PONG\r\n");

  RequestContext set;
  set.cmd = CommandType::kSet;
  set.key = "a";
  set.element = "1";
  reply.clear();
  commands.Dispatch(set, &reply);
  EXPECT_EQ(reply, "+OK\r\n");
}

TEST(CommandHandler, VsetBadBlob) {
  VNodeIndex vnode(16);
  CommandHandler commands(&vnode, nullptr);
  RequestContext set;
  set.cmd = CommandType::kVset;
  set.vector_blob = "not-floats!";
  set.user_key = "u";
  set.question = "q";
  set.answer = "a";
  std::string reply;
  commands.Dispatch(set, &reply);
  EXPECT_EQ(reply, "-ERR invalid vector byte size\r\n");
}
