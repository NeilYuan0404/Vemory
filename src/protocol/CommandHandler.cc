#include "vemory/protocol/CommandHandler.h"

#include "vemory/protocol/AssistDispatcher.h"
#include "vemory/protocol/KvsDispatcher.h"
#include "vemory/protocol/VectorDispatcher.h"

CommandHandler::CommandHandler(VectorSetRegistry* registry, KvStore* kv)
    : registry_(registry), kv_(kv) {
  register_.Register(CommandType::kPing, AssistDispatcher, nullptr);
  register_.Register(CommandType::kEcho, AssistDispatcher, nullptr);
  if (registry_ != nullptr) {
    register_.Register(CommandType::kVadd, VectorDispatcher, registry_);
    register_.Register(CommandType::kVsim, VectorDispatcher, registry_);
    register_.Register(CommandType::kVdim, VectorDispatcher, registry_);
    register_.Register(CommandType::kVemb, VectorDispatcher, registry_);
    register_.Register(CommandType::kVcard, VectorDispatcher, registry_);
  }
  if (kv_ != nullptr) {
    register_.Register(CommandType::kSet, KvsDispatcher, kv_);
    register_.Register(CommandType::kDel, KvsDispatcher, kv_);
    register_.Register(CommandType::kGet, KvsDispatcher, kv_);
  }
}

void CommandHandler::Dispatch(const RequestContext& ctx, std::string* reply) {
  if (reply == nullptr) {
    return;
  }
  reply->clear();
  register_.Dispatch(ctx, reply);
}
