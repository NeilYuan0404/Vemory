#pragma once

#include "vemory/persist/WalManager.h"
#include "vemory/replication/ReplicationMaster.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

// Opaque args for KvsDispatcher / VNodeDispatcher (store + optional AOF/repl).
struct KvsDispatchArg {
  KvStore* kv = nullptr;
  WalManager* wal = nullptr;
  ReplicationMaster* repl = nullptr;
};

struct VNodeDispatchArg {
  VNodeIndex* index = nullptr;
  WalManager* wal = nullptr;
  ReplicationMaster* repl = nullptr;
};
