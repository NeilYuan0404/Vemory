#pragma once

#include <cstddef>
#include <cstdint>

#include "vemory/net/MessageBuffer.h"
#include "vemory/protocol/RequestContext.h"

// RESP parse: MessageBuffer → DecodeArrayOfBulk → RequestContext (via FromTokens).
class RespProtocolHandler {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kNeedMore,
    kError,
  };

  // On kOk: *out is filled (owned strings), *consumed is frame size to pass to
  // MessageBuffer::ReadCompleted. Callers must not call FromTokens separately.
  Status TryParse(int client_fd, MessageBuffer& buf, RequestContext* out,
                  size_t* consumed);
};
