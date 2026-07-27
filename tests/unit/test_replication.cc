#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "vemory/persist/SnapshotManager.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/replication/ReplicationBacklog.h"
#include "vemory/replication/ReplicationSlave.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

namespace {

class TempDir {
 public:
  TempDir() {
    path_ = "/tmp/vemory_repl_XXXXXX";
    std::string tmpl = path_;
    char* p = ::mkdtemp(tmpl.data());
    EXPECT_NE(p, nullptr);
    if (p != nullptr) {
      path_ = tmpl;
    }
  }
  ~TempDir() {
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(path_, ec);
    }
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

std::string EncodeSet(const std::string& key, const std::string& val) {
  RequestContext ctx;
  ctx.cmd = CommandType::kSet;
  ctx.key = key;
  ctx.element = val;
  std::string frame;
  EXPECT_TRUE(RespEncode::EncodeWriteCommand(ctx, &frame));
  return frame;
}

}  // namespace

TEST(CommandType, ParsePsync) {
  EXPECT_EQ(ParseCommandType("PSYNC"), CommandType::kPsync);
  EXPECT_EQ(ParseCommandType("psync"), CommandType::kPsync);
}

TEST(RequestContext, FromTokens_Psync) {
  RequestContext ctx;
  auto st = RequestContext::FromTokens(3, {"PSYNC"}, &ctx);
  EXPECT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.cmd, CommandType::kPsync);
  EXPECT_EQ(ctx.client_fd, 3);
  EXPECT_TRUE(ctx.psync_replid.empty());
  EXPECT_EQ(ctx.psync_offset, -1);

  st = RequestContext::FromTokens(3, {"PSYNC", "?", "-1"}, &ctx);
  EXPECT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.psync_replid, "?");
  EXPECT_EQ(ctx.psync_offset, -1);

  st = RequestContext::FromTokens(
      3, {"PSYNC", "abc123", "42"}, &ctx);
  EXPECT_EQ(st, RequestContext::Status::kOk);
  EXPECT_EQ(ctx.psync_replid, "abc123");
  EXPECT_EQ(ctx.psync_offset, 42);

  st = RequestContext::FromTokens(3, {"PSYNC", "extra"}, &ctx);
  EXPECT_EQ(st, RequestContext::Status::kWrongArity);

  st = RequestContext::FromTokens(3, {"PSYNC", "id", "nope"}, &ctx);
  EXPECT_EQ(st, RequestContext::Status::kBadValue);
}

TEST(ReplicationBacklog, FeedAndCopyRange) {
  ReplicationBacklog bl(1024);
  const uint64_t start = bl.tip();

  const std::string frame = EncodeSet("k", "v");
  ASSERT_TRUE(bl.FeedEncoded(frame));
  ASSERT_GT(bl.tip(), start);
  ASSERT_TRUE(bl.Contains(start));

  std::string out;
  ASSERT_TRUE(bl.CopyRange(start, bl.tip(), &out));
  EXPECT_EQ(out, frame);

  std::string empty;
  ASSERT_TRUE(bl.CopyRange(bl.tip(), bl.tip(), &empty));
  EXPECT_TRUE(empty.empty());
}

TEST(ReplicationBacklog, OverflowAdvancesBase) {
  ReplicationBacklog bl(64);
  const uint64_t start = bl.tip();
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(bl.FeedEncoded(
        EncodeSet("key-" + std::to_string(i), std::string(16, 'x'))));
  }
  EXPECT_FALSE(bl.Contains(start));
  EXPECT_TRUE(bl.Contains(bl.base()));
  EXPECT_TRUE(bl.Contains(bl.tip()));
}

TEST(ReplicationBacklog, FeedEncodedRoundTrip) {
  ReplicationBacklog bl(1024);
  const std::string frame = EncodeSet("x", "y");
  const uint64_t start = bl.tip();
  ASSERT_TRUE(bl.FeedEncoded(frame));
  std::string out;
  ASSERT_TRUE(bl.CopyRange(start, bl.tip(), &out));
  EXPECT_EQ(out, frame);
}

TEST(ReplicationSlave, NextBackoffMs) {
  EXPECT_EQ(ReplicationSlave::NextBackoffMs(1000), 2000u);
  EXPECT_EQ(ReplicationSlave::NextBackoffMs(2000), 4000u);
  EXPECT_EQ(ReplicationSlave::NextBackoffMs(32000), 60000u);
  EXPECT_EQ(ReplicationSlave::NextBackoffMs(60000), 60000u);
  EXPECT_EQ(ReplicationSlave::NextBackoffMs(60001), 60000u);
}

TEST(ReplicationSlave, ConsumeStreamFrames_BytesConsumed) {
  VNodeIndex idx(16);
  KvStore kv;
  const std::string frame = EncodeSet("c", "3");
  std::string buf = frame;
  std::string err;
  std::size_t consumed = 0;
  ASSERT_TRUE(ReplicationSlave::ConsumeStreamFrames(&buf, &idx, &kv, &err,
                                                    &consumed));
  EXPECT_EQ(consumed, frame.size());
  EXPECT_TRUE(buf.empty());
}

TEST(ReplicationSlave, ConsumeStreamFrames_PartialAndMulti) {
  VNodeIndex idx(16);
  KvStore kv;

  const std::string f1 = EncodeSet("a", "1");
  const std::string f2 = EncodeSet("b", "2");

  std::string buf = f1 + f2;
  buf.pop_back();
  std::string err;
  ASSERT_TRUE(
      ReplicationSlave::ConsumeStreamFrames(&buf, &idx, &kv, &err));
  EXPECT_EQ(buf.size(), f2.size() - 1);
  std::string val;
  ASSERT_EQ(kv.Get("a", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "1");
  EXPECT_EQ(kv.Get("b", &val), KvStore::Status::kNotFound);

  buf.push_back(f2.back());
  ASSERT_TRUE(
      ReplicationSlave::ConsumeStreamFrames(&buf, &idx, &kv, &err));
  EXPECT_TRUE(buf.empty());
  ASSERT_EQ(kv.Get("b", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "2");
}

TEST(SnapshotManager, SaveToPathWithoutPersistenceDir) {
  TempDir dir;
  VNodeIndex idx(16);
  KvStore kv;
  ASSERT_EQ(kv.Set("a", "b"), KvStore::Status::kOk);

  SnapshotManager snap(&idx, &kv, "");
  EXPECT_FALSE(snap.configured());
  EXPECT_EQ(snap.BackgroundSave(), SnapshotManager::Status::kNotConfigured);

  const std::string path = dir.path() + "/repl.rdb";
  ASSERT_EQ(snap.BackgroundSaveToPath(path), SnapshotManager::Status::kOk);
  for (int i = 0; i < 100 && snap.save_in_progress(); ++i) {
    snap.ReapSaveChild();
    ::usleep(20000);
  }
  ASSERT_FALSE(snap.save_in_progress());
  ASSERT_TRUE(std::filesystem::exists(path));

  VNodeIndex idx2(16);
  KvStore kv2;
  SnapshotManager snap2(&idx2, &kv2, "");
  ASSERT_EQ(snap2.LoadFromPath(path), SnapshotManager::Status::kOk);
  std::string val;
  ASSERT_EQ(kv2.Get("a", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "b");
}
