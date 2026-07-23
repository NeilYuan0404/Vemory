#!/usr/bin/env python3
"""Measure SET QPS under different RDB SAVE frequencies.

Default engine uses redis-benchmark for SET (c=1 p=1, same as pipeline baseline)
and redis-cli for SAVE. Optional CLIENT=py uses redis-py (lower QPS, legacy).

Runs N SETs; every `interval` SETs attempts SAVE. If a background save is
already in progress, default policy is skip (count saves_skipped). Optional
SAVE_BUSY=wait retries until +OK.

Requires a running Vemory with persistence.dir set (default data/).
"""

from __future__ import annotations

import csv
import io
import os
import shutil
import subprocess
import sys
import time
from typing import Optional

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
CLIENT = os.environ.get("CLIENT", "benchmark").lower()  # benchmark | py
R = int(os.environ.get("R", "10000"))
D = int(os.environ.get("D", "64"))
VALUE = os.environ.get("VALUE", "x" * 16)
WAIT_SLEEP_S = float(os.environ.get("WAIT_SLEEP_S", "0.01"))


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def require_tools() -> None:
    for name in ("redis-benchmark", "redis-cli"):
        if shutil.which(name) is None:
            die(f"{name} not found (install redis-tools)")


def check_ping() -> None:
    try:
        out = subprocess.check_output(
            ["redis-cli", "-2", "-h", HOST, "-p", str(PORT), "PING"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        die(f"cannot connect to {HOST}:{PORT} ({exc})")
    if "PONG" not in out.upper():
        die(f"PING failed at {HOST}:{PORT} (got {out!r})")


def bench_set_n(n: int) -> None:
    """Run n SETs via redis-benchmark (c=1 p=1)."""
    cmd = [
        "redis-benchmark",
        "-h",
        HOST,
        "-p",
        str(PORT),
        "-t",
        "set",
        "-n",
        str(n),
        "-c",
        "1",
        "-P",
        "1",
        "-r",
        str(R),
        "-d",
        str(D),
        "--csv",
    ]
    try:
        raw = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
    except subprocess.CalledProcessError as exc:
        die(f"redis-benchmark failed: {' '.join(cmd)} ({exc})")

    reader = csv.reader(io.StringIO(raw))
    rows = list(reader)
    for row in rows[1:]:
        if len(row) >= 2 and row[1].strip():
            return
    die(f"no RPS in redis-benchmark csv output: {raw!r}")


def try_save_cli() -> str:
    """Return 'ok', 'busy', or raise on other errors."""
    try:
        out = subprocess.check_output(
            ["redis-cli", "-2", "-h", HOST, "-p", str(PORT), "SAVE"],
            stderr=subprocess.STDOUT,
            text=True,
        ).strip()
    except subprocess.CalledProcessError as exc:
        out = (exc.output or "").strip()

    lower = out.lower()
    if out.upper() == "OK":
        return "ok"
    if "already in progress" in lower:
        return "busy"
    if "persistence dir not set" in lower:
        die("SAVE failed: persistence dir not set "
            "(set persistence.dir, e.g. data)")
    die(f"SAVE failed: {out!r}")


def save_once_cli() -> tuple[int, int]:
    """Attempt one SAVE. Returns (ok_delta, skipped_delta)."""
    if SAVE_BUSY == "wait":
        while True:
            st = try_save_cli()
            if st == "ok":
                return 1, 0
            time.sleep(WAIT_SLEEP_S)
    st = try_save_cli()
    if st == "ok":
        return 1, 0
    return 0, 1


def run_round_benchmark(
    interval: Optional[int],
) -> tuple[int, int, float, float]:
    """Returns saves_ok, saves_skipped, elapsed_s, set_qps."""
    saves_ok = 0
    saves_skipped = 0
    t0 = time.perf_counter()
    done = 0
    while done < N:
        chunk = N - done if interval is None else min(interval, N - done)
        bench_set_n(chunk)
        done += chunk
        if interval is not None and done % interval == 0:
            ok, skipped = save_once_cli()
            saves_ok += ok
            saves_skipped += skipped
    elapsed = time.perf_counter() - t0
    qps = N / elapsed if elapsed > 0 else 0.0
    return saves_ok, saves_skipped, elapsed, qps


def run_round_py(
    interval: Optional[int],
    tag: str,
) -> tuple[int, int, float, float]:
    """Legacy redis-py path (lower QPS)."""
    try:
        import redis
        from redis.exceptions import RedisError, ResponseError
    except ImportError:
        die("redis package required for CLIENT=py "
            "(pip install -r bench/requirements.txt)")

    r = redis.Redis(host=HOST, port=PORT, decode_responses=True, protocol=2)
    try:
        if r.ping() is not True:
            die(f"PING failed at {HOST}:{PORT}")
    except RedisError as exc:
        die(f"cannot connect to {HOST}:{PORT} ({exc})")

    def try_save() -> str:
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

    def save_once() -> tuple[int, int]:
        if SAVE_BUSY == "wait":
            while True:
                st = try_save()
                if st == "ok":
                    return 1, 0
                time.sleep(WAIT_SLEEP_S)
        st = try_save()
        if st == "ok":
            return 1, 0
        return 0, 1

    saves_ok = 0
    saves_skipped = 0
    prefix = f"rdbbench:{tag}:"
    t0 = time.perf_counter()
    for i in range(1, N + 1):
        r.set(f"{prefix}{i}", VALUE)
        if interval is not None and i % interval == 0:
            ok, skipped = save_once()
            saves_ok += ok
            saves_skipped += skipped
    elapsed = time.perf_counter() - t0
    qps = N / elapsed if elapsed > 0 else 0.0
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
    if CLIENT not in ("benchmark", "py"):
        die("CLIENT must be benchmark or py")

    if CLIENT == "benchmark":
        require_tools()
        check_ping()

    intervals = parse_intervals(INTERVALS)

    print(f"# RDB SAVE vs SET QPS  host={HOST}:{PORT}  N={N}  "
          f"SAVE_BUSY={SAVE_BUSY}  CLIENT={CLIENT}")
    if CLIENT == "benchmark":
        print(f"# SET via redis-benchmark c=1 p=1 r={R} d={D}; "
              "SAVE via redis-cli")
    else:
        print("# SET via redis-py (sync); SAVE via redis-py")
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
        if CLIENT == "benchmark":
            ok, skipped, elapsed, qps = run_round_benchmark(interval)
        else:
            ok, skipped, elapsed, qps = run_round_py(interval, label)
        print(f"{label:<12} {ok:10d} {skipped:14d} "
              f"{elapsed:12.3f} {qps:12.1f}")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
