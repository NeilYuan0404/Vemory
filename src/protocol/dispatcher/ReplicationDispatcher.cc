#include "vemory/protocol/dispatcher/ReplicationDispatcher.h"

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/replication/ReplicationMaster.h"

void ReplicationDispatcher(const RequestContext& ctx, std::string* reply,
                           void* arg) {
  if (reply == nullptr) {
    return;
  }
  if (ctx.cmd != CommandType::kPsync) {
    RespEncode::AppendError(reply, "ERR unknown command");
    return;
  }
  auto* master = static_cast<ReplicationMaster*>(arg);
  if (master == nullptr) {
    RespEncode::AppendError(reply, "ERR not a master");
    return;
  }
  master->OnPsync(ctx.client_fd, reply);
}
