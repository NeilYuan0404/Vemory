# Persist Layer — AOF (WAL)

Append-only protobuf log of write mutations. Complements single-file RDB snapshots ([`Snapshot.md`](Snapshot.md)).

Live path: successful `SET` / `DEL` / `VSET` / `VDEL` → `ApplyMutation` → `WalManager::Append` (encode + enqueue) → `AofWriter` background thread (batched `thread` fwrite or `io_uring` writev), then optional `fdatasync` per `aof_fsync`.

Startup: optional `SnapshotManager::Load`, then `WalManager::Replay` (`MutateSource::kAofReplay` does **not** re-append).

---

## Config

INI `[persistence]`:

| Key | Default | Meaning |
|-----|---------|---------|
| `dir` | `data` | Shared with RDB; AOF path is `{dir}/appendonly.aof` |
| `aof` | `false` | Enable append + startup replay |
| `aof_fsync` | `everysec` | `no` / `everysec` / `always` (Redis-style) |
| `aof_io` | `thread` | `auto` / `thread` / `iouring` (flush backend; prefer `thread`) |
| `load_on_startup` | `false` | RDB load (runs **before** AOF replay when both set) |

Empty `dir` disables AOF even if `aof=true`.

### `aof_io`

| Value | Behavior |
|-------|----------|
| `auto` | Try io_uring (needs `liburing` + capable kernel); on failure use `thread` |
| `thread` | Bounded queue + flush thread + batched `fwrite` / one `fflush` per batch (**recommended**) |
| `iouring` | SPSC `RingBuffer` + flush thread; batches frames into pipelined `io_uring` `writev` (`submit` + `peek_cqe`, up to 8 in-flight); fallback to `thread` + warn if unavailable. **Experimental — not recommended for real use yet.** |

Both backends honor `aof_fsync`. Replay is always synchronous `fopen` read (not via io_uring).

### `aof_fsync`

| Value | Behavior |
|-------|----------|
| `no` | `fflush` only (kernel page cache; max throughput / benches) |
| `everysec` | `fdatasync` at most once per second; idle flush thread wakes to sync a dirty tail |
| `always` | `fdatasync` after every flush batch (up to 32 frames) |

`Append` success means enqueued, not durable. Under `everysec`, a crash may lose up to ~1s of acknowledged writes (plus any still-queued frames).

---

## Two protobuf schemas

| Message | File | Role |
|---------|------|------|
| `VNodePb` | `proto/VNode.proto` | RDB node **state** (no vector) via `ProtobufVNodeCodec` |
| `WalEntry` | `proto/WalEntry.proto` | One **mutation** (SET/DEL/VSET/VDEL; VSET includes `vector`) |

AOF and replication share `WalEntry` frames. Do not reuse `ProtobufVNodeCodec` for the log.

---

## On-disk frame

```text
u32le payload_len | protobuf WalEntry bytes
```

No per-frame CRC. Truncated tail (incomplete length or payload) stops replay at the last good record.

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
| `AofWriter` | `include/vemory/persist/AofWriter.h` (`ThreadAofWriter` / `IoUringAofWriter`) |
| `ApplyMutation` | `include/vemory/mutate/MutationApply.h`（共享突变层，非 AOF writer） |
| `BlockingQueue` | `include/vemory/util/BlockingQueue.h` (`thread` backend) |
| `RingBuffer` | `include/vemory/util/ringbuffer.h` (`iouring` backend; lock-free data + CV wake) |

`Append` serializes on the caller thread and enqueues a complete frame via `AofWriter` (capacity 1024; full → block).

- **thread**: flush thread `PopWaitFor` (1s), drains up to 32 frames, one `fwrite`+`fflush`, then `aof_fsync`.
- **iouring**: flush thread pops from SPSC `RingBuffer`, `prep_writev` + `io_uring_submit` (frames kept alive in a PendingOp until CQE), non-blocking `io_uring_peek_cqe` reclaim (multiple batches in flight), then `aof_fsync` (`always` per completed batch; `everysec` on a timer / idle wake).

`Flush()` waits until pending frames have completed writes (CQE for iouring) and then `fdatasync` when policy ≠ `no`.

---

## Limits

- No AOF rewrite after `SAVE` (file only grows while enabled)
- Crash may lose queued frames; under `everysec` also up to ~1s of OS-buffered data after write
- `Append` success means enqueued, not durable on disk
- Not Redis AOF / RESP format
- `io_uring` needs system `liburing` and a capable kernel; otherwise `aof_io=auto|iouring` falls back to `thread`
