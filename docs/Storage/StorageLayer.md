# Storage Layer

Owns string KVS (`KvStore`), domain `VNode` helpers, and a codec reserved for **replication**. Live command paths:

- Vector sets: [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md)
- String KVS: `SET` / `GET` / `DEL` → `KvStore` via `KvsDispatcher`

Network / parse: [`../Protocol/Protocol.md`](../Protocol/Protocol.md).

---

## Boundary

| Belongs here | Does not belong here |
|--------------|----------------------|
| `KvStore` (SET/GET/DEL) | RESP wire decode |
| `ProtobufVNodeCodec` (replication) | `VectorSet` / ANN |
| `VNode` / `VNodeStorage` (in-memory Q&A maps; unused by current commands) | |

---

## KvStore

In-memory string map for Redis-style `SET` / `GET` / `DEL`. Backed by `std::unordered_map<std::string, std::string>` (average O(1) lookup).

| API | Notes |
|-----|-------|
| `Set(key, value)` | Insert or replace; empty key rejected |
| `Get(key, out)` | `kNotFound` if missing |
| `Del(key)` | Erase one entry; `kNotFound` if missing |
| `size()` | Entry count |

Wire replies (`KvsDispatcher`): `SET` → `+OK`, `GET` → bulk / null bulk, `DEL` → `:1` / `:0`.

---

## ProtobufVNodeCodec

Encode/decode between domain `VNode` and opaque protobuf bytes. Kept for **replication** (and future persist); not called on `VADD`/`VSIM`/… or `SET`/`GET`/`DEL`.

Uses `vemory.VNodePb` (`proto/VNode.proto`): `id`, `prompt`, `answer` (no embed).

---

## VNode / VNodeStorage

`VNode` = `{id, prompt, answer}`. `VNodeStorage` stores nodes by value with prompt secondary index. Present for future use / unit tests; **not** wired into `src/Vemory.cc` today. Distinct from `KvStore`.

---

## Paths

| Component | Header / schema | Source |
|-----------|-----------------|--------|
| KvStore | `include/vemory/storage/KvStore.h` | `src/storage/KvStore.cc` |
| ProtobufVNodeCodec | `include/vemory/storage/ProtobufVNodeCodec.h` | `src/storage/ProtobufVNodeCodec.cc` |
| VNode.proto | `proto/VNode.proto` | `generated/VNode.pb.{h,cc}` |
| VNode | `include/vemory/protocol/VNode.h` | (header-only) |
| VNodeStorage | `include/vemory/storage/VNodeStorage.h` | `src/storage/VNodeStorage.cc` |

---

## Follow-ups (not implemented)

- Sharded / concurrent maps for `KvStore`
- Replication using `ProtobufVNodeCodec`
- Persist / WAL
- Re-link Q&A nodes to vector set elements if needed
