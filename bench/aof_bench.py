#!/usr/bin/env python3
"""AOF QPS compare via redis-benchmark (c=1 P=1, same as rdb_save_bench baseline).

Measures:
  - ECHO on Vemory without AOF
  - SET/GET on Vemory aof=false
  - SET/GET on Vemory aof=true
  - SET/GET on Redis with appendonly yes

Requires three already-running servers and redis-benchmark / redis-cli on PATH.
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
VEMORY_AOF_HOST = os.environ.get("VEMORY_AOF_HOST", "127.0.0.1")
VEMORY_AOF_PORT = int(os.environ.get("VEMORY_AOF_PORT", "8990"))
REDIS_HOST = os.environ.get("REDIS_HOST", "127.0.0.1")
REDIS_PORT = int(os.environ.get("REDIS_PORT", "6379"))
N = int(os.environ.get("N", "100000"))
R = int(os.environ.get("R", "10000"))
D = int(os.environ.get("D", "64"))
ECHO_MSG = os.environ.get("ECHO_MSG", "hello")


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


def require_redis_aof(host: str, port: int) -> None:
    try:
        out = subprocess.check_output(
            [
                "redis-cli",
                "-2",
                "-h",
                host,
                "-p",
                str(port),
                "CONFIG",
                "GET",
                "appendonly",
            ],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        die(f"Redis CONFIG GET appendonly failed at {host}:{port} ({exc})")
    # Typical reply: "appendonly\nyes\n" or bulk lines
    lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
    if len(lines) >= 2 and lines[-1].lower() == "yes":
        return
    if "yes" in out.lower() and "appendonly" in out.lower():
        # Accept "appendonly\nyes" even if formatting varies
        for i, ln in enumerate(lines):
            if ln.lower() == "appendonly" and i + 1 < len(lines):
                if lines[i + 1].lower() == "yes":
                    return
    die(
        f"Redis at {host}:{port} does not have appendonly=yes "
        f"(got {out!r}); start with --appendonly yes"
    )


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

    reader = csv.reader(io.StringIO(raw))
    rows = list(reader)
    for row in rows[1:]:
        if len(row) >= 2 and row[1].strip():
            return row[1].strip()
    die(f"no RPS in redis-benchmark csv output: {raw!r}")


def bench_echo(host: str, port: int) -> str:
    return bench_rps(
        host,
        port,
        "-n",
        str(N),
        "-c",
        "1",
        "-P",
        "1",
        "ECHO",
        ECHO_MSG,
    )


def bench_set_get(host: str, port: int) -> tuple[str, str]:
    common = [
        "-n",
        str(N),
        "-c",
        "1",
        "-P",
        "1",
        "-r",
        str(R),
        "-d",
        str(D),
    ]
    set_rps = bench_rps(host, port, "-t", "set", *common)
    get_rps = bench_rps(host, port, "-t", "get", *common)
    return set_rps, get_rps


def main() -> None:
    require_tools()
    check_ping(VEMORY_HOST, VEMORY_PORT, "vemory_no_aof")
    check_ping(VEMORY_AOF_HOST, VEMORY_AOF_PORT, "vemory_aof")
    check_ping(REDIS_HOST, REDIS_PORT, "redis_aof")
    require_redis_aof(REDIS_HOST, REDIS_PORT)

    print(
        f"# AOF QPS  c=1 P=1 N={N} R={R} D={D} ECHO_MSG={ECHO_MSG!r}\n"
        f"# vemory_no_aof  {VEMORY_HOST}:{VEMORY_PORT}\n"
        f"# vemory_aof     {VEMORY_AOF_HOST}:{VEMORY_AOF_PORT}\n"
        f"# redis_aof      {REDIS_HOST}:{REDIS_PORT}"
    )
    print()

    echo_rps = bench_echo(VEMORY_HOST, VEMORY_PORT)
    print(f"ECHO (vemory_no_aof)  {echo_rps} rps")
    print()

    header = ("mode", "SET_rps", "GET_rps")
    print(f"{header[0]:<16}  {header[1]:>12}  {header[2]:>12}")

    targets = [
        ("vemory_no_aof", VEMORY_HOST, VEMORY_PORT),
        ("vemory_aof", VEMORY_AOF_HOST, VEMORY_AOF_PORT),
        ("redis_aof", REDIS_HOST, REDIS_PORT),
    ]
    for name, host, port in targets:
        set_rps, get_rps = bench_set_get(host, port)
        print(f"{name:<16}  {set_rps:>12}  {get_rps:>12}")

    print()
    print("done (c=1 P=1; compare AOF impact on SET/GET)")


if __name__ == "__main__":
    main()
