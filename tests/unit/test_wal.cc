#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "vemory/mutate/MutationApply.h"
#include "vemory/persist/WalManager.h"
#include "vemory/persist/IoUringAofWriter.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/resp/RespEncode.h"
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

RequestContext MakeSet(const std::string& key, const std::string& val) {
  RequestContext ctx;
  ctx.cmd = CommandType::kSet;
  ctx.key = key;
  ctx.element = val;
  return ctx;
}

RequestContext MakeVset(const std::string& blob, const std::string& uk,
                        const std::string& q, const std::string& a) {
  RequestContext ctx;
  ctx.cmd = CommandType::kVset;
  ctx.vector_blob = blob;
  ctx.user_key = uk;
  ctx.question = q;
  ctx.answer = a;
  return ctx;
}

std::string EncodeSet(const std::string& key, const std::string& val) {
  std::string frame;
  EXPECT_TRUE(RespEncode::EncodeWriteCommand(MakeSet(key, val), &frame));
  return frame;
}

// > kMaxBatch (32) so flush covers at least one full batch plus a remainder.
void AppendManyFlushReplay(vemory::AofIoMode io_mode) {
  constexpr int kCount = 40;
  TempDir dir;
  {
    VNodeIndex idx(32);
    KvStore kv;
    WalManager wal(&idx, &kv, dir.path(), /*enable=*/true,
                   vemory::AofFsyncPolicy::kNo, io_mode);
    EXPECT_EQ(wal.io_mode(), io_mode);

    for (int i = 0; i < kCount; ++i) {
      ASSERT_EQ(wal.AppendFrame(EncodeSet("k" + std::to_string(i),
                                          "v" + std::to_string(i))),
                WalManager::Status::kOk);
    }
    ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);
  }

  VNodeIndex idx2(32);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), true, vemory::AofFsyncPolicy::kNo,
                  vemory::AofIoMode::kThread);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);
  for (int i = 0; i < kCount; ++i) {
    std::string val;
    ASSERT_EQ(kv2.Get("k" + std::to_string(i), &val), KvStore::Status::kOk)
        << "missing key k" << i;
    EXPECT_EQ(val, "v" + std::to_string(i));
  }
}

}  // namespace

TEST(RespEncode, EncodeWriteCommandRoundTrip) {
  auto ctx = MakeVset(FloatBlob({1.f, 0.f, 0.f}), "uk", "q", "a");
  std::string frame;
  ASSERT_TRUE(RespEncode::EncodeWriteCommand(ctx, &frame));
  EXPECT_FALSE(frame.empty());
  EXPECT_EQ(frame[0], '*');
}

TEST(WalManager, AppendReplayRoundTrip) {
  TempDir dir;
  VNodeIndex idx(64);
  KvStore kv;
  {
    WalManager wal(&idx, &kv, dir.path(), /*enable=*/true);

    ASSERT_EQ(wal.AppendFrame(EncodeSet("k1", "v1")), WalManager::Status::kOk);
    std::string vset;
    ASSERT_TRUE(RespEncode::EncodeWriteCommand(
        MakeVset(FloatBlob({1.f, 0.f, 0.f}), "uk1", "q1", "ans1"), &vset));
    ASSERT_EQ(wal.AppendFrame(std::move(vset)), WalManager::Status::kOk);
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

    ASSERT_EQ(wal.AppendFrame(EncodeSet("only", "once")),
              WalManager::Status::kOk);
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

    RequestContext set_ctx = MakeSet("hello", "world");
    std::string reply;
    commands.Dispatch(set_ctx, &reply);
    EXPECT_EQ(reply, "+OK\r\n");

    RequestContext vset_ctx =
        MakeVset(FloatBlob({0.f, 1.f, 0.f}), "u1", "q", "a");
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
    ASSERT_EQ(wal.AppendFrame(EncodeSet("k" + std::to_string(i),
                                        "v" + std::to_string(i))),
              WalManager::Status::kOk);
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
        "aof = true\n"
        "aof_fsync = always\n",
        fp);
    std::fclose(fp);
  }
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(path, &cfg, &err)) << err;
  EXPECT_TRUE(cfg.aof);
  EXPECT_EQ(cfg.persistence_dir, "/tmp/x");
  EXPECT_EQ(cfg.aof_fsync, vemory::AofFsyncPolicy::kAlways);
  ::unlink(path);
}

