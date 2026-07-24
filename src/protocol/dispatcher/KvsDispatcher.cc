#include "vemory/protocol/dispatcher/KvsDispatcher.h"

#include "WalEntry.pb.h"
#include "vemory/persist/MutationApply.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/dispatcher/DispatchArgs.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/storage/KvStore.h"

void KvsDispatcher(const RequestContext& ctx, std::string* reply, void* arg) {
  if (reply == nullptr || arg == nullptr) {
    return;
  }
  auto* args = static_cast<KvsDispatchArg*>(arg);
  auto* store = args->kv;
  if (store == nullptr) {
    RespEncode::AppendError(reply, "ERR kv not available");
    return;
  }

  switch (ctx.cmd) {
    case CommandType::kSet: {
      vemory::WalEntry entry;
      entry.set_op(vemory::WalEntry::SET);
      entry.set_key(ctx.key);
      entry.set_value(ctx.element);
      const auto ar = ApplyMutation(entry, MutateSource::kClient,
                                    /*vnode_index=*/nullptr, store, args->wal);
      if (!ar.ok) {
        RespEncode::AppendError(reply, "ERR " + ar.err);
        return;
      }
      RespEncode::AppendOk(reply);
      break;
    }
    case CommandType::kGet: {
      std::string value;
      const auto st = store->Get(ctx.key, &value);
      if (st == KvStore::Status::kNotFound) {
        RespEncode::AppendNullBulk(reply);
        return;
      }
      if (st != KvStore::Status::kOk) {
        RespEncode::AppendError(reply, "ERR get failed");
        return;
      }
      RespEncode::AppendBulkString(reply, value);
      break;
    }
    case CommandType::kDel: {
      vemory::WalEntry entry;
      entry.set_op(vemory::WalEntry::DEL);
      entry.set_key(ctx.key);
      const auto ar = ApplyMutation(entry, MutateSource::kClient,
                                    /*vnode_index=*/nullptr, store, args->wal);
      if (!ar.ok) {
        RespEncode::AppendError(reply, "ERR " + ar.err);
        return;
      }
      RespEncode::AppendInteger(reply, ar.integer_reply);
      break;
    }
    default:
      RespEncode::AppendError(reply, "ERR unknown command");
      break;
  }
}
