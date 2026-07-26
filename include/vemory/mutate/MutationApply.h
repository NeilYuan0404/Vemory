#pragma once

#include <cstdint>
#include <string>

#include "WalEntry.pb.h"
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

// Apply a WalEntry to memory. Appends to wal only for kClient when wal is set.
// Feeds replication backlog when repl is non-null (independent of AOF).
ApplyResult ApplyMutation(const vemory::WalEntry& entry, MutateSource src,
                          VNodeIndex* vnode_index, KvStore* kv,
                          WalManager* wal, ReplicationMaster* repl = nullptr);
