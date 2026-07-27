#include <gtest/gtest.h>

#include <string>

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/persist/SnapshotManager.h"

namespace {

constexpr const char* kReadonly =
    "-READONLY You can't write against a read only replica.\r\n";

}  // namespace

TEST(ReplicaReadonly, RejectsClientWrites) {
  VNodeIndex vnode(16);
  KvStore kv;
  SnapshotManager snap(&vnode, &kv, /*dir=*/"");
  CommandHandler commands(&vnode, &kv, &snap, /*wal=*/nullptr, /*repl=*/nullptr,
                          /*replica_readonly=*/true);

  RequestContext set;
  set.cmd = CommandType::kSet;
  set.key = "a";
  set.element = "1";
  std::string reply;
  commands.Dispatch(set, &reply);
  EXPECT_EQ(reply, kReadonly);

  RequestContext del;
  del.cmd = CommandType::kDel;
  del.key = "a";
  reply.clear();
  commands.Dispatch(del, &reply);
  EXPECT_EQ(reply, kReadonly);

  RequestContext vdel;
  vdel.cmd = CommandType::kVdel;
  vdel.user_key = "u";
  reply.clear();
  commands.Dispatch(vdel, &reply);
  EXPECT_EQ(reply, kReadonly);

  RequestContext save;
  save.cmd = CommandType::kSave;
  reply.clear();
  commands.Dispatch(save, &reply);
  EXPECT_EQ(reply, kReadonly);
}

TEST(ReplicaReadonly, AllowsReadsAndAssist) {
  VNodeIndex vnode(16);
  KvStore kv;
  kv.Set("a", "1");
  CommandHandler commands(&vnode, &kv, /*snapshot=*/nullptr, /*wal=*/nullptr,
                          /*repl=*/nullptr, /*replica_readonly=*/true);

  RequestContext ping;
  ping.cmd = CommandType::kPing;
  std::string reply;
  commands.Dispatch(ping, &reply);
  EXPECT_EQ(reply, "+PONG\r\n");

  RequestContext get;
  get.cmd = CommandType::kGet;
  get.key = "a";
  reply.clear();
  commands.Dispatch(get, &reply);
  EXPECT_EQ(reply, "$1\r\n1\r\n");
}

TEST(ReplicaReadonly, MasterStillAcceptsWrites) {
  VNodeIndex vnode(16);
  KvStore kv;
  CommandHandler commands(&vnode, &kv);

  RequestContext set;
  set.cmd = CommandType::kSet;
  set.key = "a";
  set.element = "1";
  std::string reply;
  commands.Dispatch(set, &reply);
  EXPECT_EQ(reply, "+OK\r\n");
}
