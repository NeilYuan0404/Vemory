#include "vemory/protocol/resp/RespProtocolHandler.h"

#include <string_view>
#include <vector>

#include "vemory/protocol/resp/RespHandler.h"

RespProtocolHandler::Status RespProtocolHandler::TryParse(
    int client_fd, MessageBuffer& buf, RequestContext* out, size_t* consumed) {
  if (out == nullptr || consumed == nullptr) {
    return Status::kError;
  }

  std::vector<std::string_view> tokens;
  const auto wire = RespHandler::TryParse(buf, &tokens, consumed);
  if (wire == RespHandler::Status::kNeedMore) {
    return Status::kNeedMore;
  }
  if (wire == RespHandler::Status::kError) {
    return Status::kError;
  }

  // Map tokens while views are still valid (before caller ReadCompleted).
  // Semantic errors (unknown cmd / wrong arity) still yield a RequestContext
  // for CommandHandler to reply -ERR; only wire failures are kError.
  (void)RequestContext::FromArgv(client_fd, tokens, out);
  return Status::kOk;
}
