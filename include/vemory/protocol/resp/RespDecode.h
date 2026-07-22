#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Parse RESP frames without copying payload bytes.
// Each string_view points into the `data` buffer passed in.
class RespDecode {
 public:
  enum class Status : uint8_t {
    kOk = 0,    // one complete value; *consumed is set
    kNeedMore,  // wait for more bytes
    kError,     // malformed RESP
  };

  // Decode one RESP array of bulk strings (Redis command form):
  //   *<n>\r\n $<len>\r\n <body>\r\n ...
  // On kOk, *tokens holds string_views into `data`; *consumed is total frame
  // size.
  static Status DecodeArrayOfBulk(const char* data, size_t size,
                                  std::vector<std::string_view>* tokens,
                                  size_t* consumed);

 private:
  static bool ParseIntegerLine(const char* data, size_t size, size_t* pos,
                               int64_t* out);
  static bool FindCRLF(const char* data, size_t size, size_t pos, size_t* crlf);
};
