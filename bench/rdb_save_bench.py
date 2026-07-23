#!/usr/bin/env python3
"""Measure SET QPS under different RDB SAVE frequencies.

Runs N SETs; every `interval` SETs attempts SAVE. If a background save is
already in progress, default policy is skip (count saves_skipped). Optional
SAVE_BUSY=wait retries until +OK.

Requires a running Vemory with persistence.dir set (default data/).
"""

from __future__ import annotations

import os
import sys
import time
from typing import Optional

try:
    import redis
    from redis.exceptions import ResponseError
except ImportError:
    print("error: redis package required (pip install -r bench/requirements.txt)",
          file=sys.stderr)
    sys.exit(1)

HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "6379"))
N = int(os.environ.get("N", "1000000"))
INTERVALS = os.environ.get("INTERVALS", "1000000 100000 10000 1000")
INCLUDE_BASELINE = os.environ.get("INCLUDE_BASELINE", "1") not in (
    "0",
    "false",
    "False",
    "no",
)
SAVE_BUSY = os.environ.get("SAVE_BUSY", "skip").lower()  # skip | wait
VALUE = os.environ.get("VALUE", "x" * 16)
WAIT_SLEEP_S = float(os.environ.get("WAIT_SLEEP_S", "0.01"))


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def connect() -> "redis.Redis":
    r = redis.Redis(host=HOST, port=PORT, decode_responses=True)
    try:
        if r.ping() is not True:
            die(f"PING failed at {HOST}:{PORT}")
    except redis.RedisError as exc:
        die(f"cannot connect to {HOST}:{PORT} ({exc})")
    return r


def try_save(r: "redis.Redis") -> str:
    """Return 'ok', 'busy', or raise on other errors."""
    try:
        r.execute_command("SAVE")
        return "ok"
    except ResponseError as exc:
        msg = str(exc).lower()
        if "already in progress" in msg:
            return "busy"
        if "persistence dir not set" in msg:
            die("SAVE failed: persistence dir not set "
                "(set persistence.dir, e.g. data)")
        raise


def save_once(r: "redis.Redis") -> tuple[int, int]:
    """Attempt one SAVE. Returns (ok_delta, skipped_delta)."""
    if SAVE_BUSY == "wait":
        while True:
            st = try_save(r)
            if st == "ok":
                return 1, 0
            time.sleep(WAIT_SLEEP_S)
    # default: skip
    st = try_save(r)
    if st == "ok":
        return 1, 0
    return 0, 1


def run_round(
    r: "redis.Redis",
    n: int,
    interval: Optional[int],
    tag: str,
) -> tuple[int, int, float, float]:
    """Returns saves_ok, saves_skipped, elapsed_s, set_qps."""
    saves_ok = 0
    saves_skipped = 0
    prefix = f"rdbbench:{tag}:"
    t0 = time.perf_counter()
    for i in range(1, n + 1):
        r.set(f"{prefix}{i}", VALUE)
        if interval is not None and i % interval == 0:
            ok, skipped = save_once(r)
            saves_ok += ok
            saves_skipped += skipped
    elapsed = time.perf_counter() - t0
    qps = n / elapsed if elapsed > 0 else 0.0
    return saves_ok, saves_skipped, elapsed, qps


def parse_intervals(text: str) -> list[int]:
    out: list[int] = []
    for part in text.split():
        v = int(part)
        if v <= 0:
            die(f"invalid INTERVALS entry: {part}")
        out.append(v)
    if not out:
        die("INTERVALS is empty")
    return out


def main() -> None:
    if SAVE_BUSY not in ("skip", "wait"):
        die("SAVE_BUSY must be skip or wait")

    intervals = parse_intervals(INTERVALS)
    r = connect()

    print(f"# RDB SAVE vs SET QPS  host={HOST}:{PORT}  N={N}  "
          f"SAVE_BUSY={SAVE_BUSY}")
    print("# Requires persistence.dir (default data/). "
          "SAVE is fork-background; busy => skip by default.")
    print()
    print(f"{'interval':<12} {'saves_ok':>10} {'saves_skipped':>14} "
          f"{'elapsed_s':>12} {'set_qps':>12}")

    rounds: list[tuple[str, Optional[int]]] = []
    if INCLUDE_BASELINE:
        rounds.append(("baseline", None))
    for iv in intervals:
        rounds.append((str(iv), iv))

    for label, interval in rounds:
        ok, skipped, elapsed, qps = run_round(r, N, interval, label)
        print(f"{label:<12} {ok:10d} {skipped:14d} "
              f"{elapsed:12.3f} {qps:12.1f}")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
