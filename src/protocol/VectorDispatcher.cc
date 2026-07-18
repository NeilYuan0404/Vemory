#include "vemory/protocol/VectorDispatcher.h"

#include <cstdio>
#include <string_view>
#include <vector>

#include "vemory/index/VectorSetRegistry.h"
#include "vemory/protocol/resp/RespEncode.h"

namespace {

void AppendFloatBulk(std::string* reply, float v) {
  char buf[64];
  const int n = std::snprintf(buf, sizeof(buf), "%g", v);
  RespEncode::AppendBulkString(
      reply, n > 0 ? std::string_view(buf, static_cast<size_t>(n))
                   : std::string_view("0"));
}

}  // namespace

void VectorDispatcher(const RequestContext& ctx, std::string* reply,
                      void* arg) {
  if (reply == nullptr || arg == nullptr) {
    return;
  }
  auto* registry = static_cast<VectorSetRegistry*>(arg);

  switch (ctx.cmd) {
    case CommandType::kVadd: {
      VectorSet* set = registry->GetOrCreate(ctx.key, ctx.embed.size());
      if (set == nullptr) {
        RespEncode::AppendError(reply, "ERR dimension mismatch or bad key");
        return;
      }
      const auto st =
          set->Add(ctx.element, ctx.embed.data(), ctx.embed.size());
      if (st != VectorSet::Status::kOk) {
        RespEncode::AppendError(reply, "ERR vadd failed");
        return;
      }
      RespEncode::AppendInteger(reply, 1);
      break;
    }
    case CommandType::kVsim: {
      VectorSet* set = registry->Find(ctx.key);
      if (set == nullptr) {
        RespEncode::AppendArrayHeader(reply, 0);
        return;
      }
      std::vector<VectorSet::Hit> hits;
      VectorSet::Status st = VectorSet::Status::kError;
      if (ctx.vsim_mode == VsimMode::kEle) {
        st = set->SearchByElement(ctx.element, ctx.count, &hits);
        if (st == VectorSet::Status::kNotFound) {
          RespEncode::AppendError(reply, "ERR element not found");
          return;
        }
      } else if (ctx.vsim_mode == VsimMode::kValues) {
        if (ctx.embed.size() != set->dimensions()) {
          RespEncode::AppendError(reply, "ERR dimension mismatch");
          return;
        }
        st = set->Search(ctx.embed.data(), ctx.embed.size(), ctx.count,
                         &hits);
      } else {
        RespEncode::AppendError(reply, "ERR bad vsim mode");
        return;
      }
      if (st != VectorSet::Status::kOk) {
        RespEncode::AppendError(reply, "ERR vsim failed");
        return;
      }
      if (ctx.with_scores) {
        RespEncode::AppendArrayHeader(
            reply, static_cast<int64_t>(hits.size() * 2));
        for (const auto& hit : hits) {
          RespEncode::AppendBulkString(reply, hit.element);
          AppendFloatBulk(reply, hit.score);
        }
      } else {
        RespEncode::AppendArrayHeader(reply,
                                      static_cast<int64_t>(hits.size()));
        for (const auto& hit : hits) {
          RespEncode::AppendBulkString(reply, hit.element);
        }
      }
      break;
    }
    case CommandType::kVdim: {
      VectorSet* set = registry->Find(ctx.key);
      if (set == nullptr) {
        RespEncode::AppendError(reply, "ERR key does not exist");
        return;
      }
      RespEncode::AppendInteger(reply,
                                static_cast<int64_t>(set->dimensions()));
      break;
    }
    case CommandType::kVemb: {
      VectorSet* set = registry->Find(ctx.key);
      if (set == nullptr) {
        RespEncode::AppendError(reply, "ERR key does not exist");
        return;
      }
      std::vector<float> emb;
      const auto st = set->GetEmbedding(ctx.element, &emb);
      if (st == VectorSet::Status::kNotFound) {
        RespEncode::AppendError(reply, "ERR element not found");
        return;
      }
      if (st != VectorSet::Status::kOk) {
        RespEncode::AppendError(reply, "ERR vemb failed");
        return;
      }
      RespEncode::AppendArrayHeader(reply, static_cast<int64_t>(emb.size()));
      for (float f : emb) {
        AppendFloatBulk(reply, f);
      }
      break;
    }
    case CommandType::kVcard: {
      VectorSet* set = registry->Find(ctx.key);
      if (set == nullptr) {
        RespEncode::AppendInteger(reply, 0);
        return;
      }
      RespEncode::AppendInteger(reply, static_cast<int64_t>(set->size()));
      break;
    }
    default:
      RespEncode::AppendError(reply, "ERR unknown command");
      break;
  }
}
