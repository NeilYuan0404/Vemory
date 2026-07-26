# Vemory demos

Simple scripts (not benches). Needs a running server and `redis` (`pip install redis`, or `bench/.venv`).

```bash
make
./bin/vemory -c demo/vemory.demo.ini          # terminal A

# terminal B — prefer bench/.venv if present
python3 demo/01_pipeline.py                   # batch / pipeline
python3 demo/02_vector.py                     # VSET / VGET / VDEL
python3 demo/03_rdb.py dump                   # write + SAVE
# restart server (Ctrl+C, same start command)
python3 demo/03_rdb.py verify                 # KV + vector after load

# replication (master + slave)
./bin/vemory 6379                             # terminal A
./bin/vemory --slaveof 127.0.0.1 6379 6380    # terminal B
python3 demo/04_repl.py                       # terminal C
```

| Script | Shows |
|--------|--------|
| `01_pipeline.py` | sequential vs pipelined SET/GET |
| `02_vector.py` | semantic cache with float32 blobs |
| `03_rdb.py` | `SAVE` + restart restore (KV + vectors) |
| `04_repl.py` | PSYNC fullsync + live stream (SET on master → GET on slave) |

Env: `HOST` (default `127.0.0.1`), `PORT` (default `6379`). Replication demo also uses `SLAVE_HOST` / `SLAVE_PORT` (default `6380`).
