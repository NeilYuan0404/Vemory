#include "vemory/protocol/resp/RespDecode.h"

#include <cctype>

bool RespDecode::FindCRLF(const char* data, size_t size, size_t pos,
                          size_t* crlf) {
  for (size_t i = pos; i + 1 < size; ++i) {
    if (data[i] == '\r' && data[i + 1] == '\n') {
      *crlf = i;
      return true;
    }
  }
  return false;
}

bool RespDecode::ParseIntegerLine(const char* data, size_t size, size_t* pos,
                                  int64_t* out) {
  size_t crlf = 0;
  if (!FindCRLF(data, size, *pos, &crlf)) {
    return false;
  }
  if (crlf == *pos) {
    return false;
  }

  // Strict: optional leading '-', then digits only.
  size_t i = *pos;
  bool neg = false;
  if (data[i] == '-') {
    neg = true;
    ++i;
    if (i == crlf) {
      return false;
    }
  }
  int64_t value = 0;
  for (; i < crlf; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(data[i]))) {
      return false;
    }
    value = value * 10 + (data[i] - '0');
  }
  *out = neg ? -value : value;
  *pos = crlf + 2;
  return true;
}

RespDecode::Status RespDecode::DecodeArrayOfBulk(
    const char* data, size_t size, std::vector<std::string_view>* tokens,
    size_t* consumed) {
  if (tokens == nullptr || consumed == nullptr) {
    return Status::kError;
  }
  tokens->clear();
  *consumed = 0;

  if (size == 0 || data == nullptr) {
    return Status::kNeedMore;
  }
  if (data[0] != '*') {
    return Status::kError;
  }

  size_t pos = 1;
  int64_t count = 0;
  if (!ParseIntegerLine(data, size, &pos, &count)) {
    // Distinguish incomplete vs malformed: no CRLF yet => NeedMore.
    size_t unused = 0;
    if (!FindCRLF(data, size, 1, &unused)) {
      return Status::kNeedMore;
    }
    return Status::kError;
  }
  if (count < 0) {
    return Status::kError;
  }

  tokens->reserve(static_cast<size_t>(count));
  for (int64_t i = 0; i < count; ++i) {
    if (pos >= size) {
      return Status::kNeedMore;
    }
    if (data[pos] != '$') {
      return Status::kError;
    }
    ++pos;

    int64_t bulk_len = 0;
    const size_t header_pos = pos;
    if (!ParseIntegerLine(data, size, &pos, &bulk_len)) {
      size_t unused = 0;
      if (!FindCRLF(data, size, header_pos, &unused)) {
        return Status::kNeedMore;
      }
      return Status::kError;
    }
    if (bulk_len < 0) {
      // Null bulk is not accepted inside command arrays.
      return Status::kError;
    }

    const size_t body_len = static_cast<size_t>(bulk_len);
    if (pos + body_len + 2 > size) {
      return Status::kNeedMore;
    }
    if (data[pos + body_len] != '\r' || data[pos + body_len + 1] != '\n') {
      return Status::kError;
    }

    tokens->emplace_back(data + pos, body_len);
    pos += body_len + 2;
  }

  *consumed = pos;
  return Status::kOk;
}
