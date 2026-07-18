#include "vemory/protocol/KvsDispatcher.h"

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/storage/KvStore.h"

void KvsDispatcher(const RequestContext& ctx, std::string* reply, void* arg) {
  if (reply == nullptr || arg == nullptr) {
    return;
  }
  auto* store = static_cast<KvStore*>(arg);

  switch (ctx.cmd) {
    case CommandType::kSet: {
      const auto st = store->Set(ctx.key, ctx.element);
      if (st != KvStore::Status::kOk) {
        RespEncode::AppendError(reply, "ERR set failed");
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
      const auto st = store->Del(ctx.key);
      if (st == KvStore::Status::kNotFound) {
        RespEncode::AppendInteger(reply, 0);
        return;
      }
      if (st != KvStore::Status::kOk) {
        RespEncode::AppendError(reply, "ERR del failed");
        return;
      }
      RespEncode::AppendInteger(reply, 1);
      break;
    }
    default:
      RespEncode::AppendError(reply, "ERR unknown command");
      break;
  }
}
