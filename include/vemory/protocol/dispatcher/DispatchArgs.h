#pragma once

#include "vemory/persist/WalManager.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

// Opaque args for KvsDispatcher / VNodeDispatcher (store + optional AOF).
struct KvsDispatchArg {
  KvStore* kv = nullptr;
  WalManager* wal = nullptr;
};

struct VNodeDispatchArg {
  VNodeIndex* index = nullptr;
  WalManager* wal = nullptr;
};
