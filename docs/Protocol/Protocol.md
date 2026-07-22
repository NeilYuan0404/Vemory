# Protocol Parsing Layer

Take unread bytes from `MessageBuffer` → **`RespProtocolHandler::TryParse`** → owned `RequestContext` → hand off to `CommandHandler` / `HandlerRegister`.

Wire commands:

- Semantic cache (live): `VSET` / `VGET` / `VDEL` → `VNodeIndex`
- KVS (live): `SET` / `GET` / `DEL` → `KvStore`
- Assist (live): `PING` / `ECHO`

I/O: [`../Network/Reactor.md`](../Network/Reactor.md); storage: [`../Storage/StorageLayer.md`](../Storage/StorageLayer.md); ANN: [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md).

---

## End-to-End Path

```
TcpConn::ReadCallback
  → ProtocolExecutor::OnBufferReadable(fd, InputBuffer)
    → loop until NeedMore / Error:
         RespProtocolHandler::TryParse  // DecodeArrayOfBulk → FromArgv
         MessageBuffer::ReadCompleted(consumed)
         DispatchCallback(RequestContext, reply) → append reply to batch
              → CommandHandler → HandlerRegister[cmd]
                   → VNodeDispatcher → VNodeIndex
                   → KvsDispatcher    → KvStore
                   → AssistDispatcher → PING / ECHO
    → WriteCallback(batch) once  // pipeline: one Send per read round
```

---

## HandlerRegister / domain dispatchers

| Commands | Dispatcher | Notes |
|----------|------------|-------|
| `VSET` `VGET` `VDEL` | `VNodeDispatcher` | `arg` = `VNodeIndex*` |
| `SET` `GET` `DEL` | `KvsDispatcher` | `arg` = `KvStore*` |
| `PING` `ECHO` | `AssistDispatcher` | no store |

---

## CommandType / RequestContext

| Command | Args | Reply |
|---------|------|-------|
| `VSET` | `<vector_blob> <user_key> <question> <answer>` | `+OK` or `-ERR …` |
| `VGET` | `<query_vector_blob> <threshold>` | bulk `answer` or null bulk |
| `VDEL` | `<user_key>` | `:1` / `:0` |
| `SET` / `GET` / `DEL` | string KVS | as Redis-style |
| `PING` / `ECHO` | assist | |

`vector_blob` / query blob: raw little-endian `float` bytes; `dim = len / sizeof(float)`.  
`threshold`: cosine **distance** upper bound (hit iff best distance ≤ threshold).

| Field | Notes |
|-------|-------|
| `vector_blob` | VSET/VGET binary floats |
| `user_key` / `question` / `answer` | VSET / VDEL |
| `threshold` | VGET distance gate |
| `key` / `element` | SET/GET/DEL / PING/ECHO |

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| ProtocolExecutor | `include/vemory/protocol/ProtocolExecutor.h` | `src/protocol/ProtocolExecutor.cc` |
| RespProtocolHandler | `include/vemory/protocol/resp/RespProtocolHandler.h` | `src/protocol/resp/RespProtocolHandler.cc` |
| CommandType | `include/vemory/protocol/CommandType.h` | `src/protocol/CommandType.cc` |
| RequestContext | `include/vemory/protocol/RequestContext.h` | `src/protocol/RequestContext.cc` |
| HandlerRegister | `include/vemory/protocol/dispatcher/HandlerRegister.h` | `src/protocol/dispatcher/HandlerRegister.cc` |
| VNodeDispatcher | `include/vemory/protocol/dispatcher/VNodeDispatcher.h` | `src/protocol/dispatcher/VNodeDispatcher.cc` |
| KvsDispatcher | `include/vemory/protocol/dispatcher/KvsDispatcher.h` | `src/protocol/dispatcher/KvsDispatcher.cc` |
| AssistDispatcher | `include/vemory/protocol/dispatcher/AssistDispatcher.h` | `src/protocol/dispatcher/AssistDispatcher.cc` |
| CommandHandler | `include/vemory/protocol/dispatcher/CommandHandler.h` | `src/protocol/dispatcher/CommandHandler.cc` |
