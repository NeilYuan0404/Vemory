#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "WalEntry.pb.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/replication/ReplicationBacklog.h"
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

  st = RequestContext::FromTokens(3, {"PSYNC", "extra"}, &ctx);
  EXPECT_EQ(st, RequestContext::Status::kWrongArity);
}

TEST(ReplicationBacklog, FeedAndCopyRange) {
  ReplicationBacklog bl(1024);
  const uint64_t start = bl.tip();

  vemory::WalEntry e;
  e.set_op(vemory::WalEntry::SET);
  e.set_key("k");
  e.set_value("v");
  ASSERT_TRUE(bl.Feed(e));
  ASSERT_GT(bl.tip(), start);
  ASSERT_TRUE(bl.Contains(start));

  std::string out;
  ASSERT_TRUE(bl.CopyRange(start, bl.tip(), &out));
  ASSERT_GE(out.size(), 4u);

  std::string empty;
  ASSERT_TRUE(bl.CopyRange(bl.tip(), bl.tip(), &empty));
  EXPECT_TRUE(empty.empty());
}

TEST(ReplicationBacklog, OverflowAdvancesBase) {
  ReplicationBacklog bl(64);
  const uint64_t start = bl.tip();
  for (int i = 0; i < 20; ++i) {
    vemory::WalEntry e;
    e.set_op(vemory::WalEntry::SET);
    e.set_key("key-" + std::to_string(i));
    e.set_value(std::string(16, 'x'));
    ASSERT_TRUE(bl.Feed(e));
  }
  // Early offset should eventually fall out.
  EXPECT_FALSE(bl.Contains(start));
  EXPECT_TRUE(bl.Contains(bl.base()));
  EXPECT_TRUE(bl.Contains(bl.tip()));
}

TEST(SnapshotManager, SaveToPathWithoutPersistenceDir) {
  TempDir dir;
  VNodeIndex idx(16);
  KvStore kv;
  ASSERT_EQ(kv.Set("a", "b"), KvStore::Status::kOk);

  // Empty persistence dir — SAVE disabled, but path API still works.
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
