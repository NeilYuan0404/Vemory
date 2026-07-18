#include "vemory/protocol/AssistDispatcher.h"

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/resp/RespEncode.h"

void AssistDispatcher(const RequestContext& ctx, std::string* reply,
                      void* arg) {
  (void)arg;
  if (reply == nullptr) {
    return;
  }
  switch (ctx.cmd) {
    case CommandType::kPing:
      if (ctx.element.empty()) {
        RespEncode::AppendSimpleString(reply, "PONG");
      } else {
        RespEncode::AppendBulkString(reply, ctx.element);
      }
      break;
    case CommandType::kEcho:
      RespEncode::AppendBulkString(reply, ctx.element);
      break;
    default:
      RespEncode::AppendError(reply, "ERR unknown command");
      break;
  }
}
