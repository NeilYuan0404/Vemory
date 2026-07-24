#include "vemory/persist/MutationApply.h"

#include "vemory/persist/WalManager.h"

ApplyResult ApplyMutation(const vemory::WalEntry& entry, MutateSource src,
                          VNodeIndex* vnode_index, KvStore* kv,
                          WalManager* wal) {
  ApplyResult out;
  bool should_append = true;

  switch (entry.op()) {
    case vemory::WalEntry::SET: {
      if (kv == nullptr) {
        out.err = "kv not available";
        return out;
      }
      if (entry.key().empty()) {
        out.err = "empty key";
        return out;
      }
      if (kv->Set(entry.key(), entry.value()) != KvStore::Status::kOk) {
        out.err = "set failed";
        return out;
      }
      break;
    }
    case vemory::WalEntry::DEL: {
      if (kv == nullptr) {
        out.err = "kv not available";
        return out;
      }
      if (entry.key().empty()) {
        out.err = "empty key";
        return out;
      }
      const auto st = kv->Del(entry.key());
      if (st == KvStore::Status::kNotFound) {
        out.ok = true;
        out.integer_reply = 0;
        return out;  // miss: no AOF append
      }
      if (st != KvStore::Status::kOk) {
        out.err = "del failed";
        return out;
      }
      out.integer_reply = 1;
      break;
    }
    case vemory::WalEntry::VSET: {
      if (vnode_index == nullptr) {
        out.err = "index not available";
        return out;
      }
      const auto st = vnode_index->Set(entry.vector(), entry.user_key(),
                                       entry.question(), entry.answer());
      switch (st) {
        case VNodeIndex::Status::kOk:
          break;
        case VNodeIndex::Status::kBadValue:
          out.err = "invalid VSET arguments";
          return out;
        case VNodeIndex::Status::kBadVectorSize:
          out.err = "invalid vector byte size";
          return out;
        case VNodeIndex::Status::kDimMismatch:
          out.err = "vector dimension mismatch";
          return out;
        case VNodeIndex::Status::kIndexInitFailed:
          out.err = "usearch init failed";
          return out;
        default:
          out.err = "vset failed";
          return out;
      }
      break;
    }
    case vemory::WalEntry::VDEL: {
      if (vnode_index == nullptr) {
        out.err = "index not available";
        return out;
      }
      const auto st = vnode_index->Del(entry.user_key());
      if (st == VNodeIndex::Status::kNotFound) {
        out.ok = true;
        out.integer_reply = 0;
        return out;
      }
      if (st != VNodeIndex::Status::kOk) {
        out.err = "vdel failed";
        return out;
      }
      out.integer_reply = 1;
      break;
    }
    default:
      out.err = "unknown wal op";
      return out;
  }

  if (should_append && src == MutateSource::kClient && wal != nullptr &&
      wal->enabled()) {
    if (wal->Append(entry) != WalManager::Status::kOk) {
      out.err = "aof append failed";
      return out;
    }
  }

  out.ok = true;
  return out;
}