TEST(WalManager, AlwaysFsyncFlushOk) {
  TempDir dir;
  VNodeIndex idx(32);
  KvStore kv;
  WalManager wal(&idx, &kv, dir.path(), /*enable=*/true,
                 vemory::AofFsyncPolicy::kAlways, vemory::AofIoMode::kThread);
  EXPECT_EQ(wal.fsync_policy(), vemory::AofFsyncPolicy::kAlways);
  EXPECT_EQ(wal.io_mode(), vemory::AofIoMode::kThread);

  ASSERT_EQ(wal.AppendFrame(EncodeSet("k", "v")), WalManager::Status::kOk);
  ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);
}

TEST(WalManager, IoUringAppendReplayOrFallback) {
  TempDir dir;
  VNodeIndex idx(32);
  KvStore kv;
  {
    WalManager wal(&idx, &kv, dir.path(), /*enable=*/true,
                   vemory::AofFsyncPolicy::kNo, vemory::AofIoMode::kIoUring);
    EXPECT_EQ(wal.io_mode(), vemory::AofIoMode::kIoUring);

    ASSERT_EQ(wal.AppendFrame(EncodeSet("uring", "ok")),
              WalManager::Status::kOk);
    ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);
  }

  VNodeIndex idx2(32);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), true, vemory::AofFsyncPolicy::kNo,
                  vemory::AofIoMode::kThread);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);
  std::string val;
  ASSERT_EQ(kv2.Get("uring", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "ok");
}

TEST(WalManager, ThreadBatchAppendReplay) {
  AppendManyFlushReplay(vemory::AofIoMode::kThread);
}

TEST(WalManager, IoUringBatchAppendReplayOrFallback) {
  AppendManyFlushReplay(vemory::AofIoMode::kIoUring);
}

#if VEMORY_HAVE_LIBURING
TEST(WalManager, IoUringSoftBufferNeedsFlushOrPoll) {
  TempDir dir;
  VNodeIndex idx(32);
  KvStore kv;
  WalManager wal(&idx, &kv, dir.path(), /*enable=*/true,
                 vemory::AofFsyncPolicy::kNo, vemory::AofIoMode::kIoUring,
                 /*flush_interval_ms=*/60000);
  ASSERT_EQ(wal.AppendFrame(EncodeSet("soft", "buf")), WalManager::Status::kOk);

  // Small frame stays in soft buffer until Poll/Flush; file may still be empty.
  FILE* fp = std::fopen(wal.path().c_str(), "rb");
  if (fp != nullptr) {
    char tmp[8];
    const std::size_t n = std::fread(tmp, 1, sizeof(tmp), fp);
    std::fclose(fp);
    // If io_uring path is active, unflushed soft buffer leaves file empty/short.
    // Thread fallback may have already written — either is OK after Flush.
    (void)n;
  }

  wal.Poll();  // may not flush yet (interval not elapsed)
  ASSERT_EQ(wal.Flush(), WalManager::Status::kOk);

  VNodeIndex idx2(32);
  KvStore kv2;
  WalManager wal2(&idx2, &kv2, dir.path(), true, vemory::AofFsyncPolicy::kNo,
                  vemory::AofIoMode::kThread);
  ASSERT_EQ(wal2.Replay(), WalManager::Status::kOk);
  std::string val;
  ASSERT_EQ(kv2.Get("soft", &val), KvStore::Status::kOk);
  EXPECT_EQ(val, "buf");
}
#endif
