#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "WalEntry.pb.h"
#include "vemory/persist/MutationApply.h"
#include "vemory/persist/WalManager.h"
#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/storage/KvStore.h"
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
    path_ = "/tmp/vemory_aof_XXXXXX";
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

TEST(WalEntry, ProtobufRoundTrip) {
  vemory::WalEntry in;
  in.set_op(vemory::WalEntry::VSET);
  in.set_user_key("uk");
  in.set_question("q");
  in.set_answer("a");
  in.set_vector(FloatBlob({1.f, 0.f, 0.f}));

  std::string bytes;
  ASSERT_TRUE(in.SerializeToString(&bytes));

  vemory::WalEntry out;
  ASSERT_TRUE(out.ParseFromString(bytes));
  EXPECT_EQ(out.op(), vemory::WalEntry::VSET);
  EXPECT_EQ(out.user_key(), "uk");
  EXPECT_EQ(out.answer(), "a");
  EXPECT_EQ(out.vector().size(), 3 * sizeof(float));
}

TEST(WalManager, AppendReplayRoundTrip) {
  TempDir dir;
  VNodeIndex idx(64);
  KvStore kv;
  {
    WalManager wal(&idx, &kv, dir.path(), /*enable=*/true);

    {
      vemory::WalEntry e;
      e.set_op(vemory::WalEntry::SET);
      e.set_key("k1");
      e.set_value("v1");
      ASSERT_EQ(wal.Append(e), WalManager::Status::kOk);
    }
    {
      vemory::WalEntry e;
      e.set_op(vemory::WalEntry::VSET);
      e.set_user_key("uk1");
      e.set_question("q1");
      e.set_answer("ans1");
      e.set_vector(FloatBlob({1.f, 0.f, 0.f}));
      ASSERT_EQ(wal.Append(e), WalManager::Status::kOk);
    }
    ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);
  }

  VNodeIndex idx2(64);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), /*enable=*/true);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);

  std::string val;
  ASSERT_EQ(kv2.Get("k1", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "v1");

  std::string answer;
  ASSERT_EQ(idx2.Get(FloatBlob({1.f, 0.f, 0.f}), 0.2f, &answer),
            VNodeIndex::Status::kOk);
  EXPECT_EQ(answer, "ans1");
}

TEST(WalManager, ReplayDoesNotReAppend) {
  TempDir dir;
  VNodeIndex idx(32);
  KvStore kv;
  std::uintmax_t size_before = 0;
  {
    WalManager wal(&idx, &kv, dir.path(), true);

    vemory::WalEntry e;
    e.set_op(vemory::WalEntry::SET);
    e.set_key("only");
    e.set_value("once");
    ASSERT_EQ(wal.Append(e), WalManager::Status::kOk);
    ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);

    size_before = std::filesystem::file_size(dir.path() + "/appendonly.aof");
  }

  VNodeIndex idx2(32);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), true);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);

  const auto size_after =
      std::filesystem::file_size(dir.path() + "/appendonly.aof");
  EXPECT_EQ(size_before, size_after);

  std::string val;
  ASSERT_EQ(kv2.Get("only", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "once");
}

TEST(WalManager, ClientPathViaHandler) {
  TempDir dir;
  VNodeIndex idx(32);
  KvStore kv;
  {
    WalManager wal(&idx, &kv, dir.path(), true);
    CommandHandler commands(&idx, &kv, /*snapshot=*/nullptr, &wal);

    RequestContext set_ctx;
    set_ctx.cmd = CommandType::kSet;
    set_ctx.key = "hello";
    set_ctx.element = "world";
    std::string reply;
    commands.Dispatch(set_ctx, &reply);
    EXPECT_EQ(reply, "+OK\r\n");

    RequestContext vset_ctx;
    vset_ctx.cmd = CommandType::kVset;
    vset_ctx.vector_blob = FloatBlob({0.f, 1.f, 0.f});
    vset_ctx.user_key = "u1";
    vset_ctx.question = "q";
    vset_ctx.answer = "a";
    reply.clear();
    commands.Dispatch(vset_ctx, &reply);
    EXPECT_EQ(reply, "+OK\r\n");

    ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);
    EXPECT_TRUE(std::filesystem::exists(dir.path() + "/appendonly.aof"));
  }

  VNodeIndex idx2(32);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), true);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);

  std::string val;
  ASSERT_EQ(kv2.Get("hello", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "world");
  std::string answer;
  ASSERT_EQ(idx2.Get(FloatBlob({0.f, 1.f, 0.f}), 0.2f, &answer),
            VNodeIndex::Status::kOk);
  EXPECT_EQ(answer, "a");
}

TEST(WalManager, MultipleAppendFlush) {
  TempDir dir;
  VNodeIndex idx(32);
  KvStore kv;
  WalManager wal(&idx, &kv, dir.path(), true);

  for (int i = 0; i < 8; ++i) {
    vemory::WalEntry e;
    e.set_op(vemory::WalEntry::SET);
    e.set_key("k" + std::to_string(i));
    e.set_value("v" + std::to_string(i));
    ASSERT_EQ(wal.Append(e), WalManager::Status::kOk);
  }
  ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);

  VNodeIndex idx2(32);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), true);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);
  for (int i = 0; i < 8; ++i) {
    std::string val;
    ASSERT_EQ(kv2.Get("k" + std::to_string(i), &val), KvStore::Status::kOk);
    EXPECT_EQ(val, "v" + std::to_string(i));
  }
}

TEST(Config, AofKey) {
  const char* path = "/tmp/vemory_aof_cfg.ini";
  {
    FILE* fp = std::fopen(path, "w");
    ASSERT_NE(fp, nullptr);
    std::fputs(
        "[persistence]\n"
        "dir = /tmp/x\n"
        "aof = true\n",
        fp);
    std::fclose(fp);
  }
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(path, &cfg, &err)) << err;
  EXPECT_TRUE(cfg.aof);
  EXPECT_EQ(cfg.persistence_dir, "/tmp/x");
  ::unlink(path);
}
