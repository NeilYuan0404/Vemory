#include "vemory/storage/RespVNodeCodec.h"

#include <cstdio>
#include <vector>

#include "vemory/protocol/resp/RespDecode.h"
#include "vemory/protocol/resp/RespEncode.h"

RespVNodeCodec::Status RespVNodeCodec::Encode(const VNode& node,
                                              std::string* out) const {
  if (out == nullptr) {
    return Status::kError;
  }
  out->clear();
  char id_buf[16];
  const int n =
      std::snprintf(id_buf, sizeof(id_buf), "%u", static_cast<unsigned>(node.id));
  if (n <= 0) {
    return Status::kError;
  }
  RespEncode::AppendArrayHeader(out, 4);
  RespEncode::AppendBulkString(out, std::string_view(id_buf, static_cast<std::size_t>(n)));
  RespEncode::AppendBulkString(out, node.user_key);
  RespEncode::AppendBulkString(out, node.question);
  RespEncode::AppendBulkString(out, node.answer);
  return Status::kOk;
}

RespVNodeCodec::Status RespVNodeCodec::Decode(std::string_view bytes,
                                              VNode* out) const {
  if (out == nullptr) {
    return Status::kError;
  }
  std::vector<std::string_view> tokens;
  std::size_t consumed = 0;
  const auto st = RespDecode::DecodeArrayOfBulk(bytes.data(), bytes.size(),
                                                &tokens, &consumed);
  if (st != RespDecode::Status::kOk || consumed != bytes.size() ||
      tokens.size() != 4) {
    return Status::kError;
  }
  unsigned long id = 0;
  for (char c : tokens[0]) {
    if (c < '0' || c > '9') {
      return Status::kError;
    }
    id = id * 10ul + static_cast<unsigned long>(c - '0');
    if (id > 0xfffful) {
      return Status::kError;
    }
  }
  out->id = static_cast<uint16_t>(id);
  out->user_key.assign(tokens[1].data(), tokens[1].size());
  out->question.assign(tokens[2].data(), tokens[2].size());
  out->answer.assign(tokens[3].data(), tokens[3].size());
  return Status::kOk;
}
