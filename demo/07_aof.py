#!/usr/bin/env python3
"""Demo 7 — AOF persistence (KV + vectors, no SAVE).

Start with AOF-enabled config; writes append to appendonly.aof and replay on restart.

  # terminal A
  ./bin/vemory -c demo/vemory.aof.ini

  # terminal B — write
  python3 demo/07_aof.py write

  # terminal A — Ctrl+C, start again (same command)

  # terminal B — verify replay
  python3 demo/07_aof.py verify
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import (  # noqa: E402
    HOST,
    PORT,
    die,
    float_blob,
    require_server,
    vget,
    vset,
)

ROOT = Path(__file__).resolve().parents[1]
AOF_DIR = ROOT / "demo" / "data_aof"
AOF_FILE = AOF_DIR / "appendonly.aof"

KV_KEY = "demo:aof:hello"
KV_VAL = b"world-from-aof"
VEC_KEY = "aof-weather"
VEC = [1.0, 0.0, 0.0, 0.0]
VEC_Q = "will it rain?"
VEC_A = b"bring an umbrella (aof)"
THRESHOLD = 0.15


def wait_for_aof(timeout_s: float = 3.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if AOF_FILE.is_file() and AOF_FILE.stat().st_size > 0:
            return
        time.sleep(0.05)
    if not AOF_FILE.is_file():
        die(f"AOF file not found: {AOF_FILE} (is aof=true in config?)")
    die(f"AOF file empty: {AOF_FILE}")


def cmd_write() -> int:
    r = require_server()
    print(f"==> AOF write  {HOST}:{PORT}")
    print(f"    persistence dir: {AOF_DIR}")

    print("\n1) SET string (appended to AOF)")
    r.set(KV_KEY, KV_VAL)
    print(f"  SET {KV_KEY} = {KV_VAL!r}")

    print("\n2) VSET vector (appended to AOF)")
    vset(r, float_blob(VEC), VEC_KEY, VEC_Q, VEC_A.decode())
    print(f"  VSET {VEC_KEY!r} → {VEC_A!r}")

    print("\n3) wait for appendonly.aof")
    wait_for_aof()
    size = AOF_FILE.stat().st_size
    print(f"  {AOF_FILE.relative_to(ROOT)}  ({size} bytes)")

    print(
        "\nnext:\n"
        "  1) stop the server (Ctrl+C)\n"
        "  2) start again:  ./bin/vemory -c demo/vemory.aof.ini\n"
        "  3) run:          python3 demo/07_aof.py verify"
    )
    return 0


def cmd_verify() -> int:
    r = require_server()
    print(f"==> AOF verify  {HOST}:{PORT}")

    got = r.get(KV_KEY)
    print(f"\n1) GET {KV_KEY} → {got!r}")
    if got != KV_VAL:
        die(f"KV AOF replay failed: expected {KV_VAL!r}, got {got!r}")

    ans = vget(r, float_blob([0.99, 0.01, 0.0, 0.0]), THRESHOLD)
    print(f"2) VGET near {VEC_KEY!r} → {ans!r}")
    if ans != VEC_A:
        die(f"vector AOF replay failed: expected {VEC_A!r}, got {ans!r}")

    if AOF_FILE.is_file():
        print(f"\n3) AOF file still present ({AOF_FILE.stat().st_size} bytes)")
    print("\nAOF replay OK (KV + vector)")
    return 0


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("write", "verify"):
        print(__doc__)
        print("usage: python3 demo/07_aof.py write|verify")
        return 2
    if sys.argv[1] == "write":
        return cmd_write()
    return cmd_verify()


if __name__ == "__main__":
    sys.exit(main())
