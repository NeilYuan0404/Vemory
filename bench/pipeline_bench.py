#!/usr/bin/env python3
"""Pipeline-depth SET/GET compare: Vemory vs Redis via redis-benchmark (c=1).

Mirrors the former smoke/pipeline.sh comparison sweep. Requires both servers
and redis-benchmark / redis-cli on PATH.
"""

from __future__ import annotations

import csv
import io
import os
import shutil
import subprocess
import sys
VEMORY_HOST = os.environ.get("VEMORY_HOST", "127.0.0.1")
VEMORY_PORT = int(os.environ.get("VEMORY_PORT", "8989"))
REDIS_HOST = os.environ.get("REDIS_HOST", "127.0.0.1")
REDIS_PORT = int(os.environ.get("REDIS_PORT", "6379"))
R = int(os.environ.get("R", "10000"))
D = int(os.environ.get("D", "64"))
PIPELINES = os.environ.get("PIPELINES", "10 20 40 100 160")
N_P1 = int(os.environ.get("N_P1", "10000"))
N_MID = int(os.environ.get("N_MID", "100000"))
N_HIGH = int(os.environ.get("N_HIGH", "5000000"))


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def require_tools() -> None:
    for name in ("redis-benchmark", "redis-cli"):
        if shutil.which(name) is None:
            die(f"{name} not found (install redis-tools)")


def check_ping(host: str, port: int, label: str) -> None:
    try:
        out = subprocess.check_output(
            ["redis-cli", "-2", "-h", host, "-p", str(port), "PING"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        die(f"{label} not responding at {host}:{port} ({exc})")
    if "PONG" not in out.upper():
        die(f"{label} not responding at {host}:{port} (got {out!r})")


def n_for_p(p: int) -> int:
    if p <= 1:
        return N_P1
    if p <= 20:
        return N_MID
    return N_HIGH


def bench_rps(host: str, port: int, *args: str) -> str:
    """Run redis-benchmark --csv; return first data-row RPS (column 2)."""
    cmd = [
        "redis-benchmark",
        "-h",
        host,
        "-p",
        str(port),
        "--csv",
        *args,
    ]
    try:
        raw = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
    except subprocess.CalledProcessError as exc:
        die(f"redis-benchmark failed: {' '.join(cmd)} ({exc})")

    # Prefer --csv: redis-benchmark 7.x -q can print bogus/negative RPS on WSL.
    reader = csv.reader(io.StringIO(raw))
    rows = list(reader)
    for row in rows[1:]:
        if len(row) >= 2 and row[1].strip():
            return row[1].strip()
    die(f"no RPS in redis-benchmark csv output: {raw!r}")


def bench_set_get(host: str, port: int, n: int, p: int) -> tuple[str, str]:
    common = ["-n", str(n), "-c", "1", "-P", str(p), "-r", str(R), "-d", str(D)]
    set_rps = bench_rps(host, port, "-t", "set", *common)
    get_rps = bench_rps(host, port, "-t", "get", *common)
    return set_rps, get_rps


def parse_pipelines(spec: str) -> list[int]:
    out: list[int] = []
    for tok in spec.split():
        try:
            out.append(int(tok))
        except ValueError:
            die(f"bad PIPELINES token: {tok!r}")
    if not out:
        die("PIPELINES is empty")
    return out


def main() -> int:
    require_tools()
    pipelines = parse_pipelines(PIPELINES)

    print(f"==> ping Vemory {VEMORY_HOST}:{VEMORY_PORT}")
    check_ping(VEMORY_HOST, VEMORY_PORT, "Vemory")
    print(f"==> ping Redis  {REDIS_HOST}:{REDIS_PORT}")
    check_ping(REDIS_HOST, REDIS_PORT, "Redis")

    print()
    print(f"==> config: c=1 pipelines=({' '.join(str(p) for p in pipelines)}) r={R} d={D}")
    print(f"    n: p=1→{N_P1}; p=10,20→{N_MID}; p=40,100,160→{N_HIGH}")

    print()
    print(f"==> SET/GET baseline (redis-benchmark): c=1 p=1 n={N_P1}")
    v_set_base, v_get_base = bench_set_get(VEMORY_HOST, VEMORY_PORT, N_P1, 1)
    r_set_base, r_get_base = bench_set_get(REDIS_HOST, REDIS_PORT, N_P1, 1)
    print(f"  {'vemory':<8}  SET {v_set_base:>12} rps   GET {v_get_base:>12} rps")
    print(f"  {'redis':<8}  SET {r_set_base:>12} rps   GET {r_get_base:>12} rps")

    print()
    print("==> pipeline sweep: c=1")
    header = ("P", "n", "vemory_SET", "redis_SET", "vemory_GET", "redis_GET")
    print(
        f"{header[0]:<6}  {header[1]:<10}  "
        f"{header[2]:>14}  {header[3]:>14}  {header[4]:>14}  {header[5]:>14}"
    )
    print(
        f"{'------':<6}  {'----------':<10}  "
        f"{'--------------':>14}  {'--------------':>14}  "
        f"{'--------------':>14}  {'--------------':>14}"
    )

    for p in pipelines:
        n = n_for_p(p)
        v_set, v_get = bench_set_get(VEMORY_HOST, VEMORY_PORT, n, p)
        r_set, r_get = bench_set_get(REDIS_HOST, REDIS_PORT, n, p)
        print(
            f"{p:<6}  {n:<10}  "
            f"{v_set:>14}  {r_set:>14}  {v_get:>14}  {r_get:>14}"
        )

    print()
    print("done (c=1; compare SET/GET pipeline scaling of Vemory vs Redis)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
