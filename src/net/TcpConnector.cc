#include "vemory/net/TcpConnector.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace TcpConnector {

int Connect(const std::string& host, uint16_t port, int timeout_ms) {
  if (host.empty() || port == 0) {
    return -1;
  }

  char port_str[16];
  std::snprintf(port_str, sizeof(port_str), "%u",
                static_cast<unsigned>(port));

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  if (::getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 ||
      res == nullptr) {
    return -1;
  }

  int fd = -1;
  for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
    fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      continue;
    }

    // Blocking connect with SO_SNDTIMEO as a simple timeout.
    if (timeout_ms > 0) {
      struct timeval tv {};
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
      ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
      break;
    }
    ::close(fd);
    fd = -1;
  }
  ::freeaddrinfo(res);

  if (fd < 0) {
    return -1;
  }

  // Clear timeouts; TcpConn will set O_NONBLOCK.
  struct timeval zero {};
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &zero, sizeof(zero));
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &zero, sizeof(zero));
  return fd;
}

}  // namespace TcpConnector
