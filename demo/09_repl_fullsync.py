#!/usr/bin/env python3
"""Demo 9 — replication fullsync of pre-existing master data.

Seed keys on master *before* the slave attaches; after PSYNC fullsync the slave
should have the same KV + vectors.

  # terminal A — master only
  ./bin/vemory 6379

  # terminal B — seed existing data
  python3 demo/09_repl_fullsync.py seed

  # terminal C — start slave (fullsync pulls seeded data)
  ./bin/vemory --slaveof 127.0.0.1 6379 6380

  # terminal B — verify on slave
  python3 demo/09_repl_fullsync.py verify
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import (  # noqa: E402
    HOST,
    PORT,
    SLAVE_HOST,
    SLAVE_PORT,
    SYNC_TIMEOUT_S,
    connect,
    die,
    float_blob,
    require_server,
    vget,
    vset,
)

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "demo" / "data" / "repl_fullsync_manifest.json"

THRESHOLD = 0.15
KV_ENTRIES = [
    ("demo:fullsync:kv1", b"alpha"),
    ("demo:fullsync:kv2", "中文已有数据".encode()),
    ("demo:fullsync:kv3", b"before\x00slave"),
]
VEC_ENTRY = {
    "key": "fullsync-weather",
    "vec": [1.0, 0.0, 0.0, 0.0],
    "question": "existing question?",
    "answer": b"existing answer from master",
    "query": [0.99, 0.01, 0.0, 0.0],
}


def save_manifest() -> None:
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "kv": {k: v.decode("latin1") for k, v in KV_ENTRIES},
        "vector": {
            "key": VEC_ENTRY["key"],
            "answer": VEC_ENTRY["answer"].decode("latin1"),
        },
    }
    MANIFEST.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def cmd_seed() -> int:
    r = require_server()
    print(f"==> fullsync seed  master={HOST}:{PORT}")

    print("\n1) SET pre-existing string keys on master")
    for key, val in KV_ENTRIES:
        r.set(key, val)
        print(f"  SET {key} = {val!r}")

    print("\n2) VSET pre-existing vector on master")
    vset(
        r,
        float_blob(VEC_ENTRY["vec"]),
        VEC_ENTRY["key"],
        VEC_ENTRY["question"],
        VEC_ENTRY["answer"].decode(),
    )
    print(f"  VSET {VEC_ENTRY['key']!r} → {VEC_ENTRY['answer']!r}")

    save_manifest()
    print(f"\n3) manifest written: {MANIFEST.relative_to(ROOT)}")
    print(
        "\nnext:\n"
        f"  1) start slave:  ./bin/vemory --slaveof {HOST} {PORT} {SLAVE_PORT}\n"
        "  2) run:          python3 demo/09_repl_fullsync.py verify"
    )
    return 0


def wait_slave() -> None:
    deadline = time.time() + SYNC_TIMEOUT_S
    while time.time() < deadline:
        try:
            client = connect(SLAVE_HOST, SLAVE_PORT)
            if client.ping() is True:
                return
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.05)
    die(f"slave not responding at {SLAVE_HOST}:{SLAVE_PORT}")


def wait_kv(slave, key: str, expect: bytes) -> None:
    deadline = time.time() + SYNC_TIMEOUT_S
    got = None
    while time.time() < deadline:
        got = slave.get(key)
        if got == expect:
            print(f"  OK  GET {key} → {got!r}")
            return
        time.sleep(0.05)
    die(f"fullsync KV {key}: expected {expect!r}, last GET={got!r}")


def cmd_verify() -> int:
    if not MANIFEST.is_file():
        die(f"manifest missing — run seed first: {MANIFEST}")

    wait_slave()
    slave = require_server(SLAVE_HOST, SLAVE_PORT)
    print(f"==> fullsync verify  slave={SLAVE_HOST}:{SLAVE_PORT}")

    print("\n1) KV from fullsync RDB")
    for key, expect in KV_ENTRIES:
        wait_kv(slave, key, expect)

    print("\n2) vector from fullsync RDB")
    ans = vget(slave, float_blob(VEC_ENTRY["query"]), THRESHOLD)
    print(f"  VGET {VEC_ENTRY['key']!r} → {ans!r}")
    if ans != VEC_ENTRY["answer"]:
        die(f"vector fullsync failed: expected {VEC_ENTRY['answer']!r}, got {ans!r}")

    print("\nfullsync OK (pre-existing master data on slave)")
    return 0


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("seed", "verify"):
        print(__doc__)
        print("usage: python3 demo/09_repl_fullsync.py seed|verify")
        return 2
    if sys.argv[1] == "seed":
        return cmd_seed()
    return cmd_verify()


if __name__ == "__main__":
    sys.exit(main())
