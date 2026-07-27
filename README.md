# Vemory

[![Release](https://img.shields.io/github/v/release/NeilYuan0404/Vemory)](https://github.com/NeilYuan0404/Vemory/releases/latest)
[![CI](https://github.com/NeilYuan0404/Vemory/actions/workflows/ci.yml/badge.svg)](https://github.com/NeilYuan0404/Vemory/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/NeilYuan0404/Vemory)](LICENSE)

English | [中文](README.zh-CN.md)

RESP semantic cache server (plus string KVS). Talk to it with a RESP client (`redis-cli` for strings; binary `VSET`/`VGET` need a library).

**v1.0.0** — optional RDB/AOF persistence and PSYNC replication (`--slaveof`). Primary API: `VSET`/`VGET`/`VDEL` plus `SET`/`GET`/`DEL` / `PING`/`ECHO` / `SAVE`. Not a Redis drop-in. Stable surface and Limits: [`CHANGELOG.md`](CHANGELOG.md).

## Requirements

- C++17 toolchain (`g++`)
- [Protocol Buffers](https://protobuf.dev/) (`protoc`, `libprotobuf`) — AOF / replication `WalEntry` frames
- Vendored [gperftools](https://github.com/gperftools/gperftools) under `third_party/gperftools` — default global heap (`libtcmalloc_minimal`, static-linked on `make`); refresh with `make gperftools-fetch`. Usearch still uses its own `mmap` arenas. Disable with `TCMALLOC=0`.
- Vendored [usearch](https://github.com/unum-cloud/usearch) under `third_party/usearch` (already in tree; refresh with `make usearch-fetch`)
- Vendored [spdlog](https://github.com/gabime/spdlog) under `third_party/spdlog` (already in tree; refresh with `make spdlog-fetch`)

## Build & run

```bash
make              # → bin/vemory (builds vendored tcmalloc if needed, then links it)
make TCMALLOC=0   # system allocator instead of tcmalloc
./bin/vemory      # listen 0.0.0.0:6379 (master)
./bin/vemory 8989 # custom port
./bin/vemory -c conf/vemory.ini
./bin/vemory -c conf/vemory.ini 8989  # CLI port overrides server.port
./bin/vemory --slaveof 127.0.0.1 6379 6380  # replica: PSYNC fullsync + stream (client writes → READONLY)
```

```bash
redis-cli                 # default port 6379
redis-cli -p 8989
```

### Configuration (INI)

Optional file via `-c` (see [`conf/vemory.ini`](conf/vemory.ini)). Without `-c`, built-in defaults apply.

| Section | Key | Default | Meaning |
|---------|-----|---------|---------|
| `server` | `port` | `6379` | Listen port |
| `server` | `bind` | `0.0.0.0` | IPv4 bind address (no auth — do not expose publicly without a firewall) |
| `logging` | `level` | `info` | `trace`/`debug`/`info`/`warn`/`error`/`critical`/`off` |
| `storage` | `kv_reserve` | `100000` | `KvStore` pre-reserve |
| `index` | `default_capacity` | `1024` | Initial semantic-cache index capacity |
| `persistence` | `dir` | `data` | RDB snapshot directory; empty disables `SAVE` |
| `persistence` | `load_on_startup` | `false` | Load `dump.rdb` from `dir` on startup |
| `persistence` | `aof` | `false` | Protobuf AOF at `dir/appendonly.aof` |
| `persistence` | `aof_fsync` | `everysec` | `no` / `everysec` / `always` (`fdatasync`) |
| `persistence` | `aof_io` | `thread` | `auto` / `thread` / `iouring` (flush backend; prefer `thread` — `iouring` is experimental only) |

Unknown sections/keys are ignored (warned). A positional port still overrides `server.port`.

Snapshot file under `data/dump.rdb` by default (Header + TOC + KV/NODES/USEARCH). `SAVE` forks a background writer. Optional AOF: enable `persistence.aof` (encode on the request path, flush thread writes `appendonly.aof`; `aof_fsync` controls durability).

Benches (server must already be running; needs `redis-benchmark` / `redis-cli`):

```bash
./bench/smoke/kvs.sh       # PING / ECHO / SET / GET
./bench/smoke/pipeline.sh  # c=1 pipeline smoke (Vemory only)
./bench/smoke/vector.sh    # VSET load + VGET + VDEL spot-check (redis-py)
./bench/smoke/vector_rdb.sh  # VSET → SAVE → dump.rdb → VGET (needs persistence.dir)
AUTO_SLAVE=1 ./bench/smoke/repl.sh               # PSYNC fullsync + stream (master already up)
python3 bench/pipeline_bench.py                  # c=1 SET/GET: Vemory vs Redis
bench/.venv/bin/python bench/vector_metrics.py   # agree / p50·p99 / QPS@agree≥0.95 (see bench/README.md)
HOST=127.0.0.1 PORT=8989 python3 bench/rdb_save_bench.py  # SAVE frequency vs SET QPS
python3 bench/aof_bench.py                               # AOF SET/GET vs Redis
AUTO_SLAVE=1 python3 bench/repl_bench.py                 # replication SET/GET (sequential no-repl then synced slave)
bench/.venv/bin/python bench/tcmalloc_mem_bench.py       # tcmalloc on/off RSS/VIRT (self-starts servers)
```

### Latest pipeline result

Run: `python3 bench/pipeline_bench.py` (Vemory `127.0.0.1:8989`, Redis `127.0.0.1:6379`)

Baseline (`c=1`, `p=1`, `n=10000`; release `bin/vemory`):

| Server | SET (rps) | GET (rps) |
|--------|-----------|-----------|
| Vemory | 13531.80 | 13404.83 |
| Redis | 12437.81 | 13531.80 |

Pipeline sweep (`c=1`):

| P | n | Vemory SET | Redis SET | Vemory GET | Redis GET |
|---|---:|-----------:|----------:|-----------:|----------:|
| 10 | 100000 | 105820.11 | 87719.30 | 89445.44 | 96339.12 |
| 20 | 100000 | 165289.25 | 130208.34 | 146842.88 | 152905.20 |
| 40 | 5000000 | 225641.95 | 147999.05 | 194552.52 | 198720.25 |
| 100 | 5000000 | 206568.89 | 166284.22 | 179649.31 | 184352.19 |
| 160 | 5000000 | 219934.91 | 170160.62 | 201126.30 | 200980.78 |

See [`bench/README.md`](bench/README.md).

### Latest vector metrics

Run: `HOST=127.0.0.1 PORT=8989 bench/.venv/bin/python bench/vector_metrics.py`  
(debug `bin/vemory` on `:8989`, single client connection; `glove-25-angular` subset)

| Metric | Value |
|--------|------:|
| CARD / QUERIES / THRESHOLD | 10000 / 200 / 0.2 |
| dim | 25 |
| agree | 1.0000 |
| latency p50 / p99 | 1.83 ms / 2.81 ms |
| QPS@agree≥0.95 | 536.2 |
| VSET load | 329.4 ops/s |

Indicative only — single-threaded event loop, not a saturated multi-client load test.

### Latest RDB SAVE vs SET QPS

Run: `HOST=127.0.0.1 PORT=8989 python3 bench/rdb_save_bench.py`  
(release `bin/vemory` on `:8989`; `CLIENT=benchmark`, `N=1000000`, `SAVE_BUSY=skip`)

| interval | saves_ok | saves_skipped | elapsed_s | set_qps |
|----------|---------:|--------------:|----------:|--------:|
| baseline | 0 | 0 | 74.773 | 13373.8 |
| 1000000 | 1 | 0 | 74.965 | 13339.6 |
| 100000 | 10 | 0 | 75.568 | 13233.1 |
| 10000 | 100 | 0 | 79.685 | 12549.5 |
| 1000 | 984 | 16 | 111.473 | 8970.8 |

SET via `redis-benchmark` (`c=1 p=1`); SAVE via `redis-cli` between chunks. Indicative only.

### Latest AOF QPS

Run: `python3 bench/aof_bench.py`  
(release `bin/vemory` `-O2 -DNDEBUG`; `c=1 P=1`, `N=100000`; Vemory no-AOF `:8989`, AOF `:8990` / `aof_fsync=everysec` / `aof_io=thread`, Redis `appendonly yes` + `appendfsync everysec` `:6379`)

ECHO (vemory_no_aof): **13989.93** rps

| mode | SET (rps) | GET (rps) |
|------|----------:|----------:|
| vemory_no_aof | 13356.48 | 13002.21 |
| vemory_aof | 10582.01 | 12835.32 |
| redis_aof | 9984.03 | 12083.13 |

Indicative only — single-threaded event loop; AOF write path differs from Redis. Both AOF sides use everysec fsync. Numbers above use `aof_io=thread` (the production path). `aof_io=iouring` exists for experimentation only and is **not recommended** for real use yet (`auto` may pick it when liburing is available).

### Latest replication QPS

Run: `AUTO_SLAVE=1 python3 bench/repl_bench.py`  
(release `bin/vemory` `-O2 -DNDEBUG`; `c=1 P=1`, `N=100000`; sequential on one master `:8989` — no-repl first, then synced slave `:8992`)

ECHO (vemory_no_repl): **13698.63** rps

| mode | SET (rps) | GET (rps) |
|------|----------:|----------:|
| vemory_no_repl | 13294.34 | 12980.27 |
| vemory_repl | 9903.93 | 13140.60 |

Indicative only — single-threaded event loop; with a synced replica, writes encode + main-thread `Send` to the slave. Sequential phases avoid concurrent topologies contending for CPU.

### Latest tcmalloc memory compare

Run: `bench/.venv/bin/python bench/tcmalloc_mem_bench.py`  
(release builds; `N=300000`, `D=64`; string `SET` then `DEL`; samples `/proc` RSS/VIRT; usearch not in path)

| Mode | Initial RSS (MB) | Initial VIRT (MB) | Peak RSS (MB) | Peak VIRT (MB) | Final RSS (MB) | Final VIRT (MB) |
|------|-----------------:|------------------:|--------------:|---------------:|---------------:|----------------:|
| tcmalloc on | 29.07 | 39.22 | 75.47 | 85.22 | 75.47 | 85.22 |
| tcmalloc off | 24.18 | 28.62 | 74.77 | 79.02 | 74.77 | 79.02 |

Peak RSS is similar with or without tcmalloc. Final RSS staying near the peak after `DEL` is expected (allocator high-water retention + `unordered_map` bucket capacity); it is not by itself a leak. See [`bench/README.md`](bench/README.md).

Other targets:

| Target | Purpose |
|--------|---------|
| `make run` | Build and start `bin/vemory` |
| `make test` | GoogleTest unit suite (`bin/unit_tests`) |
| `make proto` | Regenerate `generated/VNode.pb.*` |
| `make gperftools-fetch` | Re-vendor gperftools source into `third_party/gperftools` |
| `make gperftools-clean` | Remove gperftools build/`prefix` (source kept) |
| `make compile-commands` | Refresh `compile_commands.json` for clangd |
| `make clean` | Remove `build/`, `bin/`, `generated/` |

Entry point: [`src/Vemory.cc`](src/Vemory.cc). Index dimension is locked on the first successful `VSET` (`dim = vector_blob_bytes / sizeof(float)`).

## Commands

Wire format is Redis RESP (bulk strings are binary-safe). Semantic cache verbs:

| Command | Args | Reply |
|---------|------|-------|
| `VSET` | `<vector_blob> <user_key> <question> <answer>` | `+OK` or `-ERR …` |
| `VGET` | `<query_vector_blob> <threshold>` | bulk `answer`, or null bulk on miss |
| `VDEL` | `<user_key>` | `:1` / `:0` |

`vector_blob` / query blob: raw little-endian `float32` bytes. `threshold` is a cosine **distance** upper bound (hit if best distance ≤ threshold). Also: `SET`/`GET`/`DEL`, `PING`/`ECHO`, `SAVE` (writes under `data/` by default).

Binary blobs are awkward in interactive `redis-cli`; prefer a RESP client library, benches (`bench/smoke/vector.sh`, `bench/smoke/vector_rdb.sh`, `vector_metrics.py`), or unit tests for cache commands. String KVS still works with `redis-cli`.

## Architecture

```
client
  → TcpServer / EventLoop (epoll)
    → ProtocolExecutor + RespProtocolHandler
      → CommandHandler
        → VNodeIndex (VNodeStorage + USearchEmbedIndex)
        → KvStore
        → SnapshotManager
```

Design notes by layer:

| Layer | Doc |
|-------|-----|
| Network / reactor | [`docs/Network/Reactor.md`](docs/Network/Reactor.md) |
| Message buffer | [`docs/Network/MessageBuffer.md`](docs/Network/MessageBuffer.md) |
| RESP / commands | [`docs/Protocol/Protocol.md`](docs/Protocol/Protocol.md) |
| Storage | [`docs/Storage/StorageLayer.md`](docs/Storage/StorageLayer.md) |
| Persist / RDB | [`docs/Persist/Snapshot.md`](docs/Persist/Snapshot.md) |
| Persist / AOF | [`docs/Persist/Aof.md`](docs/Persist/Aof.md) |
| Replication / PSYNC | [`docs/Persist/Replication.md`](docs/Persist/Replication.md) |
| Embed index / semantic cache ANN | [`docs/Index/EmbedIndex.md`](docs/Index/EmbedIndex.md) |

Layout: public headers under `include/vemory/`, sources under `src/` (including `persist/` and `replication/`), schemas in `proto/` (`VNode.proto`, `WalEntry.proto`).
