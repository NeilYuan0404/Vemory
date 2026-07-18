#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "vemory/net/MessageBuffer.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/protocol/resp/RespProtocolHandler.h"

// Sticky-packet parse loop: repeatedly TryParse until NeedMore / Error.
// Replies from one OnReadable round are appended, then flushed once (pipeline batch).
class ProtocolExecutor {
 public:
  // Fill *reply for this command (may leave empty).
  using DispatchCallback =
      std::function<void(RequestContext ctx, std::string* reply)>;
  // One write for the batched replies of this read round.
  using WriteCallback =
      std::function<void(std::string_view data)>;
  using ErrorCallback = std::function<void()>;

  ProtocolExecutor(std::shared_ptr<RespProtocolHandler> handler,
                   DispatchCallback on_ok,
                   WriteCallback on_write,
                   ErrorCallback on_error = nullptr);

  // Drain complete frames from buf. Must run on the I/O thread.
  void OnReadable(int client_fd, MessageBuffer& buf);

 private:
  std::shared_ptr<RespProtocolHandler> handler_;
  DispatchCallback on_ok_;
  WriteCallback on_write_;
  ErrorCallback on_error_;
};
