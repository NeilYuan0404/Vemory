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
  if (EqualsIgnoreCase(name, "VSET")) {
    return CommandType::kVset;
  }
  if (EqualsIgnoreCase(name, "VGET")) {
    return CommandType::kVget;
  }
  if (EqualsIgnoreCase(name, "VDEL")) {
    return CommandType::kVdel;
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
  if (EqualsIgnoreCase(name, "SAVE")) {
    return CommandType::kSave;
  }
  return CommandType::kUnknown;
}
