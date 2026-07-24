#pragma once

#include <string>

#include "vemory/protocol/dispatcher/DispatchArgs.h"
#include "vemory/protocol/dispatcher/HandlerRegister.h"
#include "vemory/protocol/RequestContext.h"
#include "vemory/storage/KvStore.h"
#include "vemory/persist/SnapshotManager.h"
#include "vemory/persist/WalManager.h"
#include "vemory/storage/VNodeIndex.h"

// Facade: builds HandlerRegister and dispatches parsed requests.
class CommandHandler {
 public:
  CommandHandler(VNodeIndex* vnode_index, KvStore* kv,
                 SnapshotManager* snapshot = nullptr,
                 WalManager* wal = nullptr);

  // Fills *reply with a RESP-encoded response string.
  void Dispatch(const RequestContext& ctx, std::string* reply);

 private:
  VNodeIndex* vnode_index_;
  KvStore* kv_;
  SnapshotManager* snapshot_;
  WalManager* wal_;
  KvsDispatchArg kvs_arg_{};
  VNodeDispatchArg vnode_arg_{};
  HandlerRegister register_;
};
