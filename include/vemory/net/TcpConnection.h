#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "vemory/net/EventLoop.h"
#include "vemory/net/MessageBuffer.h"

// TCP connection
class TcpConn : public std::enable_shared_from_this<TcpConn> {
 public:
  using Ptr = std::shared_ptr<TcpConn>;
  using ReadCallback = std::function<void()>;
  using CloseCallback = std::function<void()>;
  using SendFileDoneCallback = std::function<void(bool ok)>;

  TcpConn(int fd, EventLoop& evloop);

  ~TcpConn();

  void SetReadCallback(ReadCallback cb) { read_cb_ = std::move(cb); }
  void SetCloseCallback(CloseCallback cb) { close_cb_ = std::move(cb); }

  int Fd() const { return fd_; }
  bool closed() const { return closed_; }

  // Bytes pending in userspace / in-flight sendfile (for replica soft limits).
  size_t OutputBufferedBytes() const;

  // Unconsumed read buffer for protocol parsers (RESP path).
  MessageBuffer& InputBuffer() { return input_buffer_; }

  int Send(const char* data, size_t size);

  // After output_buffer_ drains, stream file via sendfile. Calls done when
  // finished or on error (connection may already be closed on failure).
  bool SendFile(const std::string& path, SendFileDoneCallback done = {});

  // Public close for replication (kick slow replica).
  void ForceClose() { Close(); }

 private:
  static void SetNonBlocking(int fd);
  void Close();

  void HandleIO(IoEvents events);
  void HandleRead();
  void HandleWrite();

  void DisableWrite();
  void EnableWrite();
  void ClearSendFile(bool ok);

  int fd_;
  EventLoop& evloop_;
  bool closed_;
  std::string output_buffer_;
  MessageBuffer input_buffer_;
  ReadCallback read_cb_;
  CloseCallback close_cb_;
  IoHandler io_handler_;

  int sendfile_fd_ = -1;
  off_t sendfile_offset_ = 0;
  uint64_t sendfile_remaining_ = 0;
  SendFileDoneCallback sendfile_done_;
  // Bytes queued while sendfile is in progress (sent after file completes).
  std::string post_sendfile_buffer_;
};
