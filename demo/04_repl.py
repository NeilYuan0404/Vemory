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
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import (  # noqa: E402
    HOST,
    PORT,
    SLAVE_HOST,
    SLAVE_PORT,
    require_server,
    unique_key,
    wait_get,
)


def main() -> int:
    master = require_server(HOST, int(os.environ.get("PORT", str(PORT))))
    slave = require_server(SLAVE_HOST, SLAVE_PORT)

    print(
        f"==> replication demo  master={HOST}:{os.environ.get('PORT', PORT)}  "
        f"slave={SLAVE_HOST}:{SLAVE_PORT}"
    )

    key = unique_key("repl")
    val = b"hello-from-master"
    print(f"\n1) SET on master (fullsync or stream catch-up)")
    master.set(key, val)
    print(f"  SET {key} = {val!r}")
    wait_get(slave, key, val, label="slave synced")

    key2 = unique_key("repl-stream")
    val2 = b"streamed-write"
    print(f"\n2) SET on master after sync (live stream)")
    master.set(key2, val2)
    print(f"  SET {key2} = {val2!r}")
    wait_get(slave, key2, val2, label="slave streamed")

    print("\ndone")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
