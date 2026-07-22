#!/usr/bin/env python3
"""Semantic-cache smoke: VSET load + VGET QPS + VDEL spot-check.

Requires a running Vemory and redis-py (pip install redis, or bench/.venv).
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from vemory_vec import connect, float_blob, vdel, vget, vset  # noqa: E402

HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "6379"))
DIM = int(os.environ.get("DIM", "8"))
CARD = int(os.environ.get("CARD", "1000"))
QUERIES = int(os.environ.get("QUERIES", "500"))
THRESHOLD = float(os.environ.get("THRESHOLD", "0.2"))


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def synth_vec(dim: int, seed: int) -> list[float]:
    return [(seed + d + 1) / (dim + seed + 1) for d in range(dim)]


def qps(n: int, seconds: float) -> str:
    if seconds <= 0:
        return "inf"
    return f"{n / seconds:.1f}"


def main() -> int:
    if DIM < 1 or CARD < 1 or QUERIES < 1:
        die("DIM, CARD, QUERIES must be >= 1")
    if THRESHOLD < 0:
        die("THRESHOLD must be >= 0")

    try:
        client = connect(HOST, PORT)
        if client.ping() is not True:
            die("PING failed")
    except Exception as exc:  # noqa: BLE001
        die(f"server not responding at {HOST}:{PORT} ({exc})")

    print(f"==> VSET load dim={DIM} card={CARD}")
    t0 = time.perf_counter()
    for i in range(1, CARD + 1):
        blob = float_blob(synth_vec(DIM, i))
        vset(client, blob, f"e{i}", f"q{i}", f"a{i}")
    load_s = time.perf_counter() - t0
    print(f"    VSET {CARD} in {load_s * 1000:.2f} ms ({qps(CARD, load_s)} ops/s)")

    print(f"==> VGET queries={QUERIES} threshold={THRESHOLD}")
    t0 = time.perf_counter()
    hits = 0
    for i in range(1, QUERIES + 1):
        seed = (i % CARD) + 1
        # Near-duplicate of a stored vector → expect hit under moderate threshold.
        vec = synth_vec(DIM, seed)
        vec[0] = vec[0] * 0.999 + 1e-6
        ans = vget(client, float_blob(vec), THRESHOLD)
        if ans == f"a{seed}".encode():
            hits += 1
    query_s = time.perf_counter() - t0
    print(
        f"    VGET {QUERIES} in {query_s * 1000:.2f} ms "
        f"({qps(QUERIES, query_s)} qps, hits={hits}/{QUERIES})"
    )

    print("==> VDEL spot-check")
    if vdel(client, "e1") != 1:
        die("VDEL e1 expected 1")
    # Tight gate: exact deleted vector must not match a neighbor as a hit.
    if vget(client, float_blob(synth_vec(DIM, 1)), 1e-6) is not None:
        die("VGET after VDEL e1 expected miss")
    if vdel(client, "e1") != 0:
        die("second VDEL e1 expected 0")
    print("    VDEL e1 ok (1 then 0; VGET miss)")

    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
