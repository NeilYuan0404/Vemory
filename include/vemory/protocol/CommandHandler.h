#pragma once

#include <string>

#include "vemory/index/VectorSetRegistry.h"
#include "vemory/protocol/HandlerRegister.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/storage/KvStore.h"

// Facade: builds HandlerRegister and dispatches parsed requests.
class CommandHandler {
 public:
  CommandHandler(VectorSetRegistry* registry, KvStore* kv);

  // Fills *reply with a RESP-encoded response string.
  void Dispatch(const RequestContext& ctx, std::string* reply);

 private:
  VectorSetRegistry* registry_;
  KvStore* kv_;
  HandlerRegister register_;
};
