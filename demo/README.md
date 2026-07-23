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
```

| Script | Shows |
|--------|--------|
| `01_pipeline.py` | sequential vs pipelined SET/GET |
| `02_vector.py` | semantic cache with float32 blobs |
| `03_rdb.py` | `SAVE` + restart restore (KV + vectors) |

Env: `HOST` (default `127.0.0.1`), `PORT` (default `6379`).
