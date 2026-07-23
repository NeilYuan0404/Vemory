# Vemory benches

Requires a running server (`./bin/vemory` or `./bin/vemory <port>`).

Default: `HOST=127.0.0.1`, `PORT=6379`.

Smoke scripts live under [`smoke/`](smoke/). Compare / quality benches: [`pipeline_bench.py`](pipeline_bench.py), [`vector_metrics.py`](vector_metrics.py), [`rdb_save_bench.py`](rdb_save_bench.py).

Semantic-cache vector benches use **Python + redis-py** (raw float32 blobs). Prefer `bench/.venv` after `pip install -r bench/requirements.txt`.

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
- Request count: **p=1 → 10k**, **p=10/20 → 100k**, **p=40/100/160 → 5M**

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
| `N_HIGH` | `5000000` | Requests when `P=40`, `100`, or `160` |
| `R` | `10000` | Random keyspace (`-r`) |
| `D` | `64` | SET value size in bytes (`-d`) |
| `PIPELINES` | `10 20 40 100 160` | Pipeline depths (`-P`) |

## Smoke — Vector / semantic cache (`smoke/vector.sh`)

Thin wrapper around [`smoke/vector.py`](smoke/vector.py): timed `VSET` load, timed `VGET` queries, `VDEL` spot-check. Uses redis-py binary blobs (not `redis-cli`).

```bash
./bench/smoke/vector.sh
PORT=8989 DIM=8 CARD=100 QUERIES=50 ./bench/smoke/vector.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | as above | Server |
| `DIM` | `8` | Embedding dimension |
| `CARD` | `1000` | Vectors to `VSET` |
| `QUERIES` | `500` | Timed `VGET` queries |
| `THRESHOLD` | `0.2` | Cosine **distance** gate for `VGET` |

Reports wall time and QPS for load + query, then checks `VDEL`. Single-threaded client; server is one event loop. Needs `redis` (`bench/.venv` preferred).

## Smoke — Vector RDB SAVE (`smoke/vector_rdb.sh`)

`VSET` a small card → `VGET` → `SAVE` → wait for `dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch` under `DUMP_DIR` → `VGET` again. Confirms vector snapshot files are written (especially non-empty `dump.usearch`).

Server must already be running with `persistence.dir` matching `DUMP_DIR` (default: repo `data/`). Does **not** restart / `load_on_startup`; that path is [`demo/03_rdb.py`](../demo/03_rdb.py).

```bash
./bin/vemory -c conf/vemory.ini          # persistence.dir=data
./bench/smoke/vector_rdb.sh
PORT=8989 DUMP_DIR=/path/to/data ./bench/smoke/vector_rdb.sh
CARD=16 DIM=8 PORT=8989 ./bench/smoke/vector_rdb.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | as above | Server |
| `DIM` | `8` | Embedding dimension |
| `CARD` | `8` | Vectors to `VSET` |
| `THRESHOLD` | `0.2` | Cosine **distance** gate for `VGET` |
| `DUMP_DIR` | `<repo>/data` | Must match server `persistence.dir` |
| `SAVE_TIMEOUT_S` | `10` | Max wait for dump files after `SAVE` |

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
| `N_P1` / `N_MID` / `N_HIGH` | `10000` / `100000` / `5000000` | Request counts by `P` |
| `R` | `10000` | Random keyspace (`-r`) |
| `D` | `64` | SET value size (`-d`) |
| `PIPELINES` | `10 20 40 100 160` | Pipeline depths (`-P`) |

## Vector metrics (`vector_metrics.py`)

Industry-style metrics on a running server, **single Redis connection**:

1. **agree** — fraction of queries where `VGET` matches the exact top-1 cosine oracle under `THRESHOLD` (hit → expected answer `e{i}`; miss → null)
2. **p50 / p99** `VGET` latency
3. **QPS@agree≥0.95** — reported only if agreement meets the gate; otherwise `N/A` and exit `1`

