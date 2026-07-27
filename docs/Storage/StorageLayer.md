# Storage Layer

Owns string KVS (`KvStore`), semantic-cache nodes (`VNode` / `VNodeStorage` / `VNodeIndex`), and RESP codec for RDB node state (`RespVNodeCodec`). Mutation log / replication frames are RESP write commands (see [`../Persist/Aof.md`](../Persist/Aof.md)).

Live path: protocol dispatchers → `KvStore` / `VNodeIndex` (writes via `ApplyMutation`).

---

## Boundary

| Belongs here | Does not belong here |
|--------------|----------------------|
| `KvStore`, `VNode`, `VNodeStorage`, `VNodeIndex` | RESP wire decode |
| `RespVNodeCodec` (RDB NODES) | AOF / replication framing |

---

## Two encodings

| Purpose | Format |
|---------|--------|
| `RespVNodeCodec` (RDB NODES section) | RESP Array `[id, user_key, question, answer]` |
| AOF / replication write frames | RESP commands (`SET`/`DEL`/`VSET`/`VDEL`); vectors as bulk bytes |

Do not reuse node snapshot encoding for the mutation log.

---

## RespVNodeCodec

Encode/decode `VNode` ↔ RESP (`id` as decimal bulk, `user_key`, `question`, `answer`). Used by snapshot NODES section; not on the hot `VSET` path.

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| KvStore | `include/vemory/storage/KvStore.h` | `src/storage/KvStore.cc` |
| VNode | `include/vemory/storage/VNode.h` | — |
| VNodeStorage | `include/vemory/storage/VNodeStorage.h` | `src/storage/VNodeStorage.cc` |
| VNodeIndex | `include/vemory/storage/VNodeIndex.h` | `src/storage/VNodeIndex.cc` |
| RespVNodeCodec | `include/vemory/storage/RespVNodeCodec.h` | `src/storage/RespVNodeCodec.cc` |
