# Reactor / Network I/O Layer

Single-threaded epoll reactor: `EventLoop` listens and dispatches; `TcpServer` accepts; `TcpConn` owns the connection read/write half.  
Buffer semantics: [`MessageBuffer.md`](MessageBuffer.md); command frames: [`Protocol.md`](Protocol.md).

```
EventLoop::Run
  ├─ listen_fd readable → TcpServer::HandleAccept → new TcpConn → NewConnCallback
  └─ conn_fd I/O       → TcpConn::HandleIO
                           ├─ EPOLLIN  → Recv → MessageBuffer → ReadCallback
                           │              → ProtocolExecutor::OnReadable (official path)
                           ├─ EPOLLOUT → flush output_buffer_
                           └─ error / half-close → Close
```

Test entry: `tests/testcase.cc` wires `RespProtocolHandler` + `ProtocolExecutor` + `CommandHandler`.

---

## EventLoop

epoll event loop: register / modify / delete fds, block-wait then dispatch callbacks, and drive `Timer`.

### Interface

| API | Signature | Notes |
|-----|-----------|-------|
| `AddEvent` | `(int fd, uint32_t events, void* ptr)` | `epoll_ctl(ADD)`; `ptr` must point to a callable `std::function<void(uint32_t)>*` |
| `ModEvent` | `(int fd, uint32_t events, void* ptr)` | `epoll_ctl(MOD)` |
| `DelEvent` | `(int fd)` | `epoll_ctl(DEL)` |
| `Run` | `()` | Infinite loop: `epoll_wait` → invoke handler → `Timer::HandleTimeout` |

### Constraints

- `epoll_wait` timeout comes from `Timer::WaitTime()` (`-1` when there are no timers).
- Callback pointers are owned by the caller; `DelEvent` before destroying the object.
- Single-threaded assumption.

---

## TcpServer

### Interface

| API | Signature | Notes |
|-----|-----------|-------|
| Constructor | `TcpServer(EventLoop&)` | |
| `Start` | `(uint16_t port, NewConnCallback cb)` | Non-blocking listen, edge-triggered `EPOLLIN` |
| `NewConnCallback` | `void(shared_ptr<TcpConn>)` | Register `SetReadCallback` here |

### Typical usage (official RESP path)

```
auto protocol = make_shared<RespProtocolHandler>();
server.Start(port, [&](TcpConn::Ptr conn) {
  auto exec = make_shared<ProtocolExecutor>(
      protocol, on_dispatch, on_write, on_error);  // on_write = one Send per round
  conn->SetReadCallback([conn, exec] {
    exec->OnReadable(conn->Fd(), conn->InputBuffer());
  });
});
```

---

## TcpConn

### Interface

| API | Signature | Notes |
|-----|-----------|-------|
| `SetReadCallback` | `(ReadCallback)` | After a successful read round |
| `Fd` | `() const` | Connection fd |
| `InputBuffer` | `MessageBuffer&` | For `ProtocolExecutor` / parsers (do not mix with line helpers) |
| `GetDataUntilCrLf` | `string()` | Line-protocol convenience only |
| `GetAllData` | `string()` | Copy+consume all; **not** a RESP frame boundary |
| `Send` | `(const char*, size_t)` | |

### Constraints

- Official path: `SetReadCallback` → `ProtocolExecutor` → `RespProtocolHandler`. Do not call `RespHandler` from `TcpConn` itself.
- `GetDataUntilCrLf` / `GetAllData` remain for debug/line demos only.

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| EventLoop | `include/vemory/net/EventLoop.h` | (header-only) |
| TcpServer | `include/vemory/net/TcpServer.h` | `src/net/TcpServer.cc` |
| TcpConn | `include/vemory/net/TcpConnection.h` | `src/net/TcpConnection.cc` |
| MessageBuffer | `include/vemory/net/MessageBuffer.h` | (header-only) |

---

## Links

| Doc | Responsibility |
|-----|----------------|
| This doc | I/O, connections, event loop |
| [`MessageBuffer.md`](MessageBuffer.md) | Userspace read buffer |
| [`Protocol.md`](Protocol.md) | Protocol hook, RESP, `RequestContext` |
| [`../Storage/StorageLayer.md`](../Storage/StorageLayer.md) | `VNodeStorage`, `ProtobufVNodeCodec` (replication; unused by commands) |
| [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md) | `VectorSetRegistry`, `VectorSet`, `CommandHandler` |
