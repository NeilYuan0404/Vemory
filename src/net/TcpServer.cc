#include "vemory/net/TcpServer.h"

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include <spdlog/spdlog.h>

#include "vemory/net/EventLoop.h"
#include "vemory/net/TcpConnection.h"

TcpServer::TcpServer(EventLoop& evloop) : evloop_(evloop), listen_fd_(-1) {}

TcpServer::~TcpServer() {
  if (listen_fd_ != -1) {
    evloop_.DelEvent(listen_fd_);
    close(listen_fd_);
  }
}

void TcpServer::Start(uint16_t port, NewConnCallback cb) {
  new_conn_cb_ = cb;
  listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);  // nonblock
  if (listen_fd_ == -1) {
    spdlog::error("socket error: {}", errno);
    return;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  int opt = 1;
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1)  // allow quick restart
  {
    spdlog::error("setsockopt error: {}", errno);
    close(listen_fd_);
    return;
  }
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) ==
      -1) {
    spdlog::error("setsockopt error: {}", errno);
    close(listen_fd_);
    return;
  }

  if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
    spdlog::error("bind error: {}", errno);
    close(listen_fd_);
    return;
  }

  if (::listen(listen_fd_, SOMAXCONN) == -1) {
    spdlog::error("listen error: {}", errno);
    close(listen_fd_);
    return;
  }

  accept_handler_ = [this](uint32_t) { HandleAccept(); };
  evloop_.AddEvent(listen_fd_, EPOLLIN | EPOLLET, &accept_handler_);
  spdlog::info("Server started on port {}", port);
}

void TcpServer::HandleAccept() {
  while (true) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int conn_fd =
        ::accept4(listen_fd_, (sockaddr*)&client_addr, &len, SOCK_NONBLOCK);
    if (conn_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      spdlog::error("accept4 error: {}", errno);
      break;
    }

    auto conn = std::make_shared<TcpConn>(conn_fd, evloop_);
    if (new_conn_cb_) new_conn_cb_(conn);
  }
}
