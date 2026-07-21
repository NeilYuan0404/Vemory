#include "vemory/protocol/resp/RespProtocolHandler.h"

#include <string_view>
#include <vector>

#include "vemory/protocol/resp/RespDecode.h"

RespProtocolHandler::Status RespProtocolHandler::TryParse(
    int client_fd, MessageBuffer& buf, RequestContext* out, size_t* consumed) {
  if (out == nullptr || consumed == nullptr) {
    return Status::kError;
  }

  auto all = buf.GetAllData();
  if (all.first == nullptr || all.second == 0) {
    return Status::kNeedMore;
  }

  std::vector<std::string_view> tokens;
  const auto wire =
      RespDecode::DecodeArrayOfBulk(all.first, all.second, &tokens, consumed);
  if (wire == RespDecode::Status::kNeedMore) {
    return Status::kNeedMore;
  }
  if (wire == RespDecode::Status::kError) {
    return Status::kError;
  }

  // Map tokens while views are still valid (before caller ReadCompleted).
  // Semantic errors (unknown cmd / wrong arity) still yield a RequestContext
  // for CommandHandler to reply -ERR; only wire failures are kError.
  (void)RequestContext::FromArgv(client_fd, tokens, out);
  return Status::kOk;
}
