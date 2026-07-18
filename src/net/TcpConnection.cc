#include "vemory/net/TcpConnection.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "vemory/net/EventLoop.h"

TcpConn::TcpConn(int fd, EventLoop& evloop)
    : fd_(fd), evloop_(evloop), closed_(false) {
  SetNonBlocking(fd_);
  io_handler_ = [this](uint32_t events) { HandleIO(events); };
  evloop_.AddEvent(fd_, EPOLLIN | EPOLLRDHUP | EPOLLET, &io_handler_);
}

TcpConn::~TcpConn() { Close(); }

int TcpConn::Send(const char* data, size_t size) {
  if (closed_ || data == nullptr || size == 0) return -1;

  if (!output_buffer_.empty()) {
    output_buffer_.append(data, size);
    EnableWrite();
    return size;
  }

  int n = ::send(fd_, data, size, MSG_NOSIGNAL);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      output_buffer_.append(data, size);
      EnableWrite();
    } else {
      Close();
    }
  } else {
    if (static_cast<size_t>(n) < size) {
      output_buffer_.append(data + n, size - static_cast<size_t>(n));
      EnableWrite();
    }
  }
  return n;
}

void TcpConn::HandleIO(uint32_t events) {
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
  while (!output_buffer_.empty()) {
    int n =
        ::send(fd_, output_buffer_.data(), output_buffer_.size(), MSG_NOSIGNAL);
    if (n > 0) {
      output_buffer_.erase(0, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    Close();
    return;
  }
  DisableWrite();
}

void TcpConn::Close() {
  if (closed_) return;
  closed_ = true;

  evloop_.DelEvent(fd_);
  close(fd_);
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

std::string TcpConn::GetDataUntilCrLf() {
  auto data = input_buffer_.GetDataUntilCRLF();
  if (data.first != nullptr) {
    std::string result(data.first, data.second);
    input_buffer_.ReadCompleted(data.second + 2);  // consume \r\n
    return result;
  }
  return "";
}

std::string TcpConn::GetAllData() {
  auto data = input_buffer_.GetAllData();
  if (data.first != nullptr) {
    std::string result(data.first, data.second);
    input_buffer_.ReadCompleted(data.second);
    return result;
  }
  return "";
}