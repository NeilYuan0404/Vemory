#!/usr/bin/env python3
"""Replication QPS compare via redis-benchmark (c=1 P=1, same as aof_bench).

Sequential on one master (avoids concurrent topologies fighting for CPU):

  1. ECHO + SET/GET with no replica connected
  2. Wait until a replica is synced
  3. SET/GET on the same master with the replica streaming

Requires a running master and redis-benchmark / redis-cli on PATH.
Does not start the master. Optionally starts the slave when AUTO_SLAVE=1.
"""

from __future__ import annotations

import csv
import io
import os
import shutil
import signal
import subprocess
import sys
import time
import uuid
from pathlib import Path

VEMORY_HOST = os.environ.get("VEMORY_HOST", "127.0.0.1")
VEMORY_PORT = int(os.environ.get("VEMORY_PORT", "8989"))
SLAVE_HOST = os.environ.get("SLAVE_HOST", "127.0.0.1")
SLAVE_PORT = int(os.environ.get("SLAVE_PORT", "8992"))
N = int(os.environ.get("N", "100000"))
R = int(os.environ.get("R", "10000"))
D = int(os.environ.get("D", "64"))
ECHO_MSG = os.environ.get("ECHO_MSG", "hello")
SYNC_TIMEOUT_S = float(os.environ.get("SYNC_TIMEOUT_S", "60"))
SYNC_POLL_S = float(os.environ.get("SYNC_POLL_S", "0.05"))
AUTO_SLAVE = os.environ.get("AUTO_SLAVE", "0").lower() not in (
    "0",
    "false",
    "no",
    "",
)
_REPO = Path(__file__).resolve().parent.parent
VEMORY_BIN = os.environ.get("VEMORY_BIN", str(_REPO / "bin" / "vemory"))

_slave_proc: subprocess.Popen[str] | None = None


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def require_tools() -> None:
    for name in ("redis-benchmark", "redis-cli"):
        if shutil.which(name) is None:
            die(f"{name} not found (install redis-tools)")


def ping_ok(host: str, port: int) -> bool:
    try:
        out = subprocess.check_output(
            ["redis-cli", "-2", "-h", host, "-p", str(port), "PING"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False
    return "PONG" in out.upper()


def check_ping(host: str, port: int, label: str) -> None:
    if not ping_ok(host, port):
        die(f"{label} not responding at {host}:{port}")


def require_no_slave() -> None:
    if ping_ok(SLAVE_HOST, SLAVE_PORT):
        die(
            f"slave already responding at {SLAVE_HOST}:{SLAVE_PORT}; "
            f"stop it before the no-repl phase "
            f"(sequential bench must not run both topologies at once)"
        )


def cli(host: str, port: int, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["redis-cli", "-2", "-h", host, "-p", str(port), *args],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        die(f"redis-cli failed at {host}:{port} ({exc})")


def wait_replica_synced() -> None:
    """SET on master; poll GET on slave until value matches (or timeout)."""
    key = f"replbench:{uuid.uuid4().hex}"
    value = f"ok-{uuid.uuid4().hex[:8]}"
    cli(VEMORY_HOST, VEMORY_PORT, "SET", key, value)

    got = ""
    deadline = time.monotonic() + SYNC_TIMEOUT_S
    while time.monotonic() < deadline:
        if not ping_ok(SLAVE_HOST, SLAVE_PORT):
            time.sleep(SYNC_POLL_S)
            continue
        try:
            got = subprocess.check_output(
                [
                    "redis-cli",
                    "-2",
                    "-h",
                    SLAVE_HOST,
                    "-p",
                    str(SLAVE_PORT),
                    "GET",
                    key,
                ],
                stderr=subprocess.DEVNULL,
                text=True,
            ).strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            time.sleep(SYNC_POLL_S)
            continue
        if got == value:
            return
        time.sleep(SYNC_POLL_S)

    die(
        f"replica not synced within {SYNC_TIMEOUT_S}s "
        f"(master {VEMORY_HOST}:{VEMORY_PORT} SET {key}={value!r}; "
        f"slave {SLAVE_HOST}:{SLAVE_PORT} last GET={got!r}). "
        f"Start with: {VEMORY_BIN} --slaveof {VEMORY_HOST} "
        f"{VEMORY_PORT} {SLAVE_PORT}"
    )


def start_slave() -> None:
    global _slave_proc
    if not Path(VEMORY_BIN).is_file():
        die(f"VEMORY_BIN not found: {VEMORY_BIN}")
    log_path = Path("/tmp/vemory_repl_bench_slave.log")
    logf = open(log_path, "w", encoding="utf-8")
    _slave_proc = subprocess.Popen(
        [
            VEMORY_BIN,
            "--slaveof",
            VEMORY_HOST,
            str(VEMORY_PORT),
            str(SLAVE_PORT),
        ],
        stdout=logf,
        stderr=subprocess.STDOUT,
        text=True,
    )
    print(
        f"# AUTO_SLAVE started pid={_slave_proc.pid} "
        f"--slaveof {VEMORY_HOST} {VEMORY_PORT} {SLAVE_PORT} "
        f"(log {log_path})",
        flush=True,
    )


def stop_slave() -> None:
    global _slave_proc
    if _slave_proc is None:
        return
    if _slave_proc.poll() is None:
        _slave_proc.send_signal(signal.SIGTERM)
        try:
            _slave_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _slave_proc.kill()
            _slave_proc.wait(timeout=2)
    _slave_proc = None


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
    check_ping(VEMORY_HOST, VEMORY_PORT, "vemory_master")
    require_no_slave()

    print(
        f"# Replication QPS  c=1 P=1 N={N} R={R} D={D} ECHO_MSG={ECHO_MSG!r}\n"
        f"# master  {VEMORY_HOST}:{VEMORY_PORT}  (sequential: no-repl then repl)\n"
        f"# slave   {SLAVE_HOST}:{SLAVE_PORT}  (attached after no-repl phase)",
        flush=True,
    )
    print(flush=True)

    header = ("mode", "SET_rps", "GET_rps")
    print(f"{header[0]:<16}  {header[1]:>12}  {header[2]:>12}", flush=True)

    echo_rps = bench_echo(VEMORY_HOST, VEMORY_PORT)
    print(f"ECHO (vemory_no_repl)  {echo_rps} rps", flush=True)

    set_rps, get_rps = bench_set_get(VEMORY_HOST, VEMORY_PORT)
    print(f"{'vemory_no_repl':<16}  {set_rps:>12}  {get_rps:>12}", flush=True)

    print(flush=True)
    if AUTO_SLAVE:
        start_slave()
    else:
        print(
            f"# start slave, then waiting up to {SYNC_TIMEOUT_S}s:\n"
            f"#   {VEMORY_BIN} --slaveof {VEMORY_HOST} {VEMORY_PORT} "
            f"{SLAVE_PORT}",
            flush=True,
        )

    try:
        wait_replica_synced()
        print("# replica synced", flush=True)
        print(flush=True)

        set_rps, get_rps = bench_set_get(VEMORY_HOST, VEMORY_PORT)
        print(f"{'vemory_repl':<16}  {set_rps:>12}  {get_rps:>12}", flush=True)
    finally:
        if AUTO_SLAVE:
            stop_slave()

    print(flush=True)
    print(
        "done (c=1 P=1; sequential no-repl then repl on one master)",
        flush=True,
    )


if __name__ == "__main__":
    main()
