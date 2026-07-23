#!/usr/bin/env python3
"""Semantic-cache RDB SAVE smoke: VSET → SAVE → dump.* → VGET.

Requires a running Vemory with persistence.dir set (default data/), and
redis-py (pip install redis, or bench/.venv).

DUMP_DIR must match the server's persistence.dir (absolute or relative to
the server's cwd). Default: <repo>/data.

Does not restart the process; reload-after-restart is covered by
demo/03_rdb.py. This smoke checks that a vector snapshot is written and
live VGET still hits after SAVE returns.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from vemory_vec import connect, float_blob, save, vget, vset  # noqa: E402

HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "6379"))
DIM = int(os.environ.get("DIM", "8"))
CARD = int(os.environ.get("CARD", "8"))
THRESHOLD = float(os.environ.get("THRESHOLD", "0.2"))
ROOT = Path(__file__).resolve().parents[2]
DUMP_DIR = Path(os.environ.get("DUMP_DIR", str(ROOT / "data")))
SAVE_TIMEOUT_S = float(os.environ.get("SAVE_TIMEOUT_S", "10"))

DUMP_FILES = ("dump.meta", "dump.kv", "dump.nodes", "dump.usearch")
KEY_PREFIX = "smokerdb:"


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def synth_vec(dim: int, seed: int) -> list[float]:
    return [(seed + d + 1) / (dim + seed + 1) for d in range(dim)]


def wait_for_dumps(timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if all((DUMP_DIR / name).is_file() for name in DUMP_FILES):
            # Background SAVE: files may appear mid-write; require non-empty
            # usearch (vectors present) and meta.
            usearch = DUMP_DIR / "dump.usearch"
            meta = DUMP_DIR / "dump.meta"
            if usearch.stat().st_size > 0 and meta.stat().st_size > 0:
                return
        time.sleep(0.05)
    missing = []
    for name in DUMP_FILES:
        path = DUMP_DIR / name
        if not path.is_file():
            missing.append(f"{name} (missing)")
        elif path.stat().st_size == 0 and name in ("dump.meta", "dump.usearch"):
            missing.append(f"{name} (empty)")
    die(
        f"dump files not ready under {DUMP_DIR} within {timeout_s}s: "
        f"{missing or 'timeout'}"
    )


def main() -> int:
    if DIM < 1 or CARD < 1:
        die("DIM and CARD must be >= 1")
    if THRESHOLD < 0:
        die("THRESHOLD must be >= 0")
    if SAVE_TIMEOUT_S <= 0:
        die("SAVE_TIMEOUT_S must be > 0")

    try:
        client = connect(HOST, PORT)
        if client.ping() is not True:
            die("PING failed")
    except Exception as exc:  # noqa: BLE001
        die(f"server not responding at {HOST}:{PORT} ({exc})")

    print(
        f"==> vector RDB SAVE smoke  {HOST}:{PORT}  "
        f"dim={DIM} card={CARD}  dump_dir={DUMP_DIR}"
    )

    print("==> VSET")
    for i in range(1, CARD + 1):
        blob = float_blob(synth_vec(DIM, i))
        vset(client, blob, f"{KEY_PREFIX}e{i}", f"q{i}", f"a{i}")
    print(f"    wrote {CARD} vectors")

    print("==> VGET before SAVE")
    for i in range(1, CARD + 1):
        ans = vget(client, float_blob(synth_vec(DIM, i)), THRESHOLD)
        if ans != f"a{i}".encode():
            die(f"pre-SAVE VGET e{i}: expected b'a{i}', got {ans!r}")
    print(f"    hits {CARD}/{CARD}")

    print("==> SAVE")
    try:
        save(client)
    except Exception as exc:  # noqa: BLE001
        msg = str(exc).lower()
        if "persistence dir not set" in msg:
            die(
                "SAVE failed: persistence dir not set "
                "(set persistence.dir, e.g. data)"
            )
        if "already in progress" in msg:
            die("SAVE failed: background save already in progress; retry")
        die(f"SAVE failed: {exc}")
    print("    +OK (waiting for dump.* ...)")
    wait_for_dumps(SAVE_TIMEOUT_S)

    print("==> snapshot files")
    for name in DUMP_FILES:
        path = DUMP_DIR / name
        print(f"    {path}  ({path.stat().st_size} bytes)")

    print("==> VGET after SAVE (live)")
    for i in range(1, CARD + 1):
        ans = vget(client, float_blob(synth_vec(DIM, i)), THRESHOLD)
        if ans != f"a{i}".encode():
            die(f"post-SAVE VGET e{i}: expected b'a{i}', got {ans!r}")
    print(f"    hits {CARD}/{CARD}")

    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
