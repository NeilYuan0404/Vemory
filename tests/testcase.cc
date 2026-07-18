#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "vemory/index/VectorSetRegistry.h"
#include "vemory/net/EventLoop.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/net/TcpServer.h"
#include "vemory/protocol/CommandHandler.h"
#include "vemory/protocol/ProtocolExecutor.h"
#include "vemory/protocol/resp/RespProtocolHandler.h"

int main() {
  EventLoop evloop;
  TcpServer server(evloop);
  VectorSetRegistry registry;
  CommandHandler commands(&registry);
  auto protocol = std::make_shared<RespProtocolHandler>();

  server.Start(8989, [&commands, protocol](TcpConn::Ptr conn) {
    std::cout << "New connection established\n";

    auto executor = std::make_shared<ProtocolExecutor>(
        protocol,
        [&commands](RequestContext ctx, std::string* reply) {
          commands.Dispatch(ctx, reply);
        },
        [conn](std::string_view data) {
          if (!data.empty()) {
            conn->Send(data.data(), data.size());
          }
        },
        [conn]() {
          const std::string err = "-ERR protocol error\r\n";
          conn->Send(err.data(), err.size());
        });

    conn->SetReadCallback([conn, executor]() {
      executor->OnReadable(conn->Fd(), conn->InputBuffer());
    });
  });

  evloop.Run();
  return 0;
}
