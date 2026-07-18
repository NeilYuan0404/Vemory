#include "vemory/protocol/resp/RespHandler.h"

RespHandler::Status RespHandler::TryParse(
    MessageBuffer& buf, std::vector<std::string_view>* tokens, size_t* consumed) {
  if (tokens == nullptr || consumed == nullptr) {
    return Status::kError;
  }

  auto all = buf.GetAllData();
  if (all.first == nullptr || all.second == 0) {
    return Status::kNeedMore;
  }

  return RespDecode::DecodeArrayOfBulk(all.first, all.second, tokens, consumed);
}
