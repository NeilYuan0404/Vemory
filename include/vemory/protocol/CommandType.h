#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Wire command names (case-insensitive).
enum class CommandType : uint8_t {
  kVadd = 0,
  kVsim,
  kVdim,
  kVemb,
  kVcard,
  kSet,
  kDel,
  kGet,
  kPing,
  kEcho,
  kUnknown,
};

// Number of registerable commands (excludes kUnknown).
inline constexpr std::size_t kCommandTypeCount =
    static_cast<std::size_t>(CommandType::kUnknown);

CommandType ParseCommandType(std::string_view name);
