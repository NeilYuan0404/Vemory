#include "vemory/protocol/ProtocolExecutor.h"

#include <string>

ProtocolExecutor::ProtocolExecutor(std::shared_ptr<RespProtocolHandler> handler,
                                   DispatchCallback on_ok,
                                   WriteCallback on_write,
                                   ErrorCallback on_error)
    : handler_(std::move(handler)),
      on_ok_(std::move(on_ok)),
      on_write_(std::move(on_write)),
      on_error_(std::move(on_error)) {}

void ProtocolExecutor::OnBufferReadable(int client_fd, MessageBuffer& buf) {
  if (!handler_ || !on_ok_) {
    return;
  }

  std::string batch;
  auto flush = [this, &batch]() {
    if (!batch.empty() && on_write_) {
      on_write_(batch);
      batch.clear();
    }
  };

  while (true) {
    RequestContext ctx;
    size_t consumed = 0;
    const auto st = handler_->TryParse(client_fd, buf, &ctx, &consumed);
    if (st == RespProtocolHandler::Status::kNeedMore) {
      flush();
      return;
    }
    if (st == RespProtocolHandler::Status::kError) {
      // Flush replies already produced in this round, then report wire error.
      flush();
      if (on_error_) {
        on_error_();
      }
      return;
    }

    // Consume before dispatch so buffer can accept more I/O; ctx owns copies.
    buf.ReadCompleted(consumed);
    std::string reply;
    on_ok_(std::move(ctx), &reply);
    if (!reply.empty()) {
      batch.append(reply);
    }
  }
}
