#pragma once

#include <sys/socket.h>

#include <cerrno>
#include <string>
#include <utility>

// Userspace read buffer: contiguous std::string storage for TCP fragment/coalesce.
// Two read styles — do not mix:
//   - Line protocol: GetDataUntilCRLF + ReadCompleted(line_len+2)
//   - RESP (official commands): GetAllData + RespHandler/RespDecode + ReadCompleted(consumed)
class MessageBuffer {
 public:
  int Recv(int fd, int* err) {
    char tmp[4096];
    int n = static_cast<int>(::recv(fd, tmp, sizeof(tmp), 0));
    if (n > 0) {
      buf_.append(tmp, static_cast<size_t>(n));
      return n;
    }
    if (err) {
      *err = (n == 0) ? 0 : errno;
    }
    return n;
  }

  // Returns {line start, length} without \r\n; {nullptr, 0} if no complete line
  std::pair<char*, size_t> GetDataUntilCRLF() {
    const size_t pos = buf_.find("\r\n");
    if (pos == std::string::npos) {
      return {nullptr, 0};
    }
    return {buf_.data(), pos};
  }

  std::pair<char*, size_t> GetAllData() {
    if (buf_.empty()) {
      return {nullptr, 0};
    }
    return {buf_.data(), buf_.size()};
  }

  void ReadCompleted(size_t n) {
    if (n >= buf_.size()) {
      buf_.clear();
    } else if (n > 0) {
      buf_.erase(0, n);
    }
  }

  size_t Size() const { return buf_.size(); }
  bool Empty() const { return buf_.empty(); }

 private:
  std::string buf_;
};
