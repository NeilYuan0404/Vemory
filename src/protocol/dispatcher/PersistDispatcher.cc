#include "vemory/protocol/dispatcher/PersistDispatcher.h"

#include "vemory/protocol/CommandType.h"
#include "vemory/protocol/resp/RespEncode.h"
#include "vemory/persist/SnapshotManager.h"

void PersistDispatcher(const RequestContext& ctx, std::string* reply,
                       void* arg) {
  if (reply == nullptr) {
    return;
  }
  auto* snap = static_cast<SnapshotManager*>(arg);
  if (snap == nullptr) {
    RespEncode::AppendError(reply, "ERR persistence not available");
    return;
  }

  switch (ctx.cmd) {
    case CommandType::kSave: {
      const auto st = snap->BackgroundSave();
      switch (st) {
        case SnapshotManager::Status::kOk:
          RespEncode::AppendOk(reply);
          break;
        case SnapshotManager::Status::kNotConfigured:
          RespEncode::AppendError(reply, "ERR persistence dir not set");
          break;
        case SnapshotManager::Status::kInProgress:
          RespEncode::AppendError(reply,
                                  "ERR Background save already in progress");
          break;
        default:
          RespEncode::AppendError(reply, "ERR save failed");
          break;
      }
      break;
    }
    default:
      RespEncode::AppendError(reply, "ERR unknown command");
      break;
  }
}
