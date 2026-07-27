# Persist Layer — AOF (WAL)

Append-only RESP write-command log. Complements single-file RDB snapshots ([`Snapshot.md`](Snapshot.md)).

Live path: successful `SET` / `DEL` / `VSET` / `VDEL` → `ApplyMutation` → encode RESP once → `WalManager::AppendFrame` + replication `FeedEncodedFrame` → background AOF flush, then optional `fdatasync` per `aof_fsync`.

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
| `aof_io` | `thread` | `auto` / `thread` / `iouring` (flush backend; prefer `thread`) |
| `load_on_startup` | `false` | RDB load (runs **before** AOF replay when both set) |

Empty `dir` disables AOF even if `aof=true`.

### `aof_io`

| Value | Behavior |
|-------|----------|
| `auto` | Try io_uring (needs `liburing` + capable kernel); on failure use `thread` |
| `thread` | SPSC `RingBuffer` + flush thread + batched `fwrite` / one `fflush` per batch (**recommended**) |
| `iouring` | SPSC `RingBuffer` + flush thread; pipelined `io_uring` `writev` (`submit` + `peek_cqe`); fallback to `thread` + warn if unavailable. **Experimental.** |

Both backends honor `aof_fsync`. Replay is always synchronous file read + RESP decode.

### `aof_fsync`

| Value | Behavior |
|-------|----------|
| `no` | `fflush` only (kernel page cache; max throughput / benches) |
| `everysec` | `fdatasync` at most once per second; idle flush thread wakes to sync a dirty tail |
| `always` | `fdatasync` after every flush batch (up to 32 frames) |

`Append` success means enqueued, not durable. Under `everysec`, a crash may lose up to ~1s of acknowledged writes (plus any still-queued frames).

---

## On-disk format

Concatenated RESP write commands (Redis-style), no length prefix:

```text
*3\r\n$3\r\nSET\r\n$…\r\n<key>\r\n$…\r\n<value>\r\n
*5\r\n$4\r\nVSET\r\n$…\r\n<vector_blob>\r\n…
```

Supported ops: `SET`, `DEL`, `VSET`, `VDEL`. Vector payloads are RESP bulk bytes (raw float32). Truncated tail stops replay at the last complete command.

AOF and replication share the same RESP frame bytes (encode once on the client write path).

---

## Apply / source

```cpp
enum class MutateSource { kClient, kAofReplay };
ApplyMutation(ctx, src, vnode_index, kv, wal, repl);
```

| source | Mutate memory | Append AOF / feed repl |
|--------|---------------|------------------------|
| `kClient` | yes | yes if enabled / repl set |
| `kAofReplay` | yes | no |

DEL/VDEL miss (`integer_reply == 0`) does not append.

---

## Components

| Component | Path |
|-----------|------|
| `WalManager` | `include/vemory/persist/WalManager.h` |
| `AofWriter` | `include/vemory/persist/AofWriter.h` (`ThreadAofWriter` / `IoUringAofWriter`) |
| `ApplyMutation` | `include/vemory/mutate/MutationApply.h` |
| `RespEncode::EncodeWriteCommand` | `include/vemory/protocol/resp/RespEncode.h` |
| `RingBuffer` | `include/vemory/util/ringbuffer.h` (both `thread` and `iouring` backends; bounded SPSC + `PushWait` backpressure) |

`ApplyMutation` (client) encodes one RESP frame, then enqueues to AOF and feeds the replication backlog. Flush thread pops batches and writes; `Flush()` waits until pending frames complete writes and then `fdatasync` when policy ≠ `no`.

---

## Limits

- No AOF rewrite after `SAVE` (file only grows while enabled)
- Crash may lose queued frames; under `everysec` also up to ~1s of OS-buffered data after write
- Enqueue success means not durable on disk
- Not Redis AOF rewrite / multi-part AOF
- `io_uring` needs system `liburing` and a capable kernel; otherwise `aof_io=auto|iouring` falls back to `thread`
