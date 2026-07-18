#include "vemory/protocol/CommandType.h"

#include <cctype>

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const auto ca = static_cast<unsigned char>(a[i]);
    const auto cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) {
      return false;
    }
  }
  return true;
}

}  // namespace

CommandType ParseCommandType(std::string_view name) {
  if (EqualsIgnoreCase(name, "VADD")) {
    return CommandType::kVadd;
  }
  if (EqualsIgnoreCase(name, "VSIM")) {
    return CommandType::kVsim;
  }
  if (EqualsIgnoreCase(name, "VDIM")) {
    return CommandType::kVdim;
  }
  if (EqualsIgnoreCase(name, "VEMB")) {
    return CommandType::kVemb;
  }
  if (EqualsIgnoreCase(name, "VCARD")) {
    return CommandType::kVcard;
  }
  if (EqualsIgnoreCase(name, "SET")) {
    return CommandType::kSet;
  }
  if (EqualsIgnoreCase(name, "DEL")) {
    return CommandType::kDel;
  }
  if (EqualsIgnoreCase(name, "GET")) {
    return CommandType::kGet;
  }
  if (EqualsIgnoreCase(name, "PING")) {
    return CommandType::kPing;
  }
  if (EqualsIgnoreCase(name, "ECHO")) {
    return CommandType::kEcho;
  }
  return CommandType::kUnknown;
}
