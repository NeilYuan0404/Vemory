#include "vemory/protocol/HandlerRegister.h"

#include "vemory/protocol/resp/RespEncode.h"

void HandlerRegister::Register(CommandType cmd, CommandHandlerFn fn,
                               void* arg) {
  if (cmd == CommandType::kUnknown || fn == nullptr) {
    return;
  }
  const auto idx = static_cast<std::size_t>(cmd);
  if (idx >= kCommandTypeCount) {
    return;
  }
  table_[idx] = Entry{fn, arg};
}

void HandlerRegister::Dispatch(const RequestContext& ctx,
                               std::string* reply) const {
  if (reply == nullptr) {
    return;
  }
  if (ctx.cmd == CommandType::kUnknown) {
    RespEncode::AppendError(reply, "ERR unknown command");
    return;
  }
  const auto idx = static_cast<std::size_t>(ctx.cmd);
  if (idx >= kCommandTypeCount || table_[idx].fn == nullptr) {
    RespEncode::AppendError(reply, "ERR unknown command");
    return;
  }
  table_[idx].fn(ctx, reply, table_[idx].arg);
}
