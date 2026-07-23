#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/storage/KvStore.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Config.h"

namespace {

std::string FloatBlob(const std::vector<float>& v) {
  return std::string(reinterpret_cast<const char*>(v.data()),
                     v.size() * sizeof(float));
}

class TempDir {
 public:
  TempDir() {
    path_ = "/tmp/vemory_snap_XXXXXX";
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

TEST(Config, PersistenceSection) {
  // Uses /tmp ini via same pattern as test_config — inline file.
  const char* path = "/tmp/vemory_persist_cfg.ini";
  {
    FILE* fp = std::fopen(path, "w");
    ASSERT_NE(fp, nullptr);
    std::fputs(
        "[persistence]\n"
        "dir = /var/lib/vemory\n"
        "load_on_startup = true\n",
        fp);
    std::fclose(fp);
  }
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(path, &cfg, &err)) << err;
  EXPECT_EQ(cfg.persistence_dir, "/var/lib/vemory");
  EXPECT_TRUE(cfg.load_on_startup);
  ::unlink(path);
}

TEST(KvStore, DumpLoadRoundTrip) {
  KvStore a;
  ASSERT_EQ(a.Set("k1", "v1"), KvStore::Status::kOk);
  ASSERT_EQ(a.Set("k2", "v2"), KvStore::Status::kOk);

  FILE* fp = std::tmpfile();
  ASSERT_NE(fp, nullptr);
  ASSERT_EQ(a.Dump(fp), KvStore::Status::kOk);
  std::rewind(fp);

  KvStore b;
  ASSERT_EQ(b.Load(fp), KvStore::Status::kOk);
  std::fclose(fp);

  std::string out;
  ASSERT_EQ(b.Get("k1", &out), KvStore::Status::kOk);
  EXPECT_EQ(out, "v1");
  ASSERT_EQ(b.Get("k2", &out), KvStore::Status::kOk);
  EXPECT_EQ(out, "v2");
  EXPECT_EQ(b.size(), 2u);
}

TEST(SnapshotManager, SaveLoadRoundTrip) {
  TempDir dir;
  VNodeIndex idx(64);
  KvStore kv;
  ASSERT_EQ(kv.Set("hello", "world"), KvStore::Status::kOk);

  const auto a = FloatBlob({1.f, 0.f, 0.f});
  const auto b = FloatBlob({0.f, 1.f, 0.f});
  ASSERT_EQ(idx.Set(a, "uk1", "q1", "ans-a"), VNodeIndex::Status::kOk);
  ASSERT_EQ(idx.Set(b, "uk2", "q2", "ans-b"), VNodeIndex::Status::kOk);

  SnapshotManager snap(&idx, &kv, dir.path());
  ASSERT_EQ(snap.SaveToDir(), SnapshotManager::Status::kOk);

  EXPECT_TRUE(std::filesystem::exists(dir.path() + "/dump.meta"));
  EXPECT_TRUE(std::filesystem::exists(dir.path() + "/dump.kv"));
  EXPECT_TRUE(std::filesystem::exists(dir.path() + "/dump.nodes"));
  EXPECT_TRUE(std::filesystem::exists(dir.path() + "/dump.usearch"));

  VNodeIndex idx2(64);
  KvStore kv2;
  SnapshotManager snap2(&idx2, &kv2, dir.path());
  ASSERT_EQ(snap2.Load(), SnapshotManager::Status::kOk);

  std::string val;
  ASSERT_EQ(kv2.Get("hello", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "world");

  std::string answer;
  ASSERT_EQ(idx2.Get(a, 0.2f, &answer), VNodeIndex::Status::kOk);
  EXPECT_EQ(answer, "ans-a");
  ASSERT_EQ(idx2.Get(b, 0.2f, &answer), VNodeIndex::Status::kOk);
  EXPECT_EQ(answer, "ans-b");
  EXPECT_EQ(idx2.dimensions(), 3u);
  EXPECT_EQ(idx2.node_count(), 2u);
}

TEST(SnapshotManager, SaveCommandViaHandler) {
  TempDir dir;
  VNodeIndex idx(16);
  KvStore kv;
  SnapshotManager snap(&idx, &kv, dir.path());
  CommandHandler commands(&idx, &kv, &snap);

  RequestContext ctx;
  ctx.cmd = CommandType::kSave;
  std::string reply;
  commands.Dispatch(ctx, &reply);
  EXPECT_EQ(reply, "+OK\r\n");

  // Wait for background child (bounded).
  for (int i = 0; i < 100 && snap.save_in_progress(); ++i) {
    snap.ReapSaveChild();
    ::usleep(20000);
  }
  EXPECT_FALSE(snap.save_in_progress());
  EXPECT_TRUE(std::filesystem::exists(dir.path() + "/dump.meta"));
}

TEST(SnapshotManager, SaveRequiresDir) {
  VNodeIndex idx(8);
  KvStore kv;
  SnapshotManager snap(&idx, &kv, "");
  CommandHandler commands(&idx, &kv, &snap);
  RequestContext ctx;
  ctx.cmd = CommandType::kSave;
  std::string reply;
  commands.Dispatch(ctx, &reply);
  EXPECT_NE(reply.find("persistence dir not set"), std::string::npos);
}
