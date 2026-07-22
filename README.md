# Vemory

English | [中文](README.zh-CN.md)

RESP-speaking vector set server (Redis Vector Set–style subset). Talk to it with `redis-cli`.

**v0.1.1+ (toward 0.2)** — early MVP. Data is **in-memory only**. Single-threaded epoll reactor. Primary API is semantic cache (`VSET`/`VGET`/`VDEL` with binary float blobs) plus `SET`/`GET`/`DEL` / `PING`/`ECHO`. Not a drop-in Redis or Redis Vector Set replacement. See [`CHANGELOG.md`](CHANGELOG.md).

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

Unknown sections/keys are ignored (warned). A positional port still overrides `server.port`.

Benches (server must already be running; needs `redis-benchmark` / `redis-cli`):

```bash
./bench/smoke/kvs.sh       # PING / ECHO / SET / GET
./bench/smoke/pipeline.sh  # c=1 pipeline smoke (Vemory only)
./bench/smoke/vector.sh    # VADD warm-up + VSIM
python3 bench/pipeline_bench.py                  # c=1 SET/GET: Vemory vs Redis
bench/.venv/bin/python bench/vector_metrics.py   # Recall@10 / p50·p99 / QPS@recall≥0.95 (see bench/README.md)
```

### Latest pipeline result

Run: `python3 bench/pipeline_bench.py` (Vemory `127.0.0.1:8989`, Redis `127.0.0.1:6379`)

Baseline (`c=1`, `p=1`, `n=10000`):

| Server | SET (rps) | GET (rps) |
|--------|-----------|-----------|
| Vemory | 9832.84 | 10559.66 |
| Redis | 8635.58 | 9433.96 |

Pipeline sweep (`c=1`):

| P | n | Vemory SET | Redis SET | Vemory GET | Redis GET |
|---|---:|-----------:|----------:|-----------:|----------:|
| 10 | 100000 | 110741.97 | 72621.64 | 71022.73 | 74682.60 |
| 20 | 100000 | 118203.30 | 71073.21 | 112866.82 | 112359.55 |
| 40 | 1000000 | 142979.70 | 100050.02 | 111358.58 | 141023.83 |
| 100 | 1000000 | 220312.84 | 169376.70 | 197863.08 | 180929.98 |
| 160 | 1000000 | 254777.08 | 190114.06 | 215656.67 | 261096.61 |

See [`bench/README.md`](bench/README.md).

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

`vector_blob` / query blob: raw little-endian `float32` bytes. `threshold` is a cosine **distance** upper bound (hit if best distance ≤ threshold). Also: `SET`/`GET`/`DEL`, `PING`/`ECHO`.

Binary blobs are awkward in interactive `redis-cli`; prefer a RESP client library or unit tests for cache commands. String KVS still works with `redis-cli`.

Note: `bench/smoke/vector.sh` / `vector_metrics.py` still target the removed Vector Set verbs and will be updated in a follow-up commit.

## Architecture

```
client
  → TcpServer / EventLoop (epoll)
    → ProtocolExecutor + RespProtocolHandler
      → CommandHandler
        → VNodeIndex (VNodeStorage + USearchEmbedIndex)
        → KvStore
```

Design notes by layer:

| Layer | Doc |
|-------|-----|
| Network / reactor | [`docs/Network/Reactor.md`](docs/Network/Reactor.md) |
| Message buffer | [`docs/Network/MessageBuffer.md`](docs/Network/MessageBuffer.md) |
| RESP / commands | [`docs/Protocol/Protocol.md`](docs/Protocol/Protocol.md) |
| Storage | [`docs/Storage/StorageLayer.md`](docs/Storage/StorageLayer.md) |
| Embed index / vector sets | [`docs/Index/EmbedIndex.md`](docs/Index/EmbedIndex.md) |

Layout: public headers under `include/vemory/`, sources under `src/`, schema in `proto/VNode.proto` (codec for future replication).
