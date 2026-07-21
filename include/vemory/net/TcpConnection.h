#pragma once

#include <functional>
#include <memory>

#include "vemory/net/EventLoop.h"
#include "vemory/net/MessageBuffer.h"

// TCP connection
class TcpConn : public std::enable_shared_from_this<TcpConn> {
 public:
  using Ptr = std::shared_ptr<TcpConn>;
  using ReadCallback = std::function<void()>;
  using CloseCallback = std::function<void()>;

  TcpConn(int fd, EventLoop& evloop);

  ~TcpConn();

  void SetReadCallback(ReadCallback cb) { read_cb_ = cb; }

  int Fd() const { return fd_; }

  // Unconsumed read buffer for protocol parsers (RESP path).
  MessageBuffer& InputBuffer() { return input_buffer_; }

  int Send(const char* data, size_t size);

 private:
  static void SetNonBlocking(int fd);
  void Close();

 private:
  void HandleIO(IoEvents events);

  void HandleRead();

  void HandleWrite();

  void DisableWrite();
  void EnableWrite();

  int fd_;
  EventLoop& evloop_;
  bool closed_;
  std::string output_buffer_;
  MessageBuffer input_buffer_;
  ReadCallback read_cb_;
  IoHandler io_handler_;
};
