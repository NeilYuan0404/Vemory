#include "vemory/storage/ProtobufVNodeCodec.h"

#include "VNode.pb.h"

ProtobufVNodeCodec::Status ProtobufVNodeCodec::Encode(const VNode& node,
                                                      std::string* out) const {
  if (out == nullptr) {
    return Status::kError;
  }

  vemory::VNodePb pb;
  pb.set_id(node.id);
  pb.set_prompt(node.prompt);
  pb.set_answer(node.answer);
  if (!pb.SerializeToString(out)) {
    out->clear();
    return Status::kError;
  }
  return Status::kOk;
}

ProtobufVNodeCodec::Status ProtobufVNodeCodec::Decode(std::string_view bytes,
                                                      VNode* out) const {
  if (out == nullptr) {
    return Status::kError;
  }

  vemory::VNodePb pb;
  if (!pb.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
    return Status::kError;
  }

  out->id = static_cast<uint16_t>(pb.id());
  out->prompt = pb.prompt();
  out->answer = pb.answer();
  return Status::kOk;
}
