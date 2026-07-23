# Storage Layer

Owns string KVS (`KvStore`), semantic-cache nodes (`VNode` / `VNodeStorage` / `VNodeIndex`), and protobuf codec for snapshot nodes / future replication.

Live command paths:

- Semantic cache: `VSET` / `VGET` / `VDEL` → `VNodeIndex` (see also [`../Index/EmbedIndex.md`](../Index/EmbedIndex.md))
- String KVS: `SET` / `GET` / `DEL` → `KvStore` via `KvsDispatcher`

RDB snapshots live in [`../Persist/Snapshot.md`](../Persist/Snapshot.md) (`SnapshotManager`).

Network / parse: [`../Protocol/Protocol.md`](../Protocol/Protocol.md).

---

## Boundary

| Belongs here | Does not belong here |
|--------------|----------------------|
| `KvStore` (SET/GET/DEL + Dump/Load) | RESP wire decode |
| `VNode` / `VNodeStorage` / `VNodeIndex` | Snapshot fork / fsync / rename |
| `ProtobufVNodeCodec` (snapshot nodes / future replication) | |

---

## KvStore

In-memory string map for Redis-style `SET` / `GET` / `DEL`.

| API | Notes |
|-----|-------|
| `Set` / `Get` / `Del` | Average O(1); empty key rejected on Set |
| `Dump` / `Load` | Binary snapshot segment for `dump.kv` |

---

## VNode / VNodeStorage / VNodeIndex

`VNode` = `{id, user_key, question, answer}`. Vectors live only in `USearchEmbedIndex`.

| Component | Role |
|-----------|------|
| `VNodeStorage` | `by_id` + `by_user_key`; same `user_key` reuses id; `Restore` / `ForEach` for snapshot |
| `VNodeIndex` | Orchestrates storage + ANN; `DumpNodes` / `LoadNodes` / `SaveIndex` / `LoadIndex` |

---

## ProtobufVNodeCodec

Encode/decode `VNode` ↔ protobuf (`id`, `user_key`, `question`, `answer`). Used by snapshot nodes file; not on the hot `VSET` path.

---

## Paths

| Component | Header / schema | Source |
|-----------|-----------------|--------|
| KvStore | `include/vemory/storage/KvStore.h` | `src/storage/KvStore.cc` |
| VNode | `include/vemory/storage/VNode.h` | (header-only) |
| VNodeStorage | `include/vemory/storage/VNodeStorage.h` | `src/storage/VNodeStorage.cc` |
| VNodeIndex | `include/vemory/storage/VNodeIndex.h` | `src/storage/VNodeIndex.cc` |
| ProtobufVNodeCodec | `include/vemory/storage/ProtobufVNodeCodec.h` | `src/storage/ProtobufVNodeCodec.cc` |
| VNode.proto | `proto/VNode.proto` | `generated/VNode.pb.{h,cc}` |
