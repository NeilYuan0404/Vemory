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
#include "vemory/util/Config.h"
#include "vemory/util/Logging.h"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [-c <config.ini>] [port]\n"
            << "  -c path   load INI config (see conf/vemory.ini)\n"
            << "  port      listen port (overrides server.port)\n";
}

bool ParsePortArg(const char* text, uint16_t* out) {
  if (text == nullptr || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const long port = std::strtol(text, &end, 10);
  if (end == text || *end != '\0' || port <= 0 || port > 65535) {
    return false;
  }
  *out = static_cast<uint16_t>(port);
  return true;
}

// Parse: vemory [-c path] [port]
bool ParseArgs(int argc, char** argv, std::string* config_path,
               bool* port_override, uint16_t* port) {
  *config_path = "";
  *port_override = false;
  *port = 6379;

  int i = 1;
  while (i < argc) {
    const std::string_view arg = argv[i];
    if (arg == "-c") {
      if (i + 1 >= argc) {
        std::cerr << "Missing path after -c\n";
        PrintUsage(argv[0]);
        return false;
      }
      *config_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      std::exit(EXIT_SUCCESS);
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n";
      PrintUsage(argv[0]);
      return false;
    }
    if (*port_override) {
      std::cerr << "Unexpected argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return false;
    }
    if (!ParsePortArg(argv[i], port)) {
      std::cerr << "Invalid port: " << argv[i] << "\n";
      PrintUsage(argv[0]);
      return false;
    }
    *port_override = true;
    ++i;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path;
  bool port_override = false;
  uint16_t cli_port = 6379;
  if (!ParseArgs(argc, argv, &config_path, &port_override, &cli_port)) {
    return EXIT_FAILURE;
  }

  vemory::Config cfg;
  if (!config_path.empty()) {
    std::string err;
    if (!vemory::LoadConfig(config_path, &cfg, &err)) {
      std::cerr << "Config error: " << err << "\n";
      return EXIT_FAILURE;
    }
  }
  if (port_override) {
    cfg.port = cli_port;
  }

  vemory::InitLogging(cfg.log_level);
  for (const auto& w : cfg.warnings) {
    spdlog::warn("{}", w);
  }

  EventLoop evloop;
  TcpServer server(evloop);
  VectorSetRegistry registry(cfg.default_capacity);
  KvStore kv;
  kv.Reserve(cfg.kv_reserve);
  CommandHandler commands(&registry, &kv);
  auto protocol = std::make_shared<RespProtocolHandler>();

  server.Start(cfg.bind, cfg.port, [&commands, protocol](TcpConn::Ptr conn) {
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
      executor->OnBufferReadable(conn->Fd(), conn->InputBuffer());
    });
  });

  spdlog::info("Vemory listening on {}:{} (RESP; try: redis-cli -p {})",
               cfg.bind, cfg.port, cfg.port);
  evloop.Run();
  return 0;
}
