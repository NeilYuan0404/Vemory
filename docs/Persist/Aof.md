# Persist Layer — AOF (WAL)

Append-only protobuf log of write mutations. Complements multi-file RDB snapshots ([`Snapshot.md`](Snapshot.md)).

Live path: successful `SET` / `DEL` / `VSET` / `VDEL` → `ApplyMutation` → `WalManager::Append` (encode + enqueue) → flush thread `fwrite` + `fflush`, then optional `fdatasync` per `aof_fsync`.

Startup: optional `SnapshotManager::Load`, then `WalManager::Replay` (`MutateSource::kAofReplay` does **not** re-append).

---

## Config

INI `[persistence]`:

| Key | Default | Meaning |
|-----|---------|---------|
| `dir` | `data` | Shared with RDB; AOF path is `{dir}/appendonly.aof` |
| `aof` | `false` | Enable append + startup replay |
| `aof_fsync` | `everysec` | `no` / `everysec` / `always` (Redis-style) |
| `load_on_startup` | `false` | RDB load (runs **before** AOF replay when both set) |

Empty `dir` disables AOF even if `aof=true`.

### `aof_fsync`

| Value | Behavior |
|-------|----------|
| `no` | `fflush` only (kernel page cache; max throughput / benches) |
| `everysec` | `fdatasync` at most once per second; idle flush thread wakes to sync a dirty tail |
| `always` | `fdatasync` after every frame |

`Append` success means enqueued, not durable. Under `everysec`, a crash may lose up to ~1s of acknowledged writes (plus any still-queued frames).

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
| `BlockingQueue` | `include/vemory/util/BlockingQueue.h` |

`Append` serializes on the caller thread and pushes a complete frame into a bounded queue (capacity 1024; full → block). One flush thread pops (1s timed wait) and writes (`fwrite` + `fflush`), then applies `aof_fsync`. `Flush()` waits until pending frames are written and then `fdatasync` when policy ≠ `no`.

---

## Limits

- No AOF rewrite after `SAVE` (file only grows while enabled)
- Crash may lose queued frames; under `everysec` also up to ~1s of OS-buffered data after `fflush`
- `Append` success means enqueued, not durable on disk
- Not Redis AOF / RESP format
- No io_uring yet
