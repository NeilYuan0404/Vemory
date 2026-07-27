#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct RequestContext;

// Append RESP-encoded frames into an output buffer (caller owns the string).
class RespEncode {
 public:
  static void AppendSimpleString(std::string* out, std::string_view s);
  static void AppendError(std::string* out, std::string_view s);
  static void AppendInteger(std::string* out, int64_t v);
  static void AppendBulkString(std::string* out, std::string_view s);
  static void AppendNullBulk(std::string* out);
  static void AppendArrayHeader(std::string* out, int64_t n);

  // Common replies.
  static void AppendOk(std::string* out);

  // Encode a write command as a RESP array (SET/DEL/VSET/VDEL).
  // Returns false if ctx.cmd is not a supported write.
  static bool EncodeWriteCommand(const RequestContext& ctx, std::string* out);
};
