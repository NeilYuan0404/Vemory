# Changelog

All notable changes to Vemory are documented in this file.

## [Unreleased]

## [1.0.0] — 2026-07-27

First stable release of a RESP semantic cache server (plus string KVS), with optional persistence and master/slave replication.

### What's in 1.0
- Semantic cache: `VSET` / `VGET` / `VDEL` (client-supplied float32 embeddings)
- String KVS: `SET` / `GET` / `DEL`, plus `PING` / `ECHO` / `SAVE`
- Persistence: single-file RDB v2 (`dump.rdb`); optional protobuf AOF (startup: RDB then AOF replay when enabled)
- Replication: `--slaveof` — fullsync, live stream, partial resync, auto-reconnect, replica-readonly
- Runtime: single-threaded epoll; default vendored tcmalloc heap
- Wire / formats: RESP command channel; RDB magic `VEMORYDB` version 2; AOF/replication `u32le` + `WalEntry` frames

### New since 0.5.2
- PSYNC partial resync: `PSYNC <replid> <offset>` → `+CONTINUE` catch-up from backlog when offset retained; else `+FULLRESYNC <replid> <cut>`
- Process-lifetime master `replid` (40 hex); replication backlog always fed on mutations
- Smoke: [`bench/smoke/repl_reconnect_partial.sh`](bench/smoke/repl_reconnect_partial.sh)

### Limits
- Not a Redis / Redis Vector Set drop-in
- No AOF rewrite after `SAVE` (AOF only grows while enabled)
- No auth; bind carefully for non-local use
- Internal ANN / metadata ids are `uint16` (~65k entries)
- No server-side embedding; clients send float blobs
- Single-threaded epoll reactor
- Master restart issues a new `replid` (slaves must fullsync); backlog is process-local
- `aof_io=iouring` remains experimental; prefer `thread`

## [0.5.2] — 2026-07-27

Slave auto-reconnect, replica-readonly writes, and vendored gperftools for out-of-the-box tcmalloc.

### Added
- Replica-readonly: `--slaveof` rejects client `SET`/`DEL`/`VSET`/`VDEL`/`SAVE` with `-READONLY …` (replication stream apply unchanged)
- Slave auto-reconnect: link/`PSYNC` failure schedules exponential backoff (1s→60s) and fullsyncs again; initial master connect failure no longer exits the process
- Smoke: [`bench/smoke/repl_reconnect.sh`](bench/smoke/repl_reconnect.sh)
- Vendored gperftools (`third_party/gperftools`, `make gperftools-fetch`); default build static-links `libtcmalloc_minimal` with no system package required

### Changed
- tcmalloc no longer requires `apt install libgoogle-perftools-dev` / `PKG_CONFIG_PATH` for out-of-the-box `make`

## [0.5.1] — 2026-07-27

Default process heap via gperftools tcmalloc (build-time), plus an RSS/VIRT compare bench.

### Added
- Build links gperftools `libtcmalloc_minimal` by default for STL / protobuf / `new` (`TCMALLOC=0` to use the system allocator); usearch vector tape stays on its mmap arenas
- Bench: [`bench/tcmalloc_mem_bench.py`](bench/tcmalloc_mem_bench.py) (tcmalloc on/off SET→DEL RSS/VIRT table); results in README

### Changed
- `make clean` / `proto` / `*-fetch` / `compile-commands` skip the tcmalloc link probe so cleanup works without gperftools installed

### Notes
- New build dependency when `TCMALLOC=1` (default): Debian/Ubuntu `libgoogle-perftools-dev`, or set `PKG_CONFIG_PATH` if gperftools lives under a custom prefix (e.g. `~/.local`)

## [0.5.0] — 2026-07-26

PSYNC master/slave replication (fullsync + stream) and single-file RDB v2.

### Added
- PSYNC fullsync replication: `ReplicationMaster` / `ReplicationSlave`, CLI `--slaveof <host> <port>`
- Temp RDB under `tmp/` (`repl-fullsync.rdb` / `repl-in.rdb`) via `sendfile`; independent of `persistence.dir`
- In-memory replication backlog (WalEntry frames) for writes during the fullsync window
- Post-fullsync streaming: main-thread direct push of WalEntry frames to `kSynced` replicas (32 MiB output buffer kick)
- `SnapshotManager::BackgroundSaveToPath` / `LoadFromPath` (path API does not require configured dir)
- `TcpConn::SendFile`, `OutputBufferedBytes`, `ForceClose`; `TcpConnector::Connect`
- Docs: [`docs/Persist/Replication.md`](docs/Persist/Replication.md)
- Smoke: [`bench/smoke/repl.sh`](bench/smoke/repl.sh); demo: [`demo/04_repl.py`](demo/04_repl.py)
- Bench: [`bench/repl_bench.py`](bench/repl_bench.py) (SET/GET with synced replica)

### Changed
- RDB snapshot is a single file `dump.rdb` (Header + TOC + KV/NODES/USEARCH); magic `VEMORYDB`, version 2
- `USearchEmbedIndex` / `VNodeIndex` index persist via `FILE*` streams (`save_to_stream` / `load_from_stream`)
- Successful `SAVE` removes legacy multi-file names (`dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch`)

### Breaking
- Old multi-file RDB layouts (`dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch`) are no longer loaded; re-`SAVE` after upgrade to produce `dump.rdb`

### Limits
- Master restart issues a new `replid` (slaves must fullsync); backlog is process-local
- No AOF rewrite after SAVE (unchanged from 0.4.x)

## [0.4.1] — 2026-07-25

AOF durability controls, pluggable flush backends, and batched writes.

