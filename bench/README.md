# Vemory benches

Requires a running server (`./bin/vemory` or `./bin/vemory <port>`) and Redis CLI tools (`redis-benchmark`, `redis-cli`).

Default: `HOST=127.0.0.1`, `PORT=6379`.

Smoke scripts live under [`smoke/`](smoke/). Quality metrics: [`vector_metrics.py`](vector_metrics.py).

## Smoke — KVS / protocol (`smoke/kvs.sh`)

- `PING` / `ECHO` / `SET` / `GET` via `redis-benchmark` (RESP).
- Use **command form** (`redis-benchmark … PING`, `… ECHO hello`), not `-t ping` — that also runs `PING_INLINE` (non-RESP), which Vemory rejects.
- `redis-cli` is only used for a one-shot connectivity check.

```bash
./bench/smoke/kvs.sh
PORT=8989 ./bench/smoke/kvs.sh
N=200000 C=50 P=16 R=10000 D=64 ./bench/smoke/kvs.sh
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

## Smoke — Pipeline vs Redis (`smoke/pipeline.sh`)

- Clients fixed at **c=1**; pipeline depths **10 / 20 / 40 / 100 / 160**.
- Compares **Vemory** and **Redis** side by side (**SET + GET**), all via `redis-benchmark`.
- Prints SET/GET **p=1** baselines first (`n=10000`), then a sweep table of RPS.
- Request count scales with pipeline depth: **p=1 → 10k**, **p=10/20 → 100k**, **p=40/100/160 → 1M**.

Both servers must already be running.

```bash
./bench/smoke/pipeline.sh
VEMORY_PORT=8989 REDIS_PORT=6379 ./bench/smoke/pipeline.sh
PIPELINES="10 40 160" ./bench/smoke/pipeline.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `VEMORY_HOST` / `VEMORY_PORT` | `127.0.0.1` / `8989` | Vemory |
| `REDIS_HOST` / `REDIS_PORT` | `127.0.0.1` / `6379` | Redis |
| `N_P1` | `10000` | Requests when `P=1` (baselines) |
| `N_MID` | `100000` | Requests when `P=10` or `20` |
| `N_HIGH` | `1000000` | Requests when `P=40`, `100`, or `160` |
| `R` | `10000` | Random keyspace (`-r`) |
| `D` | `64` | SET value size in bytes (`-d`) |
| `PIPELINES` | `10 20 40 100 160` | Pipeline depths (`-P`) |

## Smoke — Vector (`smoke/vector.sh`)

Warm-up `VADD`, then timed `VSIM` (ELE and VALUES). Uses `redis-cli -2` (not `redis-benchmark`). One client process per command (fine for microchecks; lower `CARD` / `QUERIES` for a quick run).

```bash
./bench/smoke/vector.sh
PORT=8989 DIM=8 CARD=100 QUERIES=50 ./bench/smoke/vector.sh
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

## Vector metrics (`vector_metrics.py`)

Industry-style metrics on a running server, **single Redis connection**:

1. **Recall@10** (or `COUNT`) vs cosine brute-force ground truth on the loaded subset  
2. **p50 / p99** `VSIM` latency  
3. **QPS@recall≥0.95** — reported only if recall meets the gate; otherwise `N/A` and exit `1`

Uses an [ANN-Benchmarks](https://github.com/erikbern/ann-benchmarks) HDF5 dataset (default `glove-25-angular`). First run downloads ~121MB into `bench/data/` (gitignored). Only a **subset** is loaded (`CARD` / `QUERIES`); HDF5 `neighbors` are ignored and ground truth is recomputed with **cosine** on that subset (aligned with Vemory’s metric; not comparable to published full-train leaderboard numbers).

Keep [`smoke/vector.sh`](smoke/vector.sh) for quick redis-cli smoke; this script is for quality + latency.

```bash
python3 -m venv bench/.venv
bench/.venv/bin/pip install -r bench/requirements.txt
./bin/vemory 8989   # separate terminal
HOST=127.0.0.1 PORT=8989 bench/.venv/bin/python bench/vector_metrics.py
CARD=2000 QUERIES=50 PORT=8989 bench/.venv/bin/python bench/vector_metrics.py   # quicker
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | `127.0.0.1` / `6379` | Server |
| `DATASET` | `glove-25-angular` | HDF5 name under `http://ann-benchmarks.com/{name}.hdf5` |
| `DATA_DIR` | `bench/data` | Local cache directory |
| `CARD` | `10000` | Train vectors to `VADD` (max `65534`) |
| `QUERIES` | `200` | Timed `VSIM` queries |
| `COUNT` | `10` | Top-k for recall and `VSIM` |

## Notes

- `KvStore` is `unordered_map`; load gaps vs Redis are mostly single-threaded I/O, not linear scan.
- Prefer `redis-benchmark … PING` / `ECHO` over `-t ping`; avoid `redis-cli --pipe` (inline/HELLO quirks).
- After changing `CommandType`, rebuild with `make clean && make` if handlers look stale.
