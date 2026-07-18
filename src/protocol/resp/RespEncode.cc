#include "vemory/protocol/resp/RespEncode.h"

#include <cstdio>

namespace {

void AppendCRLF(std::string* out) { out->append("\r\n", 2); }

}  // namespace

void RespEncode::AppendSimpleString(std::string* out, std::string_view s) {
  out->push_back('+');
  out->append(s.data(), s.size());
  AppendCRLF(out);
}

void RespEncode::AppendError(std::string* out, std::string_view s) {
  out->push_back('-');
  out->append(s.data(), s.size());
  AppendCRLF(out);
}

void RespEncode::AppendInteger(std::string* out, int64_t v) {
  char buf[32];
  const int n = std::snprintf(buf, sizeof(buf), ":%lld\r\n",
                              static_cast<long long>(v));
  if (n > 0) {
    out->append(buf, static_cast<size_t>(n));
  }
}

void RespEncode::AppendBulkString(std::string* out, std::string_view s) {
  char hdr[32];
  const int n = std::snprintf(hdr, sizeof(hdr), "$%zu\r\n", s.size());
  if (n > 0) {
    out->append(hdr, static_cast<size_t>(n));
  }
  out->append(s.data(), s.size());
  AppendCRLF(out);
}

void RespEncode::AppendNullBulk(std::string* out) {
  out->append("$-1\r\n", 5);
}

void RespEncode::AppendArrayHeader(std::string* out, int64_t n) {
  char buf[32];
  const int m = std::snprintf(buf, sizeof(buf), "*%lld\r\n",
                              static_cast<long long>(n));
  if (m > 0) {
    out->append(buf, static_cast<size_t>(m));
  }
}

void RespEncode::AppendOk(std::string* out) {
  out->append("+OK\r\n", 5);
}
