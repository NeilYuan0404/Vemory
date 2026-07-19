# Vemory benches

Requires a running server (`./bin/vemory` or `./bin/vemory <port>`) and Redis CLI tools (`redis-benchmark`, `redis-cli`).

Default: `HOST=127.0.0.1`, `PORT=6379`.

Smoke scripts live under [`smoke/`](smoke/). Compare / quality benches: [`pipeline_bench.py`](pipeline_bench.py), [`vector_metrics.py`](vector_metrics.py).

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

## Smoke — Pipeline (`smoke/pipeline.sh`)

**Vemory-only** SET/GET pipeline smoke via `redis-benchmark` (`c=1`). No Redis side-by-side.

- Pipeline depths **10 / 20 / 40 / 100 / 160**
- Prints `p=1` baseline, then a sweep table
- Request count: **p=1 → 10k**, **p=10/20 → 100k**, **p=40/100/160 → 1M**

For Vemory vs Redis compare, use [`pipeline_bench.py`](pipeline_bench.py).

```bash
./bench/smoke/pipeline.sh
PORT=8989 ./bench/smoke/pipeline.sh
PIPELINES="10 40 160" PORT=8989 ./bench/smoke/pipeline.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | `127.0.0.1` / `6379` | Vemory |
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

## Pipeline compare (`pipeline_bench.py`)

Side-by-side **Vemory vs Redis** SET/GET pipeline sweep (`c=1`), still driven by `redis-benchmark --csv`. Same `n` / `PIPELINES` scaling as the smoke script.

Both servers must already be running.

```bash
python3 bench/pipeline_bench.py
VEMORY_PORT=8989 REDIS_PORT=6379 python3 bench/pipeline_bench.py
PIPELINES="10 40 160" python3 bench/pipeline_bench.py
```

| Env | Default | Meaning |
|-----|---------|---------|
| `VEMORY_HOST` / `VEMORY_PORT` | `127.0.0.1` / `8989` | Vemory |
| `REDIS_HOST` / `REDIS_PORT` | `127.0.0.1` / `6379` | Redis |
| `N_P1` / `N_MID` / `N_HIGH` | `10000` / `100000` / `1000000` | Request counts by `P` |
| `R` | `10000` | Random keyspace (`-r`) |
| `D` | `64` | SET value size (`-d`) |
| `PIPELINES` | `10 20 40 100 160` | Pipeline depths (`-P`) |

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

## Sample results (local, 2026-07-19)

WSL2, debug `bin/vemory` on `:8989`, single event loop. Numbers are indicative only.

### Smoke — KVS (`N=10000 C=10 P=8`)

| Command | Load RPS (approx.) | Notes |
|---------|-------------------:|-------|
| PING | ~114k | also baseline ~1.9k rps at `c=1 p=1` |
| ECHO | ~125k | |
| SET | ~115k | |
| GET | ~125k | |

### Smoke — Vector (`DIM=8 CARD=100 QUERIES=50 COUNT=10`)

| Phase | Result |
|-------|--------|
| VADD | 100 in ~3.9 s (~26 ops/s; one `redis-cli` per command) |
| VSIM ELE | ~117 qps |
| VSIM VALUES | exit 0 (wall time dominated by `redis-cli` argv overhead) |

### Vector metrics (`glove-25-angular`, defaults)

| Metric | Value |
|--------|------:|
| card / queries | 10000 / 200 |
| recall@10 | 0.9905 |
| latency_p50_ms | 1.58 |
| latency_p99_ms | 2.68 |
| qps@recall≥0.95 | 604.6 |

Recall is cosine exact-top-10 on the **loaded subset**, not the full ANN-Benchmarks train set. Gate passed (exit 0).

## Notes

- `KvStore` is `unordered_map`; load gaps vs Redis are mostly single-threaded I/O, not linear scan.
- Prefer `redis-benchmark … PING` / `ECHO` over `-t ping`; avoid `redis-cli --pipe` (inline/HELLO quirks).
- After changing `CommandType`, rebuild with `make clean && make` if handlers look stale.
- HDF5 download needs a normal `User-Agent` (script sets one; falls back to `vectors.erikbern.com` if needed).
