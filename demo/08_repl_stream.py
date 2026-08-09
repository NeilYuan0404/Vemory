#!/usr/bin/env python3
"""Demo 8 — replication live stream (real-time sync after fullsync).

Master and slave must already be running and past initial PSYNC fullsync.

  # terminal A — master
  ./bin/vemory 6379

  # terminal B — slave (wait until connected)
  ./bin/vemory --slaveof 127.0.0.1 6379 6380

  # terminal C — stream demo
  python3 demo/08_repl_stream.py
"""

from __future__ import annotations

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
    die,
    require_server,
    unique_key,
    wait_get,
)


def wait_slave_ready(slave) -> None:
    deadline = time.time() + SYNC_TIMEOUT_S
    while time.time() < deadline:
        try:
            if slave.ping() is True:
                return
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.05)
    die(f"slave not responding at {SLAVE_HOST}:{SLAVE_PORT}")


def main() -> int:
    master = require_server(HOST, int(os.environ.get("PORT", str(PORT))))
    slave = require_server(SLAVE_HOST, SLAVE_PORT)
    wait_slave_ready(slave)

    print(
        f"==> replication stream demo  master={HOST}:{os.environ.get('PORT', PORT)}  "
        f"slave={SLAVE_HOST}:{SLAVE_PORT}"
    )

    # Baseline: prove link is live before stressing stream path.
    probe_key = unique_key("repl-probe")
    probe_val = b"stream-ready"
    print("\n1) probe SET on master → slave")
    master.set(probe_key, probe_val)
    wait_get(slave, probe_key, probe_val, label="probe")

    n = int(os.environ.get("STREAM_N", "20"))
    print(f"\n2) stream {n} writes (SET → GET on slave)")
    keys: list[str] = []
    t0 = time.perf_counter()
    for i in range(n):
        key = unique_key(f"stream-{i}")
        val = f"live-{i}-{time.time_ns()}".encode()
        master.set(key, val)
        keys.append(key)
        wait_get(slave, key, val, label=f"stream[{i}]")
    elapsed = time.perf_counter() - t0
    print(f"  {n} keys synced in {elapsed:.3f}s ({n / elapsed:.1f} ops/s)")

    print("\n3) DEL on master propagates")
    del_key = keys[-1]
    master.delete(del_key)
    deadline = time.time() + SYNC_TIMEOUT_S
    while time.time() < deadline:
        if slave.get(del_key) is None:
            print(f"  DEL {del_key} → slave nil OK")
            break
        time.sleep(0.05)
    else:
        die(f"DEL not replicated within {SYNC_TIMEOUT_S}s")

    print("\ndone (live stream OK)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
