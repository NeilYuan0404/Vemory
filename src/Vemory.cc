#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "vemory/net/EventLoop.h"
#include "vemory/net/TcpConnection.h"
#include "vemory/net/TcpServer.h"
#include "vemory/protocol/dispatcher/CommandHandler.h"
#include "vemory/protocol/ProtocolExecutor.h"
#include "vemory/protocol/resp/RespProtocolHandler.h"
#include "vemory/storage/KvStore.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/persist/WalManager.h"
#include "vemory/replication/ReplicationMaster.h"
#include "vemory/replication/ReplicationSlave.h"
#include "vemory/storage/VNodeIndex.h"
#include "vemory/util/Config.h"
#include "vemory/util/Logging.h"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " [-c <config.ini>] [--slaveof <host> <port>] [port]\n"
            << "  -c path              load INI config (see conf/vemory.ini)\n"
            << "  --slaveof host port  run as replica; PSYNC fullsync from master\n"
            << "  port                 listen port (overrides server.port)\n";
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

// Parse: vemory [-c path] [--slaveof host port] [port]
bool ParseArgs(int argc, char** argv, std::string* config_path,
               bool* port_override, uint16_t* port, bool* slaveof,
               std::string* slaveof_host, uint16_t* slaveof_port) {
  *config_path = "";
  *port_override = false;
  *port = 6379;
  *slaveof = false;
  *slaveof_host = "";
  *slaveof_port = 0;

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
    if (arg == "--slaveof") {
      if (i + 2 >= argc) {
        std::cerr << "Usage: --slaveof <host> <port>\n";
        PrintUsage(argv[0]);
        return false;
      }
      *slaveof = true;
      *slaveof_host = argv[i + 1];
      if (!ParsePortArg(argv[i + 2], slaveof_port)) {
        std::cerr << "Invalid --slaveof port: " << argv[i + 2] << "\n";
        PrintUsage(argv[0]);
        return false;
      }
      i += 3;
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
  bool slaveof = false;
  std::string slaveof_host;
  uint16_t slaveof_port = 0;
  if (!ParseArgs(argc, argv, &config_path, &port_override, &cli_port, &slaveof,
                 &slaveof_host, &slaveof_port)) {
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
  cfg.slaveof = slaveof;
  cfg.slaveof_host = slaveof_host;
  cfg.slaveof_port = slaveof_port;

  vemory::InitLogging(cfg.log_level);
  for (const auto& w : cfg.warnings) {
    spdlog::warn("{}", w);
  }

  EventLoop evloop;
  TcpServer server(evloop);
  VNodeIndex vnode_index(cfg.default_capacity);
  KvStore kv;
  kv.Reserve(cfg.kv_reserve);
  SnapshotManager snapshot(&vnode_index, &kv, cfg.persistence_dir);
  WalManager wal(&vnode_index, &kv, cfg.persistence_dir, cfg.aof, cfg.aof_fsync,
                 cfg.aof_io);

  if (cfg.load_on_startup && !cfg.persistence_dir.empty()) {
    const auto st = snapshot.Load();
    if (st == SnapshotManager::Status::kOk) {
      spdlog::info("Loaded snapshot from {}", cfg.persistence_dir);
    } else if (st == SnapshotManager::Status::kIoError) {
      spdlog::warn("No usable snapshot in {} (starting empty)",
                   cfg.persistence_dir);
    } else {
      spdlog::error("Failed to load snapshot from {} (status={})",
                    cfg.persistence_dir, static_cast<int>(st));
      return EXIT_FAILURE;
    }
  }

  if (wal.enabled()) {
    const auto st = wal.Replay();
    if (st == WalManager::Status::kOk) {
      // logged inside Replay
    } else if (st == WalManager::Status::kNotConfigured) {
      // ignore
    } else {
      spdlog::error("Failed to replay AOF from {} (status={})", wal.path(),
                    static_cast<int>(st));
      return EXIT_FAILURE;
    }
  }

  std::unique_ptr<ReplicationMaster> repl_master;
  if (!cfg.slaveof) {
    repl_master = std::make_unique<ReplicationMaster>(&snapshot);
  }

  CommandHandler commands(&vnode_index, &kv, &snapshot, &wal,
                          repl_master.get(), cfg.slaveof);
  auto protocol = std::make_shared<RespProtocolHandler>();

  server.Start(cfg.bind, cfg.port,
               [&commands, protocol, master = repl_master.get()](
                   TcpConn::Ptr conn) {
                 spdlog::debug("accepted fd={}", conn->Fd());
                 if (master != nullptr) {
                   master->OnConnection(conn);
                 }

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

  std::unique_ptr<ReplicationSlave> repl_slave;
  if (cfg.slaveof) {
    repl_slave = std::make_unique<ReplicationSlave>(&evloop, &snapshot,
                                                    &vnode_index, &kv);
    if (!repl_slave->Start(cfg.slaveof_host, cfg.slaveof_port)) {
      spdlog::error("Failed to start replication slave of {}:{} (fatal setup)",
                    cfg.slaveof_host, cfg.slaveof_port);
      return EXIT_FAILURE;
    }
    spdlog::info(
        "Vemory slaveof {}:{} listening on {}:{} (replica-readonly; "
        "auto-reconnect on link failure)",
        cfg.slaveof_host, cfg.slaveof_port, cfg.bind, cfg.port);
  } else {
    spdlog::info("Vemory master listening on {}:{} (RESP; try: redis-cli -p {})",
                 cfg.bind, cfg.port, cfg.port);
  }

  evloop.Run();
  return 0;
}
