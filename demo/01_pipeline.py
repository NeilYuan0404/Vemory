#!/usr/bin/env python3
"""Demo 1 — batch / pipeline (SET + GET).

One TCP round-trip can carry many commands. Compare sequential vs pipelined.

  # terminal A
  ./bin/vemory -c demo/vemory.demo.ini

  # terminal B
  python3 demo/01_pipeline.py
  PORT=8989 python3 demo/01_pipeline.py
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import HOST, PORT, die, require_server  # noqa: E402

N = 1000


def main() -> int:
    r = require_server()
    print(f"==> pipeline demo  {HOST}:{PORT}  N={N}")

    # --- sequential: 1 RTT per command ---
    t0 = time.perf_counter()
    for i in range(N):
        r.set(f"demo:p:{i}", f"v{i}")
    seq_set_s = time.perf_counter() - t0

    t0 = time.perf_counter()
    for i in range(N):
        if r.get(f"demo:p:{i}") != f"v{i}".encode():
            die(f"GET mismatch at {i}")
    seq_get_s = time.perf_counter() - t0

    print(f"sequential  SET {N}: {seq_set_s * 1000:.1f} ms  ({N / seq_set_s:.0f} ops/s)")
    print(f"sequential  GET {N}: {seq_get_s * 1000:.1f} ms  ({N / seq_get_s:.0f} ops/s)")

    # --- pipeline: many commands, one flush / one reply batch ---
    pipe = r.pipeline(transaction=False)
    t0 = time.perf_counter()
    for i in range(N):
        pipe.set(f"demo:p:{i}", f"v{i}")
    replies = pipe.execute()
    pipe_set_s = time.perf_counter() - t0
    if any(x is not True for x in replies):
        die(f"pipeline SET unexpected: {replies[:3]!r} ...")

    pipe = r.pipeline(transaction=False)
    t0 = time.perf_counter()
    for i in range(N):
        pipe.get(f"demo:p:{i}")
    replies = pipe.execute()
    pipe_get_s = time.perf_counter() - t0
    for i, val in enumerate(replies):
        if val != f"v{i}".encode():
            die(f"pipeline GET mismatch at {i}: {val!r}")

    print(f"pipeline    SET {N}: {pipe_set_s * 1000:.1f} ms  ({N / pipe_set_s:.0f} ops/s)")
    print(f"pipeline    GET {N}: {pipe_get_s * 1000:.1f} ms  ({N / pipe_get_s:.0f} ops/s)")
    print(
        f"speedup     SET ~{seq_set_s / pipe_set_s:.1f}x  "
        f"GET ~{seq_get_s / pipe_get_s:.1f}x"
    )
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
