#pragma once

#include <cstdint>
#include <string>

#include "vemory/protocol/RequestContext.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

class WalManager;
class ReplicationMaster;

enum class MutateSource : uint8_t {
  kClient = 0,
  kAofReplay = 1,
};

struct ApplyResult {
  bool ok = false;
  std::string err;
  int integer_reply = 0;  // DEL / VDEL
};

// Apply a write command (SET/DEL/VSET/VDEL) from RequestContext.
// For kClient: encodes RESP once, appends to wal and feeds replication backlog.
ApplyResult ApplyMutation(const RequestContext& ctx, MutateSource src,
                          VNodeIndex* vnode_index, KvStore* kv, WalManager* wal,
                          ReplicationMaster* repl = nullptr);
