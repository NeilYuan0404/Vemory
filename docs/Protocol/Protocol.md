# Protocol Parsing Layer

Take unread bytes from `MessageBuffer` → **`RespProtocolHandler::TryParse`** → owned `RequestContext` → hand off to `CommandHandler` / `HandlerRegister` (see [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md)).

Wire commands:

- Vector (live): `VADD` / `VSIM` / `VDIM` / `VEMB` / `VCARD`
- KVS (live): `SET` / `GET` / `DEL` → `KvStore` (`unordered_map`)
- Assist (live): `PING` / `ECHO`

I/O: [`../Network/Reactor.md`](../Network/Reactor.md); buffer: [`../Network/MessageBuffer.md`](../Network/MessageBuffer.md); vector sets: [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md).

---

## End-to-End Path

```
TcpConn::ReadCallback
  → ProtocolExecutor::OnReadable(fd, InputBuffer)
    → loop until NeedMore / Error:
         RespProtocolHandler::TryParse  // RespHandler → tokens → FromArgv
         MessageBuffer::ReadCompleted(consumed)
         DispatchCallback(RequestContext, reply) → append reply to batch
              → CommandHandler → HandlerRegister[cmd]
                   → VectorDispatcher → VectorSetRegistry
                   → KvsDispatcher    → KvStore
                   → AssistDispatcher → PING / ECHO
    → WriteCallback(batch) once  // pipeline: one Send per read round
```

`RequestContext` is the **output** of `TryParse`, not a parallel path.

---

## HandlerRegister / domain dispatchers

`CommandHandler` builds a function-pointer table (`HandlerRegister`) at construction:

| Commands | Dispatcher | Notes |
|----------|------------|-------|
| `VADD` `VSIM` `VDIM` `VEMB` `VCARD` | `VectorDispatcher` | `arg` = `VectorSetRegistry*` |
| `SET` `GET` `DEL` | `KvsDispatcher` | `arg` = `KvStore*` |
| `PING` `ECHO` | `AssistDispatcher` | no store (`arg` = null) |

`HandlerRegister::Dispatch` looks up `ctx.cmd`; missing / null entry → `ERR unknown command`.

---

## RespProtocolHandler / ProtocolExecutor

RESP-only parse path used by the network layer.

### RespProtocolHandler

| API | Signature | Notes |
|-----|-----------|-------|
| `TryParse` | `Status TryParse(int client_fd, MessageBuffer& buf, RequestContext* out, size_t* consumed)` | `kOk` / `kNeedMore` / `kError`. On `kOk`, `*out` owns strings |

`RespHandler::TryParse` → `RequestContext::FromArgv`.

### ProtocolExecutor

| API | Signature | Notes |
|-----|-----------|-------|
| Constructor | `(shared_ptr<RespProtocolHandler>, DispatchCallback, WriteCallback, ErrorCallback)` | |
| `OnReadable` | `(int client_fd, MessageBuffer& buf)` | Loop until `kNeedMore` / `kError`; append each reply, then **one** `WriteCallback` per round |

---

## Line Protocol vs RESP (Do Not Mix)

| | Line protocol | RESP (this layer) |
|--|---------------|-------------------|
| Frame shape | Single line, ends at `\r\n` | Multi-line: `*<n>\r\n` + several `$<len>\r\n<body>\r\n` |
| Buffer read | `MessageBuffer::GetDataUntilCRLF` | `GetAllData` (via `RespHandler` / `RespProtocolHandler`) |
| Who decides "frame complete" | First `\r\n` found | `RespDecode` (`kOk` / `kNeedMore`) |
| Consume | `ReadCompleted(line_len + 2)` | `ReadCompleted(consumed)` via `ProtocolExecutor` |
| Use | Simple text / debugging | **Vemory command channel (only supported path)** |

---

## Argument Representation: `std::string_view`

`tokens[i]` from `RespDecode` / `RespHandler` points into the read buffer bulk body and is **not copied**.

Constraints:

- Views are invalid after `ReadCompleted`.
- `RespProtocolHandler` copies into `RequestContext` before the executor consumes.

---

## RespDecode / RespEncode / RespHandler

Wire codec only. Headers under `include/vemory/protocol/resp/`.

| API | Notes |
|-----|-------|
| `RespDecode::DecodeArrayOfBulk` | Zero-copy `vector<string_view>` + `consumed` |
| `RespHandler::TryParse` | `GetAllData` → `DecodeArrayOfBulk` |
| `RespEncode::*` | Append reply frames to `string` |

---

## CommandType / RequestContext

Wire verbs → enum; `RequestContext::FromArgv` maps tokens.

| Command | Args |
|---------|------|
| `VADD` | `<key> VALUES <dim> <f1> … <fN> <element>` |
| `VSIM` | `<key> ELE <element> [COUNT <n>] [WITHSCORES]` or `<key> VALUES <dim> <f1>… [COUNT <n>] [WITHSCORES]` |
| `VDIM` | `<key>` |
| `VEMB` | `<key> <element>` |
| `VCARD` | `<key>` |
| `SET` | `<key> <value>` (value in `element`) |
| `GET` | `<key>` |
| `DEL` | `<key>` |
| `PING` | `[<message>]` (message in `element`; bare `PING` → `+PONG`) |
| `ECHO` | `<message>` (message in `element`) |

| Field (`RequestContext`) | Notes |
|--------------------------|-------|
| `client_fd` | For replies |
| `cmd` | `CommandType` |
| `key` | Key / vector set name |
| `element` | Element label (`VADD` / `VSIM ELE` / `VEMB`), SET value, or PING/ECHO message |
| `embed` | Floats for `VADD` / `VSIM VALUES` |
| `count` | `VSIM` neighbor count (default 10) |
| `with_scores` | `VSIM WITHSCORES` |
| `vsim_mode` | `ELE` or `VALUES` |
| `recv_time` | Set on successful `FromArgv` |

This layer stops at an owned `RequestContext`. Vector set ops: [EmbedIndex](../Index/EmbedIndex.md).

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| ProtocolExecutor | `include/vemory/protocol/ProtocolExecutor.h` | `src/protocol/ProtocolExecutor.cc` |
| RespProtocolHandler | `include/vemory/protocol/resp/RespProtocolHandler.h` | `src/protocol/resp/RespProtocolHandler.cc` |
| RespDecode | `include/vemory/protocol/resp/RespDecode.h` | `src/protocol/resp/RespDecode.cc` |
| RespEncode | `include/vemory/protocol/resp/RespEncode.h` | `src/protocol/resp/RespEncode.cc` |
| RespHandler | `include/vemory/protocol/resp/RespHandler.h` | `src/protocol/resp/RespHandler.cc` |
| CommandType | `include/vemory/protocol/CommandType.h` | `src/protocol/CommandType.cc` |
| RequestContext | `include/vemory/protocol/RequestContext.h` | `src/protocol/RequestContext.cc` |
| HandlerRegister | `include/vemory/protocol/HandlerRegister.h` | `src/protocol/HandlerRegister.cc` |
| VectorDispatcher | `include/vemory/protocol/VectorDispatcher.h` | `src/protocol/VectorDispatcher.cc` |
| KvsDispatcher | `include/vemory/protocol/KvsDispatcher.h` | `src/protocol/KvsDispatcher.cc` |
| AssistDispatcher | `include/vemory/protocol/AssistDispatcher.h` | `src/protocol/AssistDispatcher.cc` |
| CommandHandler | `include/vemory/protocol/CommandHandler.h` | `src/protocol/CommandHandler.cc` |
