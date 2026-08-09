#!/usr/bin/env python3
"""Demo 6 — large string values (bulk SET / GET).

  # terminal A
  ./bin/vemory -c demo/vemory.demo.ini

  # terminal B
  python3 demo/06_large_value.py
  LARGE_BYTES=4194304 python3 demo/06_large_value.py   # 4 MiB
"""

from __future__ import annotations

import hashlib
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import HOST, PORT, die, require_server, unique_key  # noqa: E402

LARGE_BYTES = int(os.environ.get("LARGE_BYTES", "1048576"))  # default 1 MiB


def make_payload(n: int, seed: bytes) -> bytes:
    # Deterministic pseudo-random-ish blob (fast to generate, easy to verify).
    h = hashlib.sha256(seed).digest()
    out = bytearray()
    counter = 0
    while len(out) < n:
        block = hashlib.sha256(h + counter.to_bytes(4, "little")).digest()
        out.extend(block)
        counter += 1
    return bytes(out[:n])


def main() -> int:
    if LARGE_BYTES < 1024:
        die("LARGE_BYTES must be >= 1024")

    r = require_server()
    key = unique_key("large")
    payload = make_payload(LARGE_BYTES, b"vemory-large-value-demo")
    digest = hashlib.sha256(payload).hexdigest()

    print(f"==> large-value demo  {HOST}:{PORT}")
    print(f"    size={LARGE_BYTES} bytes  sha256={digest[:16]}…")

    t0 = time.perf_counter()
    r.set(key, payload)
    set_s = time.perf_counter() - t0
    print(f"\n1) SET {key}  ({set_s:.3f}s, {LARGE_BYTES / set_s / 1e6:.2f} MB/s)")

    t0 = time.perf_counter()
    got = r.get(key)
    get_s = time.perf_counter() - t0
    if got is None:
        die("GET returned nil")
    if len(got) != LARGE_BYTES:
        die(f"GET length mismatch: expected {LARGE_BYTES}, got {len(got)}")
    if hashlib.sha256(got).hexdigest() != digest:
        die("GET payload sha256 mismatch")
    print(
        f"2) GET {key}  ({get_s:.3f}s, {LARGE_BYTES / get_s / 1e6:.2f} MB/s)  "
        f"sha256 OK"
    )

    # Second smaller key to show multiple large entries coexist.
    key2 = unique_key("large2")
    small = make_payload(min(65536, LARGE_BYTES // 16), b"small-sidecar")
    r.set(key2, small)
    got2 = r.get(key2)
    if got2 != small:
        die("sidecar GET mismatch")

    print(f"3) sidecar key {key2} ({len(small)} bytes) OK")
    print("\ndone")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
