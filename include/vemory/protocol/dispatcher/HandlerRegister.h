#pragma once

#include <string>

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/RequestContext.h"

// Function-pointer entry for one CommandType.
using CommandHandlerFn = void (*)(const RequestContext& ctx, std::string* reply,
                                  void* arg);

// Maps CommandType → handler fn + opaque arg (e.g. VectorSetRegistry*).
class HandlerRegister {
 public:
  void Register(CommandType cmd, CommandHandlerFn fn, void* arg);

  // Looks up ctx.cmd; on miss / null → ERR unknown command.
  void Dispatch(const RequestContext& ctx, std::string* reply) const;

 private:
  struct Entry {
    CommandHandlerFn fn = nullptr;
    void* arg = nullptr;
  };

  Entry table_[kCommandTypeCount]{};
};