### Added
- AOF fsync policy: INI `persistence.aof_fsync` = `no` | `everysec` (default) | `always` (`fdatasync` after each flush batch on the writer thread)
- AOF IO backend: INI `persistence.aof_io` = `thread` (default) | `auto` | `iouring`; `AofWriter` abstraction (`ThreadAofWriter` / optional `IoUringAofWriter` via `liburing`). `iouring` is experimental — prefer `thread` for real use.

### Changed
- AOF flush thread batches up to 32 frames per write (`fwrite`+one `fflush`, or `io_uring` `writev`)
- AOF flush thread uses timed queue pop so `everysec` can sync a dirty tail while idle
- Default / bench `aof_io` is `thread` (was `auto`, which could select experimental io_uring)
- Bench config [`conf/vemory_aof_bench.ini`](conf/vemory_aof_bench.ini) uses `aof_fsync = everysec` (aligned with Redis `appendfsync everysec`)

## [0.4.0] — 2026-07-24

Optional protobuf AOF (WAL) with background flush.

### Added
- Protobuf AOF (`WalEntry`): `{persistence.dir}/appendonly.aof`, INI `persistence.aof`
- Background AOF flush: encode on caller thread, bounded `BlockingQueue`, single writer thread (`fwrite` + `fflush`)
- Startup replay after optional RDB load (`MutateSource::kAofReplay` does not re-append)
- Docs: [`docs/Persist/Aof.md`](docs/Persist/Aof.md)
- Bench: [`bench/aof_bench.py`](bench/aof_bench.py) (ECHO + SET/GET vs Redis AOF; `c=1 P=1`)
- Config: [`conf/vemory_aof_bench.ini`](conf/vemory_aof_bench.ini) for the AOF bench instance

### Limits
- No AOF rewrite after SAVE
- `Append` success means enqueued, not durable on disk (use `aof_fsync`; see 0.4.1)
- Not Redis AOF / RESP format

## [0.3.0] — 2026-07-23

Optional multi-file RDB snapshot persistence.

### Added
- Multi-file RDB snapshot: `dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch`
- `SAVE` command (fork background dump via `SnapshotManager`)
- INI `[persistence]` `dir` + `load_on_startup`
- Bench: `bench/rdb_save_bench.py` (SAVE frequency vs SET QPS)
- Smoke: `bench/smoke/vector_rdb.sh` (VSET → SAVE → dump.usearch)
- Demo: `demo/03_rdb.py` (dump / verify after restart)

### Changed
- Moved `SnapshotManager` from `storage/` to `persist/` (`include/vemory/persist/`, `src/persist/`)
- Default snapshot directory is `data/` (`persistence.dir`)

### Limits
- No WAL / AOF; crash may lose writes since last successful SAVE
- Snapshot format is Vemory-specific (not Redis RDB-compatible)

## [0.2.0] — 2026-07-22

Semantic cache as the primary wire API (breaking vs Redis Vector Set–style verbs).

### Breaking
- Removed Redis Vector Set verbs: `VADD`, `VSIM`, `VDIM`, `VEMB`, `VCARD`
- Removed `VectorSet` / `VectorSetRegistry` / `VectorDispatcher`

### Added
- Semantic cache API: `VSET` / `VGET` / `VDEL` with raw float32 blobs
- `VNodeIndex` orchestrating `VNodeStorage` + `USearchEmbedIndex`
- `VNode` fields: `user_key`, `question`, `answer` (id reused on same `user_key`)

### Changed
- Benches retargeted to binary `VSET` / `VGET` / `VDEL` (`bench/vemory_vec.py`, smoke + `vector_metrics.py` agree gate)
- Protocol mapping renamed `FromArgv` → `FromTokens` (RESP array tokens)

### Notes
- `VGET` threshold is cosine **distance** (not similarity)

### Limits
- No auth; bind carefully for non-local use
- Internal ANN / metadata ids are `uint16` (~65k entries)
- No server-side embedding; clients send float blobs
- Single global index; dimension locked on first successful `VSET`

## [0.1.1] — 2026-07-22

Protocol / network path hardening and layout cleanup after pipeline smoke benches.

### Changed
- Merged `RespHandler` into `RespProtocolHandler` (single RESP parse entry)
- Moved command dispatch under `protocol/dispatcher/`; moved `VNode` to `storage/`
- Clarified reactor / protocol docs for the sticky-packet + pipeline batch write path

### Added
- Bench: `bench/smoke/` scripts, `pipeline_bench.py` (Vemory vs Redis), `vector_metrics.py` (Recall@10 / latency / gated QPS)
- Bilingual README (`README.zh-CN.md`)

### Limits
- Same as 0.1.0 (no persistence, no auth, `uint16` element ids, partial Vector Set API)
- `VNode` / `VNodeStorage` / `ProtobufVNodeCodec` remain unwired to live commands

## [0.1.0] — 2026-07-19

First public MVP tag.

### Added
- RESP server over single-threaded epoll (`TcpServer` / `EventLoop`)
- Vector set commands: `VADD`, `VSIM`, `VDIM`, `VEMB`, `VCARD` (USearch-backed cosine ANN)
- String KVS: `SET`, `GET`, `DEL`
- Assist: `PING`, `ECHO`
- Optional INI config via `-c` (`conf/vemory.ini`): port, bind, log level, `kv_reserve`, `default_capacity`
- Unit tests (`make test`), bench scripts under `bench/`

### Limits
- No persistence / WAL — process exit clears all data
- No auth; bind carefully for non-local use
- Per-set element ids are `uint16` (~65k elements per key)
- Partial Redis Vector Set API (no `VREM`, filters, attrs, etc.)
