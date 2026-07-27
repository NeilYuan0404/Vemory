# Persist Layer — AOF (WAL)

Append-only RESP write-command log. Complements single-file RDB snapshots ([`Snapshot.md`](Snapshot.md)).

Live path: successful `SET` / `DEL` / `VSET` / `VDEL` → `ApplyMutation` → encode RESP when AOF or replicas need it → `WalManager::AppendFrame` and/or replication `FeedEncodedFrame` (only if `has_slaves()`).

Startup: optional `SnapshotManager::Load`, then `WalManager::Replay` (`MutateSource::kAofReplay` does **not** re-append).

**Breaking:** pre-RESP AOF (`u32le` + protobuf `WalEntry`) is not loaded — delete/rebuild `appendonly.aof`.

---

## Config

INI `[persistence]`:

| Key | Default | Meaning |
|-----|---------|---------|
| `dir` | `data` | Shared with RDB; AOF path is `{dir}/appendonly.aof` |
| `aof` | `false` | Enable append + startup replay |
| `aof_fsync` | `everysec` | `no` / `everysec` / `always` (Redis-style) |
| `aof_io` | `auto` | `auto` / `thread` / `iouring` (flush backend) |
| `aof_flush_interval_ms` | `1000` | Soft flush interval for inline buffer when below soft threshold |
| `load_on_startup` | `false` | RDB load (runs **before** AOF replay when both set) |

Empty `dir` disables AOF even if `aof=true`.

### `aof_io`

| Value | Behavior |
|-------|----------|
| `auto` | Prefer inline io_uring (needs `liburing` + capable kernel); on failure use `thread` |
| `thread` | Bounded queue + flush thread + batched `fwrite` / one `fflush` per batch (fallback / no liburing) |
| `iouring` | Same as auto's inline path; fallback to `thread` + warn if unavailable |

**Inline io_uring** (default when available): reactor-thread growable byte buffer (soft flush ~4 KiB, hard cap ~1 MiB) + `io_uring_prep_write` / `submit`; `EventLoop` idle calls `WalManager::Poll()` → `peek_cqe` + timed soft flush + everysec sync. No cross-thread queue.

Both backends honor `aof_fsync`. Replay is always synchronous file read + RESP decode.

### `aof_fsync`

| Value | Behavior |
|-------|----------|
| `no` | Write only (kernel page cache; max throughput / benches) |
| `everysec` | `fdatasync` at most once per second (Poll / flush thread) |
| `always` | `fdatasync` after every flush batch |

`Append` success means buffered/enqueued, not durable. Under `everysec`, a crash may lose up to ~1s of acknowledged writes (plus any still-buffered unsubmitted frames on the inline path).

---

## On-disk format

Concatenated RESP write commands (Redis-style), no length prefix:

```text
*3\r\n$3\r\nSET\r\n$…\r\n<key>\r\n$…\r\n<value>\r\n
*5\r\n$4\r\nVSET\r\n$…\r\n<vector_blob>\r\n…
```

Supported ops: `SET`, `DEL`, `VSET`, `VDEL`. Vector payloads are RESP bulk bytes (raw float32). Truncated tail stops replay at the last complete command.

AOF and replication share the same RESP frame bytes when both need them (encode once).

---

## Apply / source

```cpp
enum class MutateSource { kClient, kAofReplay };
ApplyMutation(ctx, src, vnode_index, kv, wal, repl);
```

| source | Mutate memory | Append AOF / feed repl |
|--------|---------------|------------------------|
| `kClient` | yes | AOF if enabled; repl only if `has_slaves()`; skip encode if neither |
| `kAofReplay` | yes | no |

DEL/VDEL miss (`integer_reply == 0`) does not append.

---

## Components

| Component | Path |
|-----------|------|
| `WalManager` | `include/vemory/persist/WalManager.h` |
| `AofWriter` | `include/vemory/persist/AofWriter.h` (`InlineIoUringAofWriter` / `ThreadAofWriter`) |
| `EventLoop::SetIdleCallback` | `include/vemory/net/EventLoop.h` (Poll hook) |
| `ApplyMutation` | `include/vemory/mutate/MutationApply.h` |
| `RespEncode::EncodeWriteCommand` | `include/vemory/protocol/resp/RespEncode.h` |
| `BlockingQueue` | `include/vemory/util/BlockingQueue.h` (`thread` backend) |

`ApplyMutation` (client) encodes one RESP frame only when AOF is on or slaves are connected, then appends to AOF and/or feeds the replication backlog. Inline path: soft/hard buffer + submit; `Poll()` peeks CQEs. `Flush()` drains pending and `fdatasync` when policy ≠ `no`.

---

## Limits

- No AOF rewrite after `SAVE` (file only grows while enabled)
- Crash may lose unsubmitted buffer / queued frames; under `everysec` also up to ~1s of OS-buffered data after write
- Enqueue success means not durable on disk
- Not Redis AOF rewrite / multi-part AOF
- `io_uring` needs system `liburing` and a capable kernel; otherwise `aof_io=auto|iouring` falls back to `thread`
