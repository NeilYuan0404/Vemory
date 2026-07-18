#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

class TcpConn;
class EventLoop;

class TcpServer {
 public:
  using NewConnCallback = std::function<void(std::shared_ptr<TcpConn>)>;

  TcpServer(EventLoop& evloop);
  ~TcpServer();

  void Start(uint16_t port, NewConnCallback cb);

 private:
  void HandleAccept();

  EventLoop& evloop_;
  int listen_fd_;
  NewConnCallback new_conn_cb_;
  std::function<void(uint32_t)> accept_handler_;
  std::unordered_map<int, std::shared_ptr<TcpConn>> connections_;
};