Uses an [ANN-Benchmarks](https://github.com/erikbern/ann-benchmarks) HDF5 dataset (default `glove-25-angular`). First run downloads ~121MB into `bench/data/` (gitignored). Only a **subset** is loaded (`CARD` / `QUERIES`); HDF5 `neighbors` are ignored and ground truth is recomputed with **cosine** on that subset (aligned with Vemory’s metric; not comparable to published full-train leaderboard numbers).

Keep [`smoke/vector.sh`](smoke/vector.sh) for quick smoke; this script is for quality + latency.

```bash
python3 -m venv bench/.venv
bench/.venv/bin/pip install -r bench/requirements.txt
./bin/vemory 8989   # separate terminal
HOST=127.0.0.1 PORT=8989 bench/.venv/bin/python bench/vector_metrics.py
CARD=2000 QUERIES=50 PORT=8989 THRESHOLD=0.2 bench/.venv/bin/python bench/vector_metrics.py   # quicker
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | `127.0.0.1` / `6379` | Server |
| `DATASET` | `glove-25-angular` | HDF5 name under `http://ann-benchmarks.com/{name}.hdf5` |
| `DATA_DIR` | `bench/data` | Local cache directory |
| `CARD` | `10000` | Train vectors to `VSET` (max `65534`) |
| `QUERIES` | `200` | Timed `VGET` queries |
| `THRESHOLD` | `0.2` | Cosine **distance** upper bound |

Shared helpers: [`vemory_vec.py`](vemory_vec.py).

## RDB SAVE vs SET QPS (`rdb_save_bench.py`)

Measures how often calling `SAVE` during a long `SET` stream affects SET throughput.

- Fixed **`N` SETs** (default 1M)
- Every **`interval`** SETs, attempt **`SAVE`** once
- Default intervals: `1000000 100000 10000 1000` plus a **baseline** with no SAVE
- Reports `saves_ok`, `saves_skipped`, wall time, **SET QPS**
- Default **`CLIENT=benchmark`**: `redis-benchmark` SET (`c=1 p=1`, same style as pipeline baseline ~10k rps) + `redis-cli SAVE` between chunks; `CLIENT=py` keeps the legacy redis-py loop (~2–3k rps)

**Busy policy** (previous background dump still running):

| `SAVE_BUSY` | Behavior |
|-------------|----------|
| `skip` (default) | Count skip, continue SETs immediately (Redis-like) |
| `wait` | Sleep/retry until `+OK` (forces a dump each interval; can tank QPS) |

Does **not** wait for dump files to finish after `+OK` (fork/COW overlap is part of the measurement).

Server must have `persistence.dir` set (built-in / `conf` default: `data/`).

```bash
./bin/vemory -c conf/vemory.ini   # separate terminal; dir=data
bench/.venv/bin/python bench/rdb_save_bench.py
HOST=127.0.0.1 PORT=8989 N=100000 bench/.venv/bin/python bench/rdb_save_bench.py   # quicker smoke
SAVE_BUSY=wait INTERVALS="100000 10000" PORT=8989 bench/.venv/bin/python bench/rdb_save_bench.py
```

| Env | Default | Meaning |
|-----|---------|---------|
| `HOST` / `PORT` | `127.0.0.1` / `6379` | Server |
| `N` | `1000000` | SET count per round |
| `INTERVALS` | `1000000 100000 10000 1000` | SAVE every N SETs |
| `INCLUDE_BASELINE` | `1` | Run a no-SAVE round first |
| `SAVE_BUSY` | `skip` | `skip` or `wait` when dump in progress |
| `CLIENT` | `benchmark` | `benchmark` (redis-benchmark + redis-cli) or `py` |
| `R` / `D` | `10000` / `64` | Keyspace / value size for `CLIENT=benchmark` |
| `VALUE` | 16×`x` | SET payload when `CLIENT=py` only |
| `WAIT_SLEEP_S` | `0.01` | Sleep between retries if `SAVE_BUSY=wait` |

## Sample results (local, indicative)

WSL2, debug `bin/vemory` on `:8989`, single event loop. Numbers are indicative only; re-run after changes.

### Smoke — KVS (`N=10000 C=10 P=8`)

| Command | Load RPS (approx.) | Notes |
|---------|-------------------:|-------|
| PING | ~114k | also baseline ~1.9k rps at `c=1 p=1` |
| ECHO | ~125k | |
| SET | ~115k | |
| GET | ~125k | |

### Smoke — Vector (`DIM=8 CARD=1000 QUERIES=500 THRESHOLD=0.2`)

Run: `PORT=8989 ./bench/smoke/vector.sh`

| Step | Result |
|------|--------|
| VSET 1000 | 1378.89 ms (725.2 ops/s) |
| VGET 500 | 380.98 ms (1312.4 qps, hits=98/500) |
| VDEL | e1 → 1 then 0; post-delete VGET miss |

### Vector metrics (`glove-25-angular`)

Run: `HOST=127.0.0.1 PORT=8989 bench/.venv/bin/python bench/vector_metrics.py`  
(`CARD=10000`, `QUERIES=200`, `THRESHOLD=0.2`, dim=25)

| Metric | Value |
|--------|------:|
| agree | 1.0000 |
| latency p50 / p99 | 1.83 ms / 2.81 ms |
| QPS@agree≥0.95 | 536.2 |
| VSET load | 329.4 ops/s |

### RDB SAVE vs SET QPS (`rdb_save_bench.py`)

Run: `HOST=127.0.0.1 PORT=8989 python3 bench/rdb_save_bench.py`  
(release `bin/vemory` on `:8989`; `CLIENT=benchmark`, `N=1000000`, `SAVE_BUSY=skip`)

| interval | saves_ok | saves_skipped | elapsed_s | set_qps |
|----------|---------:|--------------:|----------:|--------:|
| baseline | 0 | 0 | 74.773 | 13373.8 |
| 1000000 | 1 | 0 | 74.965 | 13339.6 |
| 100000 | 10 | 0 | 75.568 | 13233.1 |
| 10000 | 100 | 0 | 79.685 | 12549.5 |
| 1000 | 984 | 16 | 111.473 | 8970.8 |

## Notes

- `KvStore` is `unordered_map`; load gaps vs Redis are mostly single-threaded I/O, not linear scan.
- Prefer `redis-benchmark … PING` / `ECHO` over `-t ping`; avoid `redis-cli --pipe` (inline/HELLO quirks).
- Binary `VSET`/`VGET` need redis-py (or another RESP client); interactive `redis-cli` is awkward for float blobs.
- After changing `CommandType`, rebuild with `make clean && make` if handlers look stale.
- HDF5 download needs a normal `User-Agent` (script sets one; falls back to `vectors.erikbern.com` if needed).
