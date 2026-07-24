#include "vemory/protocol/dispatcher/VNodeDispatcher.h"

#include "WalEntry.pb.h"
#include "vemory/persist/MutationApply.h"
#include "vemory/protocol/dispatcher/DispatchArgs.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/storage/VNodeIndex.h"

void VNodeDispatcher(const RequestContext& ctx, std::string* reply, void* arg) {
  if (reply == nullptr || arg == nullptr) {
    return;
  }
  auto* args = static_cast<VNodeDispatchArg*>(arg);
  auto* index = args->index;
  if (index == nullptr) {
    RespEncode::AppendError(reply, "ERR index not available");
    return;
  }

  switch (ctx.cmd) {
    case CommandType::kVset: {
      vemory::WalEntry entry;
      entry.set_op(vemory::WalEntry::VSET);
      entry.set_user_key(ctx.user_key);
      entry.set_question(ctx.question);
      entry.set_answer(ctx.answer);
      entry.set_vector(ctx.vector_blob);
      const auto ar = ApplyMutation(entry, MutateSource::kClient, index,
                                    /*kv=*/nullptr, args->wal);
      if (!ar.ok) {
        RespEncode::AppendError(reply, "ERR " + ar.err);
        return;
      }
      RespEncode::AppendOk(reply);
      break;
    }
    case CommandType::kVget: {
      std::string answer;
      const auto st =
          index->Get(ctx.vector_blob, ctx.threshold, &answer);
      if (st == VNodeIndex::Status::kOk) {
        RespEncode::AppendBulkString(reply, answer);
      } else {
        RespEncode::AppendNullBulk(reply);
      }
      break;
    }
    case CommandType::kVdel: {
      vemory::WalEntry entry;
      entry.set_op(vemory::WalEntry::VDEL);
      entry.set_user_key(ctx.user_key);
      const auto ar = ApplyMutation(entry, MutateSource::kClient, index,
                                    /*kv=*/nullptr, args->wal);
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
