#include "vemory/protocol/dispatcher/CommandHandler.h"

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/dispatcher/AssistDispatcher.h"
#include "vemory/protocol/dispatcher/KvsDispatcher.h"
#include "vemory/protocol/dispatcher/PersistDispatcher.h"
#include "vemory/protocol/dispatcher/ReplicationDispatcher.h"
#include "vemory/protocol/dispatcher/VNodeDispatcher.h"
#include "vemory/protocol/resp/RespEncode.h"

namespace {

bool IsReplicaWriteCommand(CommandType cmd) {
  switch (cmd) {
    case CommandType::kSet:
    case CommandType::kDel:
    case CommandType::kVset:
    case CommandType::kVdel:
    case CommandType::kSave:
      return true;
    default:
      return false;
  }
}

}  // namespace

CommandHandler::CommandHandler(VNodeIndex* vnode_index, KvStore* kv,
                               SnapshotManager* snapshot, WalManager* wal,
                               ReplicationMaster* repl, bool replica_readonly)
    : vnode_index_(vnode_index),
      kv_(kv),
      snapshot_(snapshot),
      wal_(wal),
      repl_(repl),
      replica_readonly_(replica_readonly) {
  kvs_arg_.kv = kv_;
  kvs_arg_.wal = wal_;
  kvs_arg_.repl = repl_;
  vnode_arg_.index = vnode_index_;
  vnode_arg_.wal = wal_;
  vnode_arg_.repl = repl_;

  register_.Register(CommandType::kPing, AssistDispatcher, nullptr);
  register_.Register(CommandType::kEcho, AssistDispatcher, nullptr);
  if (vnode_index_ != nullptr) {
    register_.Register(CommandType::kVset, VNodeDispatcher, &vnode_arg_);
    register_.Register(CommandType::kVget, VNodeDispatcher, &vnode_arg_);
    register_.Register(CommandType::kVdel, VNodeDispatcher, &vnode_arg_);
  }
  if (kv_ != nullptr) {
    register_.Register(CommandType::kSet, KvsDispatcher, &kvs_arg_);
    register_.Register(CommandType::kDel, KvsDispatcher, &kvs_arg_);
    register_.Register(CommandType::kGet, KvsDispatcher, &kvs_arg_);
  }
  if (snapshot_ != nullptr) {
    register_.Register(CommandType::kSave, PersistDispatcher, snapshot_);
  }
  if (repl_ != nullptr) {
    register_.Register(CommandType::kPsync, ReplicationDispatcher, repl_);
  }
}

void CommandHandler::Dispatch(const RequestContext& ctx, std::string* reply) {
  if (reply == nullptr) {
    return;
  }
  reply->clear();
  if (replica_readonly_ && IsReplicaWriteCommand(ctx.cmd)) {
    RespEncode::AppendError(
        reply, "READONLY You can't write against a read only replica.");
    return;
  }
  register_.Dispatch(ctx, reply);
}
