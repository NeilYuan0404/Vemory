#include "vemory/protocol/dispatcher/VNodeDispatcher.h"

#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/storage/VNodeIndex.h"

void VNodeDispatcher(const RequestContext& ctx, std::string* reply, void* arg) {
  if (reply == nullptr || arg == nullptr) {
    return;
  }
  auto* index = static_cast<VNodeIndex*>(arg);

  switch (ctx.cmd) {
    case CommandType::kVset: {
      const auto st = index->Set(ctx.vector_blob, ctx.user_key, ctx.question,
                                 ctx.answer);
      switch (st) {
        case VNodeIndex::Status::kOk:
          RespEncode::AppendOk(reply);
          break;
        case VNodeIndex::Status::kBadValue:
          RespEncode::AppendError(reply, "ERR invalid VSET arguments");
          break;
        case VNodeIndex::Status::kBadVectorSize:
          RespEncode::AppendError(reply, "ERR invalid vector byte size");
          break;
        case VNodeIndex::Status::kDimMismatch:
          RespEncode::AppendError(reply, "ERR vector dimension mismatch");
          break;
        case VNodeIndex::Status::kIndexInitFailed:
          RespEncode::AppendError(reply, "ERR usearch init failed");
          break;
        default:
          RespEncode::AppendError(reply, "ERR vset failed");
          break;
      }
      break;
    }
    case CommandType::kVget: {
      std::string answer;
      const auto st =
          index->Get(ctx.vector_blob, ctx.threshold, &answer);
      if (st == VNodeIndex::Status::kOk) {
        RespEncode::AppendBulkString(reply, answer);
      } else {
        // Miss or illegal query → null bulk per protocol.
        RespEncode::AppendNullBulk(reply);
      }
      break;
    }
    case CommandType::kVdel: {
      const auto st = index->Del(ctx.user_key);
      if (st == VNodeIndex::Status::kOk) {
        RespEncode::AppendInteger(reply, 1);
      } else {
        RespEncode::AppendInteger(reply, 0);
      }
      break;
    }
    default:
      RespEncode::AppendError(reply, "ERR unknown command");
      break;
  }
}
