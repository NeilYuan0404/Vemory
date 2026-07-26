#!/usr/bin/env python3
"""Demo 4 — PSYNC replication (fullsync + stream).

Requires a running master and slave:

  # terminal A — master
  ./bin/vemory 6379

  # terminal B — slave
  ./bin/vemory --slaveof 127.0.0.1 6379 6380

  # terminal C
  python3 demo/04_repl.py
"""

from __future__ import annotations

import os
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import HOST, die, require_server  # noqa: E402

SLAVE_HOST = os.environ.get("SLAVE_HOST", HOST)
SLAVE_PORT = int(os.environ.get("SLAVE_PORT", "6380"))
SYNC_TIMEOUT_S = float(os.environ.get("SYNC_TIMEOUT_S", "30"))


def wait_get(client, key: str, expect: bytes, label: str) -> None:
    deadline = time.time() + SYNC_TIMEOUT_S
    got = None
    while time.time() < deadline:
        got = client.get(key)
        if got == expect:
            print(f"  {label}: GET {key} → {got!r}")
            return
        time.sleep(0.05)
    die(
        f"{label} failed within {SYNC_TIMEOUT_S}s "
        f"(expected {expect!r}, last GET={got!r})"
    )


def main() -> int:
    master = require_server(HOST, int(os.environ.get("PORT", "6379")))
    slave = require_server(SLAVE_HOST, SLAVE_PORT)

    print(f"==> replication demo  master={HOST}:{os.environ.get('PORT', '6379')}  "
          f"slave={SLAVE_HOST}:{SLAVE_PORT}")

    key = f"demo:repl:{uuid.uuid4().hex[:8]}"
    val = b"hello-from-master"
    print(f"\n1) SET on master (fullsync or stream catch-up)")
    master.set(key, val)
    print(f"  SET {key} = {val!r}")
    wait_get(slave, key, val, "slave synced")

    key2 = f"demo:repl:stream:{uuid.uuid4().hex[:8]}"
    val2 = b"streamed-write"
    print(f"\n2) SET on master after sync (live stream)")
    master.set(key2, val2)
    print(f"  SET {key2} = {val2!r}")
    wait_get(slave, key2, val2, "slave streamed")

    print("\ndone")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
