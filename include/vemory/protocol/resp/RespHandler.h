#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "vemory/net/MessageBuffer.h"
#include "vemory/protocol/resp/RespDecode.h"

// Glue MessageBuffer <-> RESP decode.
// Views are valid until the matching ReadCompleted(consumed) call.
class RespHandler {
 public:
  using Status = RespDecode::Status;

  // Try to parse one complete request (array of bulk strings) from buf.
  // On kOk: *tokens are string_views into GetAllData(); pass *consumed to
  // MessageBuffer::ReadCompleted after copying / using the views.
  static Status TryParse(MessageBuffer& buf,
                         std::vector<std::string_view>* tokens,
                         size_t* consumed);
};
