# Vemory

RESP-speaking vector set server (Redis Vector Set–style subset). Talk to it with `redis-cli`.

## Requirements

- C++17 toolchain (`g++`)
- [Protocol Buffers](https://protobuf.dev/) (`protoc`, `libprotobuf`) — codec kept for future replication
- Vendored [usearch](https://github.com/unum-cloud/usearch) under `third_party/usearch` (already in tree; refresh with `make usearch-fetch`)
- Vendored [spdlog](https://github.com/gabime/spdlog) under `third_party/spdlog` (already in tree; refresh with `make spdlog-fetch`)

## Build & run

```bash
make              # → bin/vemory
./bin/vemory      # listen 0.0.0.0:8989
./bin/vemory 6379 # custom port
```

```bash
redis-cli -p 8989         # Vemory default port
redis-cli -p 6379
```

Benches (server must already be running; needs `redis-benchmark` / `redis-cli`):

```bash
./bench/kvs.sh       # PING / ECHO / SET / GET
./bench/pipeline.sh  # c=1 pipeline sweep vs Redis (SET / GET)
./bench/vector.sh    # VADD warm-up + VSIM
```

## Latest pipeline result

Run: `./bench/pipeline.sh` (Vemory `127.0.0.1:8989`, Redis `127.0.0.1:6379`)

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

Entry point: [`src/Vemory.cc`](src/Vemory.cc). Dimension is set per key on the first `VADD`.

## Commands

Wire format is Redis RESP. Subset of Redis Vector Set verbs:

| Command | Args | Reply |
|---------|------|-------|
| `VADD` | `<key> VALUES <dim> <f1> … <fN> <element>` | integer `1` |
| `VSIM` | `<key> ELE <element> [COUNT <n>] [WITHSCORES]` or `<key> VALUES <dim> <f1>…<fN> [COUNT <n>] [WITHSCORES]` | array of elements (or element/score pairs) |
| `VDIM` | `<key>` | integer dim |
| `VEMB` | `<key> <element>` | array of float strings |
| `VCARD` | `<key>` | integer cardinality (`0` if key missing) |

`COUNT` defaults to **10**. Scores are cosine similarity (`1 - distance`).

Examples:

```bash
redis-cli VADD docs VALUES 8 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 apple
redis-cli VSIM docs VALUES 8 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 COUNT 3 WITHSCORES
redis-cli VSIM docs ELE apple COUNT 3
redis-cli VDIM docs
redis-cli VEMB docs apple
redis-cli VCARD docs
```

## Architecture

```
redis-cli
  → TcpServer / EventLoop (epoll)
    → ProtocolExecutor + RespProtocolHandler
      → CommandHandler
        → VectorSetRegistry
          → VectorSet (name↔id, vectors, USearchEmbedIndex)
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
