#include "vemory/mutate/MutationApply.h"

#include "vemory/persist/WalManager.h"
#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/replication/ReplicationMaster.h"

ApplyResult ApplyMutation(const RequestContext& ctx, MutateSource src,
                          VNodeIndex* vnode_index, KvStore* kv, WalManager* wal,
                          ReplicationMaster* repl) {
  ApplyResult out;

  switch (ctx.cmd) {
    case CommandType::kSet: {
      if (kv == nullptr) {
        out.err = "kv not available";
        return out;
      }
      if (ctx.key.empty()) {
        out.err = "empty key";
        return out;
      }
      if (kv->Set(ctx.key, ctx.element) != KvStore::Status::kOk) {
        out.err = "set failed";
        return out;
      }
      break;
    }
    case CommandType::kDel: {
      if (kv == nullptr) {
        out.err = "kv not available";
        return out;
      }
      if (ctx.key.empty()) {
        out.err = "empty key";
        return out;
      }
      const auto st = kv->Del(ctx.key);
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
    case CommandType::kVset: {
      if (vnode_index == nullptr) {
        out.err = "index not available";
        return out;
      }
      // Capacity eviction only on client writes (AOF/repl follow recorded VDELs).
      if (src == MutateSource::kClient) {
        while (vnode_index->NeedsRoomFor(ctx.user_key)) {
          std::string victim;
          const auto pst = vnode_index->PeekLruUserKey(&victim);
          if (pst != VNodeIndex::Status::kOk || victim.empty()) {
            out.err = "eviction failed";
            return out;
          }
          RequestContext del_ctx;
          del_ctx.cmd = CommandType::kVdel;
          del_ctx.user_key = std::move(victim);
          const auto del_ar =
              ApplyMutation(del_ctx, src, vnode_index, kv, wal, repl);
          if (!del_ar.ok) {
            return del_ar;
          }
        }
      }
      const auto st = vnode_index->Set(ctx.vector_blob, ctx.user_key,
                                       ctx.question, ctx.answer);
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
    case CommandType::kVdel: {
      if (vnode_index == nullptr) {
        out.err = "index not available";
        return out;
      }
      const auto st = vnode_index->Del(ctx.user_key);
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
      out.err = "unknown write command";
      return out;
  }

  if (src == MutateSource::kClient) {
    const bool wal_on = wal != nullptr && wal->enabled();
    const bool repl_on = repl != nullptr && repl->has_slaves();
    if (wal_on || repl_on) {
      std::string frame;
      if (!RespEncode::EncodeWriteCommand(ctx, &frame)) {
        out.err = "encode write command failed";
        return out;
      }
      if (wal_on) {
        if (wal->AppendFrame(frame) != WalManager::Status::kOk) {
          out.err = "aof append failed";
          return out;
        }
      }
      if (repl_on) {
        repl->FeedEncodedFrame(frame);
      }
    }
  }

  out.ok = true;
  return out;
}
