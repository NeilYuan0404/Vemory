#pragma once

#include <cstdint>
#include <string>

// Synchronous TCP connect helper for replication slave → master.
namespace TcpConnector {

// Returns connected non-blocking socket fd, or -1 on failure.
int Connect(const std::string& host, uint16_t port, int timeout_ms = 3000);

}  // namespace TcpConnector
