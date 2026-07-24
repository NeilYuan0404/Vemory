#pragma once

#include <cstdint>
#include <string>

#include "WalEntry.pb.h"
#include "vemory/storage/KvStore.h"
#include "vemory/storage/VNodeIndex.h"

class WalManager;

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
ApplyResult ApplyMutation(const vemory::WalEntry& entry, MutateSource src,
                          VNodeIndex* vnode_index, KvStore* kv,
                          WalManager* wal);
