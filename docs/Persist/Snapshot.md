# Persist Layer

Owns multi-file RDB snapshots (`SnapshotManager`). Depends on the storage layer for dump/load of in-memory state; does not own hot-path Put/Get.

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

Under `dir`:

| File | Content |
|------|---------|
| `dump.meta` | magic `VEMORYSN`, version, dim, next_id, kv_count, node_count (**commit point**: renamed last) |
| `dump.kv` | KvStore binary (`KvStore::Dump` / `Load`) |
| `dump.nodes` | length-prefixed `VNodePb` via `ProtobufVNodeCodec` (no vectors) |
| `dump.usearch` | usearch native file (`USearchEmbedIndex::Save` / `Load`); omitted when `dim == 0` |

Write path: `*.tmp` → `fflush`/`fsync` → `rename`. Payload files first, **`dump.meta` last**. Load requires a readable `dump.meta`, then the other files.

Format is Vemory-specific (not Redis RDB-compatible). Optional protobuf AOF: [`Aof.md`](Aof.md) (`persistence.aof`).

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

### `SaveToDir()` — synchronous dump

Writes a full snapshot on the **calling thread** (blocks until done).

Typical callers:

- Child process after `fork` inside `BackgroundSave`
- Unit tests / tools that need a deterministic dump without fork

Returns `kNotConfigured` if `dir` empty or store pointers null; `kIoError` / `kError` on failure; `kOk` on success.

Creates `dir` if missing (`create_directories`).

### `BackgroundSave()` — fork (wire `SAVE`)

```text
BackgroundSave()
  → if dir empty → kNotConfigured
  → if child already running → kInProgress
  → fork()
       parent: record pid, schedule Timer poll, return kOk  (+OK to client)
       child:  SaveToDir(); _exit(0|1)
```

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

1. Read/validate `dump.meta`
2. `KvStore::Clear` + `Load` from `dump.kv` (check `kv_count`)
3. `VNodeIndex::Clear` + `LoadNodes` from `dump.nodes` (restore ids / `next_id`)
4. If `dim > 0`, `LoadIndex` from `dump.usearch`

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
