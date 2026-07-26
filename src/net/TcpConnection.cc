#include "vemory/net/TcpConnection.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "vemory/net/EventLoop.h"

TcpConn::TcpConn(int fd, EventLoop& evloop)
    : fd_(fd), evloop_(evloop), closed_(false) {
  SetNonBlocking(fd_);
  io_handler_ = [this](IoEvents events) { HandleIO(events); };
  evloop_.AddEvent(fd_, EPOLLIN | EPOLLRDHUP | EPOLLET, &io_handler_);
}

TcpConn::~TcpConn() { Close(); }

size_t TcpConn::OutputBufferedBytes() const {
  size_t n = output_buffer_.size() + post_sendfile_buffer_.size();
  n += static_cast<size_t>(sendfile_remaining_);
  return n;
}

int TcpConn::Send(const char* data, size_t size) {
  if (closed_ || data == nullptr || size == 0) return -1;

  // Do not interleave with an in-flight sendfile body.
  if (sendfile_fd_ >= 0) {
    post_sendfile_buffer_.append(data, size);
    return static_cast<int>(size);
  }

  if (!output_buffer_.empty()) {
    output_buffer_.append(data, size);
    EnableWrite();
    return static_cast<int>(size);
  }

  int n = ::send(fd_, data, size, MSG_NOSIGNAL);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      output_buffer_.append(data, size);
      EnableWrite();
      return static_cast<int>(size);
    }
    Close();
    return -1;
  }
  if (static_cast<size_t>(n) < size) {
    output_buffer_.append(data + n, size - static_cast<size_t>(n));
    EnableWrite();
  }
  return n;
}

bool TcpConn::SendFile(const std::string& path, SendFileDoneCallback done) {
  if (closed_ || path.empty()) {
    return false;
  }
  if (sendfile_fd_ >= 0) {
    return false;  // one sendfile at a time
  }
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
    ::close(fd);
    return false;
  }
  sendfile_fd_ = fd;
  sendfile_offset_ = 0;
  sendfile_remaining_ = static_cast<uint64_t>(st.st_size);
  sendfile_done_ = std::move(done);
  EnableWrite();
  // Kick write path in case EPOLLOUT already level-triggered ready.
  HandleWrite();
  return true;
}

void TcpConn::ClearSendFile(bool ok) {
  if (sendfile_fd_ >= 0) {
    ::close(sendfile_fd_);
    sendfile_fd_ = -1;
  }
  sendfile_offset_ = 0;
  sendfile_remaining_ = 0;
  auto cb = std::move(sendfile_done_);
  sendfile_done_ = nullptr;
  if (ok && !post_sendfile_buffer_.empty()) {
    output_buffer_.append(post_sendfile_buffer_);
    post_sendfile_buffer_.clear();
    EnableWrite();
  } else {
    post_sendfile_buffer_.clear();
  }
  if (cb) {
    cb(ok);
  }
}

void TcpConn::HandleIO(IoEvents events) {
  if (closed_) return;

  if (events & EPOLLIN) HandleRead();
  if (closed_) return;

  if (events & EPOLLOUT) HandleWrite();
  if (closed_) return;

  if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) Close();
}

void TcpConn::HandleRead() {
  bool has_data = false;
  while (true) {
    int err = 0;
    int n = input_buffer_.Recv(fd_, &err);
    if (n > 0) {
      has_data = true;
      continue;
    }
    if (n == 0) {
      Close();
      return;
    }
    if (err == EAGAIN || err == EWOULDBLOCK) break;
    Close();
    return;
  }
  if (has_data && read_cb_) read_cb_();
}

void TcpConn::HandleWrite() {
  for (;;) {
    while (!output_buffer_.empty()) {
      int n = ::send(fd_, output_buffer_.data(), output_buffer_.size(),
                     MSG_NOSIGNAL);
      if (n > 0) {
        output_buffer_.erase(0, static_cast<size_t>(n));
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
      ClearSendFile(false);
      Close();
      return;
    }

    if (sendfile_fd_ >= 0 && sendfile_remaining_ > 0) {
      const size_t chunk = sendfile_remaining_ > static_cast<uint64_t>(1 << 20)
                               ? static_cast<size_t>(1 << 20)
                               : static_cast<size_t>(sendfile_remaining_);
      ssize_t n = ::sendfile(fd_, sendfile_fd_, &sendfile_offset_, chunk);
      if (n > 0) {
        sendfile_remaining_ -= static_cast<uint64_t>(n);
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      ClearSendFile(false);
      Close();
      return;
    }

    if (sendfile_fd_ >= 0 && sendfile_remaining_ == 0) {
      ClearSendFile(true);
      // Callback / post buffer may have queued more output — loop.
      continue;
    }

    DisableWrite();
    return;
  }
}

void TcpConn::Close() {
  if (closed_) return;
  closed_ = true;

  ClearSendFile(false);
  evloop_.DelEvent(fd_);
  ::close(fd_);
  if (close_cb_) {
    close_cb_();
  }
}

void TcpConn::DisableWrite() {
  evloop_.ModEvent(fd_, EPOLLIN | EPOLLRDHUP | EPOLLET, &io_handler_);
}

void TcpConn::EnableWrite() {
  evloop_.ModEvent(fd_, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET,
                   &io_handler_);
}

void TcpConn::SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    spdlog::error("fcntl get error: {}", errno);
    return;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    spdlog::error("fcntl set error: {}", errno);
  }
}
