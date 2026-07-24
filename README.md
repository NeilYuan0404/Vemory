# Vemory

English | [中文](README.zh-CN.md)

RESP-speaking semantic cache server (plus string KVS). Talk to it with a RESP client (`redis-cli` works for strings; binary `VSET`/`VGET` need a library).

**v0.4.0** — early MVP. Optional multi-file RDB snapshot (`SAVE` / `persistence.dir`) plus optional protobuf AOF (`persistence.aof`, background flush). Single-threaded epoll reactor. Primary API is semantic cache (`VSET`/`VGET`/`VDEL` with binary float blobs) plus `SET`/`GET`/`DEL` / `PING`/`ECHO` / `SAVE`. Not a drop-in Redis or Redis Vector Set replacement. See [`CHANGELOG.md`](CHANGELOG.md).

## Requirements

- C++17 toolchain (`g++`)
- [Protocol Buffers](https://protobuf.dev/) (`protoc`, `libprotobuf`) — codec kept for future replication
- Vendored [usearch](https://github.com/unum-cloud/usearch) under `third_party/usearch` (already in tree; refresh with `make usearch-fetch`)
- Vendored [spdlog](https://github.com/gabime/spdlog) under `third_party/spdlog` (already in tree; refresh with `make spdlog-fetch`)

## Build & run

```bash
make              # → bin/vemory
./bin/vemory      # listen 0.0.0.0:6379
./bin/vemory 8989 # custom port
./bin/vemory -c conf/vemory.ini
./bin/vemory -c conf/vemory.ini 8989  # CLI port overrides server.port
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
| `server` | `bind` | `0.0.0.0` | IPv4 bind address |
| `logging` | `level` | `info` | `trace`/`debug`/`info`/`warn`/`error`/`critical`/`off` |
| `storage` | `kv_reserve` | `100000` | `KvStore` pre-reserve |
| `index` | `default_capacity` | `1024` | Initial vector-set capacity |
| `persistence` | `dir` | `data` | RDB snapshot directory; empty disables `SAVE` |
| `persistence` | `load_on_startup` | `false` | Load `dump.*` from `dir` on startup |
| `persistence` | `aof` | `false` | Protobuf AOF at `dir/appendonly.aof` |

Unknown sections/keys are ignored (warned). A positional port still overrides `server.port`.

Snapshot files (multi-file) under `data/` by default: `dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch`. `SAVE` forks a background writer. Optional AOF: enable `persistence.aof` (encode on the request path, flush thread writes `appendonly.aof`).

Benches (server must already be running; needs `redis-benchmark` / `redis-cli`):

```bash
./bench/smoke/kvs.sh       # PING / ECHO / SET / GET
./bench/smoke/pipeline.sh  # c=1 pipeline smoke (Vemory only)
./bench/smoke/vector.sh    # VSET load + VGET + VDEL spot-check (redis-py)
./bench/smoke/vector_rdb.sh  # VSET → SAVE → dump.usearch → VGET (needs persistence.dir)
python3 bench/pipeline_bench.py                  # c=1 SET/GET: Vemory vs Redis
bench/.venv/bin/python bench/vector_metrics.py   # agree / p50·p99 / QPS@agree≥0.95 (see bench/README.md)
HOST=127.0.0.1 PORT=8989 python3 bench/rdb_save_bench.py  # SAVE frequency vs SET QPS
python3 bench/aof_bench.py                               # AOF SET/GET vs Redis
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
(release `bin/vemory`; `c=1 P=1`, `N=100000`; Vemory no-AOF `:8989`, AOF `:8990` / `conf/vemory_aof_bench.ini`, Redis `appendonly yes` `:6379`)

ECHO (vemory_no_aof): **13509.86** rps

| mode | SET (rps) | GET (rps) |
|------|----------:|----------:|
| vemory_no_aof | 13113.03 | 12573.87 |
| vemory_aof | 8722.20 | 12828.74 |
| redis_aof | 9790.48 | 12682.31 |

Indicative only — single-threaded event loop; AOF write path differs from Redis.

Other targets:

| Target | Purpose |
|--------|---------|
| `make run` | Build and start `bin/vemory` |
| `make test` | GoogleTest unit suite (`bin/unit_tests`) |
| `make proto` | Regenerate `generated/VNode.pb.*` |
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
| Embed index / vector sets | [`docs/Index/EmbedIndex.md`](docs/Index/EmbedIndex.md) |

Layout: public headers under `include/vemory/`, sources under `src/` (including `persist/`), schema in `proto/VNode.proto` (codec for future replication).
