# Persist Layer

Owns single-file RDB snapshots (`SnapshotManager`). Depends on the storage layer for dump/load of in-memory state; does not own hot-path Put/Get.

Path API (`BackgroundSaveToPath` / `LoadFromPath`) is also used by replication fullsync under `tmp/` and does **not** require `persistence.dir`. See [`Replication.md`](Replication.md).

Live command path: `SAVE` → `PersistDispatcher` → `SnapshotManager::BackgroundSave` (fork).

Storage APIs used here: [`../Storage/StorageLayer.md`](../Storage/StorageLayer.md). Protocol: [`../Protocol/Protocol.md`](../Protocol/Protocol.md).

---

## Boundary

| Belongs here | Does not belong here |
|--------------|----------------------|
| `SnapshotManager` (RDB dump/load, fork, fsync/rename) | RESP wire decode |
| Snapshot file layout under `persistence.dir` | In-memory `KvStore` / `VNodeIndex` logic |

---

## Config

INI `[persistence]` (see `conf/vemory.ini`):

| Key | Default | Meaning |
|-----|---------|---------|
| `dir` | `data` | Snapshot directory; empty string disables `SAVE` (`kNotConfigured`) |
| `load_on_startup` | `false` | If true and `dir` set, call `Load()` before listen |

Built-in default and `conf/vemory.ini` both use `data/` (created on first `SAVE` if missing).

---

## File layout

Under `dir`, a single file `dump.rdb`:

```text
dump.rdb
  Header (96 bytes)
    magic[8] = "VEMORYDB"
    version   u32 = 3
    flags     u32   // bit0: has_usearch
    dim       u64
    next_id   u32
    pad       u32
    kv_count  u64
    node_count u64
    toc[3]: { offset u64, length u64 }  // 0=KV, 1=NODES, 2=USEARCH
  Payload
    [KV bytes][NODES bytes][USEARCH bytes?]
```

| Segment | Content |
|---------|---------|
| KV | KvStore binary (`KvStore::Dump` / `Load`) |
| NODES | length-prefixed RESP Array via `RespVNodeCodec` (`[id, user_key, question, answer]`; no vectors) |
| USEARCH | usearch native bytes (`USearchEmbedIndex::Save` / `Load` on `FILE*`); omitted when `dim == 0` (`toc[2].length = 0`) |

Write path: write `dump.rdb.tmp` (header reserved, then payloads, then rewrite header/TOC) → `fflush`/`fsync` → `rename` to `dump.rdb`. Successful SAVE also removes legacy multi-file names (`dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch` and `.tmp`).

Format is Vemory-specific (not Redis RDB-compatible). **RDB v3 only** (v2 protobuf NODES not loaded). Optional RESP AOF: [`Aof.md`](Aof.md) (`persistence.aof`).

Wire: `redis-cli SAVE` (default dir `data/`; empty `persistence.dir` disables).

## SnapshotManager API

Header: `include/vemory/persist/SnapshotManager.h`.

### Construction

```cpp
SnapshotManager snapshot(&vnode_index, &kv, cfg.persistence_dir);
```

| Arg | Role |
|-----|------|
| `VNodeIndex*` | Must outlive the manager; nodes + ANN dumped/loaded through it |
| `KvStore*` | Must outlive the manager |
| `dir` | Snapshot directory string (may be empty) |

Destructor cancels the poll timer and, if a save child is still running, **blocking** `waitpid` until it exits.

### Status

| Value | Meaning |
|-------|---------|
| `kOk` | Success |
| `kBadValue` | Null args / invalid use |
| `kNotConfigured` | `dir` empty |
| `kInProgress` | `BackgroundSave` while a child is already running |
| `kIoError` | Open/read/write/fsync/rename/missing files failed |
| `kError` | Bad magic/version, count mismatch, fork failure, etc. |

### Accessors

| Method | Notes |
|--------|-------|
| `dir()` | Configured path |
| `configured()` | `!dir().empty()` |
| `save_in_progress()` | `true` while background child pid is tracked |

### `BackgroundSave()` — fork (wire `SAVE`)

```text
BackgroundSave()
  → if dir empty → kNotConfigured
  → if child already running → kInProgress
  → fork()
       parent: record pid, schedule Timer poll, return kOk  (+OK to client)
       child:  private SaveToDir(); _exit(0|1)
```

`SaveToDir()` is private: synchronous dump runs only in the forked child (creates `dir` if missing). Returns `kNotConfigured` if `dir` empty or store pointers null; `kIoError` / `kError` on failure; `kOk` on success.

Does **not** wait for the dump to finish. Concurrent second `SAVE` → `kInProgress` → RESP `-ERR Background save already in progress`.

`PersistDispatcher` maps:

| Status | RESP |
|--------|------|
| `kOk` | `+OK` |
| `kNotConfigured` | `-ERR persistence dir not set` |
| `kInProgress` | `-ERR Background save already in progress` |
| other | `-ERR save failed` |

### `ReapSaveChild()` — reap background save

Non-blocking `waitpid(WNOHANG)`. Clears `child_pid_` when the child exits; logs success/failure.

`BackgroundSave` registers a recurring ~100ms `Timer` via `EnsureReapTimer` that calls `ReapSaveChild` until the child is gone. Callers can also invoke `ReapSaveChild` manually (tests do this).

### `Load()` — synchronous restore

Replaces in-memory KV + semantic cache from `dir` on the **calling thread**.

Order:

1. Open `dump.rdb`, read/validate Header (magic `VEMORYDB`, version 3)
2. Seek TOC[0]; `KvStore::Clear` + `Load` (check `kv_count`)
3. Seek TOC[1]; `VNodeIndex::Clear` + `LoadNodes` (restore ids / `next_id`)
4. If `dim > 0`, seek TOC[2]; `LoadIndex` from the usearch segment

Used at startup when `load_on_startup` is true (see `src/Vemory.cc`):

- `kOk` → log and continue
- `kIoError` → treat as “no usable snapshot”, start empty (warn)
- other → fatal exit

`Load` is not exposed as a RESP command today.

---

## End-to-end wiring

```text
main
  → SnapshotManager(&vnode_index, &kv, persistence_dir)
  → optional Load() if load_on_startup
  → CommandHandler(..., &snapshot)   // registers SAVE

client SAVE
  → PersistDispatcher
  → BackgroundSave() → fork → child SaveToDir()
  → parent Timer → ReapSaveChild()
```

Wire: `redis-cli SAVE` (default dir `data/`; empty `persistence.dir` disables).

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| SnapshotManager | `include/vemory/persist/SnapshotManager.h` | `src/persist/SnapshotManager.cc` |
| PersistDispatcher | `include/vemory/protocol/dispatcher/PersistDispatcher.h` | `src/protocol/dispatcher/PersistDispatcher.cc` |
