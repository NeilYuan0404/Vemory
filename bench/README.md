# Vemory benches

Requires a running server (`./bin/vemory` or `./bin/vemory <port>`) and Redis CLI tools (`redis-benchmark`, `redis-cli`).

Default: `HOST=127.0.0.1`, `PORT=6379`.

## KVS / protocol (`kvs.sh`)

- `PING` / `ECHO` / `SET` / `GET` via `redis-benchmark` (RESP).
- Use **command form** (`redis-benchmark … PING`, `… ECHO hello`), not `-t ping` — that also runs `PING_INLINE` (non-RESP), which Vemory rejects.
- `redis-cli` is only used for a one-shot connectivity check.

```bash
./bench/kvs.sh
PORT=8989 ./bench/kvs.sh
N=200000 C=50 P=16 R=10000 D=64 ./bench/kvs.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` | `127.0.0.1` | Server host |
| `PORT` | `6379` | Server port |
| `N` | `100000` | Requests per test |
| `C` | `50` | Clients |
| `P` | `16` | Pipeline depth |
| `R` | `10000` | Random keyspace (`-r`) |
| `D` | `64` | SET value size (bytes) |
| `ECHO_MSG` | `hello` | Payload for `ECHO` |

Also prints single-client, no-pipeline baselines for PING/ECHO and SET/GET.

## Pipeline vs Redis (`pipeline.sh`)

- Clients fixed at **c=1**; pipeline depths **10 / 20 / 40 / 100 / 160**.
- Compares **Vemory** and **Redis** side by side (**SET + GET**), all via `redis-benchmark`.
- Prints SET/GET **p=1** baselines first (`n=10000`), then a sweep table of RPS.
- Request count scales with pipeline depth: **p=1 → 10k**, **p=10/20 → 100k**, **p=40/100/160 → 1M**.

Both servers must already be running.

```bash
./bench/pipeline.sh
VEMORY_PORT=8989 REDIS_PORT=6379 ./bench/pipeline.sh
PIPELINES="10 40 160" ./bench/pipeline.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `VEMORY_HOST` / `VEMORY_PORT` | `127.0.0.1` / `6379` | Vemory |
| `REDIS_HOST` / `REDIS_PORT` | `127.0.0.1` / `6380` | Redis |
| `N_P1` | `10000` | Requests when `P=1` (baselines) |
| `N_MID` | `100000` | Requests when `P=10` or `20` |
| `N_HIGH` | `1000000` | Requests when `P=40`, `100`, or `160` |
| `R` | `10000` | Random keyspace (`-r`) |
| `D` | `64` | SET value size in bytes (`-d`) |
| `PIPELINES` | `10 20 40 100 160` | Pipeline depths (`-P`) |

## Vector (`vector.sh`)

Warm-up `VADD`, then timed `VSIM` (ELE and VALUES). Uses `redis-cli -2` (not `redis-benchmark`). One client process per command (fine for microchecks; lower `CARD` / `QUERIES` for a quick run).

```bash
./bench/vector.sh
PORT=8989 DIM=8 CARD=100 QUERIES=50 ./bench/vector.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | as above | Server |
| `KEY` | `bench` | Vector set name |
| `DIM` | `8` | Embedding dimension |
| `CARD` | `1000` | Elements to insert |
| `QUERIES` | `500` | VSIM queries per mode |
| `COUNT` | `10` | VSIM top-k |

Reports wall time and QPS for load + each query mode. Single-threaded client; server is one event loop.

## Notes

- `KvStore` is `unordered_map`; load gaps vs Redis are mostly single-threaded I/O, not linear scan.
- Prefer `redis-benchmark … PING` / `ECHO` over `-t ping`; avoid `redis-cli --pipe` (inline/HELLO quirks).
- After changing `CommandType`, rebuild with `make clean && make` if handlers look stale.
