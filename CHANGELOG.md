# Changelog

All notable changes to Vemory are documented in this file.

## [Unreleased]

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
- AOF has no io_uring / everysec fsync yet; crash may lose queued frames and OS buffers; no AOF rewrite after SAVE
- `Append` success means enqueued, not durable on disk
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
