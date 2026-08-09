# Vemory demos

Simple scripts (not benches). Needs a running server and `redis` (`pip install redis`, or `bench/.venv`).

```bash
make
./bin/vemory -c demo/vemory.demo.ini          # terminal A (port 8888)

# terminal B — prefer bench/.venv if present
python3 demo/01_pipeline.py                   # batch / pipeline
python3 demo/02_vector.py                     # VSET / VGET / VDEL
python3 demo/03_rdb.py dump                   # write + SAVE
# restart server (Ctrl+C, same start command)
python3 demo/03_rdb.py verify                 # KV + vector after load

python3 demo/05_special_chars.py              # UTF-8 / NUL / whitespace in SET/GET
python3 demo/06_large_value.py                # multi-MiB bulk values
LARGE_BYTES=4194304 python3 demo/06_large_value.py

# AOF (separate config, port 8889)
./bin/vemory -c demo/vemory.aof.ini           # terminal A
PORT=8889 python3 demo/07_aof.py write
# restart server
PORT=8889 python3 demo/07_aof.py verify

# replication (master + slave)
./bin/vemory 6379                             # terminal A — master
./bin/vemory --slaveof 127.0.0.1 6379 6380    # terminal B — slave
python3 demo/04_repl.py                       # quick fullsync + stream
python3 demo/08_repl_stream.py                # live stream stress
python3 demo/09_repl_fullsync.py seed         # seed master first
# start slave if not running, then:
python3 demo/09_repl_fullsync.py verify       # pre-existing data on slave
```

| Script | Shows |
|--------|--------|
| `01_pipeline.py` | sequential vs pipelined SET/GET |
| `02_vector.py` | semantic cache with float32 blobs |
| `03_rdb.py` | `SAVE` + restart restore (KV + vectors) |
| `04_repl.py` | PSYNC fullsync + live stream (SET on master → GET on slave) |
| `05_special_chars.py` | binary-safe special characters in string KVS |
| `06_large_value.py` | large bulk SET/GET (`LARGE_BYTES`, default 1 MiB) |
| `07_aof.py` | AOF append + restart replay (`write` / `verify`) |
| `08_repl_stream.py` | real-time replication stream after sync |
| `09_repl_fullsync.py` | fullsync of data that existed before slave attached |

Env: `HOST` (default `127.0.0.1`), `PORT` (default `6379`; use `8888` with `vemory.demo.ini`, `8889` with `vemory.aof.ini`). Replication demos also use `SLAVE_HOST` / `SLAVE_PORT` (default `6380`), `SYNC_TIMEOUT_S`, `STREAM_N` (for `08`).
