#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "vemory/index/VectorSetRegistry.h"
#include "vemory/net/EventLoop.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/net/TcpServer.h"
#include "vemory/protocol/CommandHandler.h"
#include "vemory/protocol/ProtocolExecutor.h"
#include "vemory/protocol/resp/RespProtocolHandler.h"
#include "vemory/storage/KvStore.h"
#include "vemory/util/Logging.h"

namespace {
constexpr uint16_t kDefaultPort = 8989;

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [port]\n"
            << "  port  listen port (default " << kDefaultPort << ")\n";
}

uint16_t ParsePort(int argc, char** argv) {
  if (argc <= 1) {
    return kDefaultPort;
  }
  if (argc > 2) {
    PrintUsage(argv[0]);
    std::exit(EXIT_FAILURE);
  }
  char* end = nullptr;
  const long port = std::strtol(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || port <= 0 || port > 65535) {
    std::cerr << "Invalid port: " << argv[1] << "\n";
    PrintUsage(argv[0]);
    std::exit(EXIT_FAILURE);
  }
  return static_cast<uint16_t>(port);
}
}  // namespace

int main(int argc, char** argv) {
  const uint16_t port = ParsePort(argc, argv);
  vemory::InitLogging();

  EventLoop evloop;
  TcpServer server(evloop);
  VectorSetRegistry registry;
  KvStore kv;
  // Pre-size for smoke / bench keyspace (N=100000).
  kv.Reserve(100000);
  CommandHandler commands(&registry, &kv);
  auto protocol = std::make_shared<RespProtocolHandler>();

  server.Start(port, [&commands, protocol](TcpConn::Ptr conn) {
    spdlog::debug("accepted fd={}", conn->Fd());

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

  spdlog::info("Vemory listening on 0.0.0.0:{} (RESP; try: redis-cli -p {})",
               port, port);
  evloop.Run();
  return 0;
}
