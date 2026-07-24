# Persist Layer — AOF (WAL)

Append-only protobuf log of write mutations. Complements multi-file RDB snapshots ([`Snapshot.md`](Snapshot.md)).

Live path: successful `SET` / `DEL` / `VSET` / `VDEL` → `ApplyMutation` → `WalManager::Append` (sync write).

Startup: optional `SnapshotManager::Load`, then `WalManager::Replay` (`MutateSource::kAofReplay` does **not** re-append).

---

## Config

INI `[persistence]`:

| Key | Default | Meaning |
|-----|---------|---------|
| `dir` | `data` | Shared with RDB; AOF path is `{dir}/appendonly.aof` |
| `aof` | `false` | Enable append + startup replay |
| `load_on_startup` | `false` | RDB load (runs **before** AOF replay when both set) |

Empty `dir` disables AOF even if `aof=true`.

---

## Two protobuf schemas

| Message | File | Role |
|---------|------|------|
| `VNodePb` | `proto/VNode.proto` | RDB node **state** (no vector) via `ProtobufVNodeCodec` |
| `WalEntry` | `proto/WalEntry.proto` | One **mutation** (SET/DEL/VSET/VDEL; VSET includes `vector`) |

AOF and future replication share `WalEntry`. Do not reuse `ProtobufVNodeCodec` for the log.

---

## On-disk frame

```text
u32le payload_len | protobuf WalEntry bytes
```

No CRC in MVP. Truncated tail (incomplete length or payload) stops replay at the last good record.

---

## Apply / source

```cpp
enum class MutateSource { kClient, kAofReplay };
ApplyMutation(entry, src, vnode_index, kv, wal);
```

| source | Mutate memory | Append AOF |
|--------|---------------|------------|
| `kClient` | yes | yes if `wal` enabled |
| `kAofReplay` | yes | no |

DEL/VDEL miss (`integer_reply == 0`) does not append.

---

## Components

| Component | Path |
|-----------|------|
| `WalManager` | `include/vemory/persist/WalManager.h` |
| `ApplyMutation` | `include/vemory/persist/MutationApply.h` |

MVP: synchronous `fwrite` + `fflush`. Phase 2 (not implemented): background flush thread draining a bounded [`BlockingQueue`](../../include/vemory/util/BlockingQueue.h) (SPSC-style: reactor encodes frames, one writer thread `write`/`fsync`), and/or io_uring submit with CQE drain in `EventLoop`, optional everysec `fdatasync`. The queue header is already in-tree; `WalManager` is not wired to it yet.

---

## Limits

- No AOF rewrite after `SAVE` (file only grows while enabled)
- Crash may lose the last unflushed OS buffers (no everysec fsync yet)
- Not Redis AOF / RESP format
