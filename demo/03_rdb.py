#!/usr/bin/env python3
"""Demo 3 — RDB snapshot (KV + vectors).

Two steps (SAVE is fork-background; restart loads dump.* when load_on_startup=true):

  # terminal A — start with demo config (dir=demo/data, load_on_startup=true)
  ./bin/vemory -c demo/vemory.demo.ini

  # terminal B — write + SAVE
  python3 demo/03_rdb.py dump

  # terminal A — Ctrl+C, start again (same command)
  ./bin/vemory -c demo/vemory.demo.ini

  # terminal B — verify restored state
  python3 demo/03_rdb.py verify
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
    save,
    vget,
    vset,
)

ROOT = Path(__file__).resolve().parents[1]
DUMP_DIR = ROOT / "demo" / "data"
DUMP_FILES = ("dump.meta", "dump.kv", "dump.nodes", "dump.usearch")

KV_KEY = "demo:rdb:hello"
KV_VAL = b"world"
VEC_KEY = "rdb-weather"
VEC = [1.0, 0.0, 0.0, 0.0]
VEC_Q = "will it rain?"
VEC_A = b"bring an umbrella"
THRESHOLD = 0.15


def wait_for_dumps(timeout_s: float = 5.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if all((DUMP_DIR / name).is_file() for name in DUMP_FILES):
            return
        time.sleep(0.05)
    missing = [n for n in DUMP_FILES if not (DUMP_DIR / n).is_file()]
    die(f"dump files not ready under {DUMP_DIR}: missing {missing}")


def cmd_dump() -> int:
    r = require_server()
    print(f"==> RDB dump  {HOST}:{PORT}")
    print(f"    snapshot dir: {DUMP_DIR}")

    print("\n1) write string KVS")
    r.set(KV_KEY, KV_VAL)
    print(f"  SET {KV_KEY} = {KV_VAL!r}")

    print("\n2) write semantic-cache vector")
    vset(r, float_blob(VEC), VEC_KEY, VEC_Q, VEC_A.decode())
    print(f"  VSET {VEC_KEY!r} → {VEC_A!r}")

    print("\n3) SAVE (background fork)")
    save(r)
    print("  SAVE → OK (waiting for dump.* ...)")
    wait_for_dumps()

    print("\n4) snapshot files:")
    for name in DUMP_FILES:
        path = DUMP_DIR / name
        print(f"  {path.relative_to(ROOT)}  ({path.stat().st_size} bytes)")

    print(
        "\nnext:\n"
        "  1) stop the server (Ctrl+C)\n"
        "  2) start again:  ./bin/vemory -c demo/vemory.demo.ini\n"
        "  3) run:          python3 demo/03_rdb.py verify"
    )
    return 0


def cmd_verify() -> int:
    r = require_server()
    print(f"==> RDB verify  {HOST}:{PORT}")

    got = r.get(KV_KEY)
    print(f"\n1) GET {KV_KEY} → {got!r}")
    if got != KV_VAL:
        die(f"KV restore failed: expected {KV_VAL!r}, got {got!r}")

    ans = vget(r, float_blob([0.99, 0.01, 0.0, 0.0]), THRESHOLD)
    print(f"2) VGET near {VEC_KEY!r} → {ans!r}")
    if ans != VEC_A:
        die(f"vector restore failed: expected {VEC_A!r}, got {ans!r}")

    print("\nrestore OK (KV + vector)")
    return 0


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("dump", "verify"):
        print(__doc__)
        print("usage: python3 demo/03_rdb.py dump|verify")
        return 2
    if sys.argv[1] == "dump":
        return cmd_dump()
    return cmd_verify()


if __name__ == "__main__":
    sys.exit(main())
