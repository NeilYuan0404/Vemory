# Protocol Parsing Layer

Take unread bytes from `MessageBuffer` → **`RespProtocolHandler::TryParse`** → owned `RequestContext` → hand off to `CommandHandler` / `HandlerRegister`.

Wire commands:

- Semantic cache (live): `VSET` / `VGET` / `VDEL` → `VNodeIndex`
- KVS (live): `SET` / `GET` / `DEL` → `KvStore`
- Assist (live): `PING` / `ECHO`
- Persistence (live): `SAVE` → `SnapshotManager` (fork background dump)
- Replication (live): `PSYNC` → `ReplicationMaster` (tmp RDB fullsync + backlog)

I/O: [`../Network/Reactor.md`](../Network/Reactor.md); storage: [`../Storage/StorageLayer.md`](../Storage/StorageLayer.md); persist: [`../Persist/Snapshot.md`](../Persist/Snapshot.md); ANN: [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md).

---

## End-to-End Path

```
TcpConn::ReadCallback
  → ProtocolExecutor::OnBufferReadable(fd, InputBuffer)
    → loop until NeedMore / Error:
         RespProtocolHandler::TryParse  // DecodeArrayOfBulk → FromTokens
         MessageBuffer::ReadCompleted(consumed)
         DispatchCallback(RequestContext, reply) → append reply to batch
              → CommandHandler → HandlerRegister[cmd]
                   → VNodeDispatcher → VNodeIndex
                   → KvsDispatcher    → KvStore
                   → AssistDispatcher → PING / ECHO
                   → PersistDispatcher → SnapshotManager
    → WriteCallback(batch) once  // pipeline: one Send per read round
```

---

## HandlerRegister / domain dispatchers

| Commands | Dispatcher | Notes |
|----------|------------|-------|
| `VSET` `VGET` `VDEL` | `VNodeDispatcher` | `arg` = `VNodeIndex*` |
| `SET` `GET` `DEL` | `KvsDispatcher` | `arg` = `KvStore*` |
| `PING` `ECHO` | `AssistDispatcher` | no store |
| `SAVE` | `PersistDispatcher` | `arg` = `SnapshotManager*` |
| `PSYNC` | `ReplicationDispatcher` | `arg` = `ReplicationMaster*` |

---

## CommandType / RequestContext

| Command | Args | Reply |
|---------|------|-------|
| `VSET` | `<vector_blob> <user_key> <question> <answer>` | `+OK` or `-ERR …` |
| `VGET` | `<query_vector_blob> <threshold>` | bulk `answer` or null bulk |
| `VDEL` | `<user_key>` | `:1` / `:0` |
| `SET` / `GET` / `DEL` | string KVS | as Redis-style |
| `PING` / `ECHO` | assist | |
| `SAVE` | (none) | `+OK` or `-ERR …` (fork background RDB) |
| `PSYNC` | (none) or `<replid> <offset>` | async `+FULLRESYNC <replid> <cut>` + RDB/backlog, or `+CONTINUE` + catch-up frames (see [`../Persist/Replication.md`](../Persist/Replication.md)) |

`vector_blob` / query blob: raw little-endian `float` bytes; `dim = len / sizeof(float)`.  
`threshold`: cosine **distance** upper bound (hit iff best distance ≤ threshold).

| Field | Notes |
|-------|-------|
| `vector_blob` | VSET/VGET binary floats |
| `user_key` / `question` / `answer` | VSET / VDEL |
| `threshold` | VGET distance gate |
| `key` / `element` | SET/GET/DEL / PING/ECHO |
| `psync_replid` / `psync_offset` | PSYNC partial (`-1` = fullsync request) |

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
| PersistDispatcher | `include/vemory/protocol/dispatcher/PersistDispatcher.h` | `src/protocol/dispatcher/PersistDispatcher.cc` |
| CommandHandler | `include/vemory/protocol/dispatcher/CommandHandler.h` | `src/protocol/dispatcher/CommandHandler.cc` |
