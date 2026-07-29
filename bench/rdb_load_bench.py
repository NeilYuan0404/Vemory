#!/usr/bin/env python3
"""Measure RDB LoadFromPath time via server logs (mmap vs stdio binaries).

Build both:
  make -j$(nproc)
  make -j$(nproc) RDB_MMAP=0

Then:
  VEMORY_BIN=./bin/vemory CARD=10000 ITERS=5 python bench/rdb_load_bench.py
  VEMORY_BIN=./bin/vemory-stdio CARD=10000 ITERS=5 python bench/rdb_load_bench.py

Env: CARD, DIM, ITERS, PORT, VEMORY_BIN
"""

from __future__ import annotations

import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vemory_vec import connect, float_blob, save, vset  # noqa: E402

_REPO = Path(__file__).resolve().parent.parent
VEMORY_BIN = str(
    Path(os.environ.get("VEMORY_BIN", str(_REPO / "bin" / "vemory"))).resolve()
)
CARD = int(os.environ.get("CARD", "10000"))
DIM = int(os.environ.get("DIM", "64"))
ITERS = int(os.environ.get("ITERS", "5"))
PORT = int(os.environ.get("PORT", "18989"))
HOST = os.environ.get("HOST", "127.0.0.1")
SAVE_TIMEOUT_S = float(os.environ.get("SAVE_TIMEOUT_S", "120"))
LOAD_TIMEOUT_S = float(os.environ.get("LOAD_TIMEOUT_S", "120"))

_LOAD_RE = re.compile(r"load_ms=(\d+)")


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def synth_vec(dim: int, seed: int) -> list[float]:
    return [(seed + d + 1) / (dim + seed + 1) for d in range(dim)]


def write_ini(path: Path, data_dir: Path, load_on_startup: bool) -> None:
    path.write_text(
        "\n".join(
            [
                "[server]",
                f"port = {PORT}",
                "bind = 127.0.0.1",
                "[logging]",
                "level = info",
                "[index]",
                "default_capacity = 16384",
                "[persistence]",
                f"dir = {data_dir}",
                f"load_on_startup = {'true' if load_on_startup else 'false'}",
                "aof = false",
                "",
            ]
        ),
        encoding="utf-8",
    )


def start_server(ini: Path, log_path: Path) -> subprocess.Popen[str]:
    log_f = open(log_path, "w", encoding="utf-8")  # noqa: SIM115
    proc = subprocess.Popen(
        [VEMORY_BIN, "-c", str(ini)],
        stdout=log_f,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=str(_REPO),
    )
    proc._log_f = log_f  # type: ignore[attr-defined]
    return proc


def stop_server(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    log_f = getattr(proc, "_log_f", None)
    if log_f is not None:
        log_f.close()


def wait_ping(timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            c = connect(HOST, PORT)
            if c.ping() is True:
                return
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.05)
    die(f"server not up on {HOST}:{PORT} within {timeout_s}s")


def wait_rdb(data_dir: Path, timeout_s: float) -> None:
    rdb = data_dir / "dump.rdb"
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if rdb.is_file() and rdb.stat().st_size > 0:
            return
        time.sleep(0.05)
    die(f"dump.rdb not ready under {data_dir}")


def parse_load_ms(log_path: Path) -> int | None:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    matches = _LOAD_RE.findall(text)
    if not matches:
        return None
    return int(matches[-1])


def proc_rss_virt_mb(pid: int) -> tuple[float, float]:
    status = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
    rss = virt = 0.0
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            rss = int(line.split()[1]) / 1024.0
        elif line.startswith("VmSize:"):
            virt = int(line.split()[1]) / 1024.0
    return rss, virt


def main() -> int:
    if not Path(VEMORY_BIN).is_file():
        die(f"vemory binary not found: {VEMORY_BIN} (make first)")
    if CARD < 1 or DIM < 1 or ITERS < 1:
        die("CARD, DIM, ITERS must be >= 1")

    with tempfile.TemporaryDirectory(prefix="vemory_rdb_load_") as tmp:
        tmp_path = Path(tmp)
        data_dir = tmp_path / "data"
        data_dir.mkdir()
        ini_fill = tmp_path / "fill.ini"
        ini_load = tmp_path / "load.ini"
        write_ini(ini_fill, data_dir, load_on_startup=False)
        write_ini(ini_load, data_dir, load_on_startup=True)

        log_fill = tmp_path / "fill.log"
        proc = start_server(ini_fill, log_fill)
        try:
            wait_ping(15)
            client = connect(HOST, PORT)
            for i in range(CARD):
                blob = float_blob(synth_vec(DIM, i))
                vset(client, blob, f"k{i}", f"q{i}", f"a{i}")
            save(client)
            wait_rdb(data_dir, SAVE_TIMEOUT_S)
            rdb_size = (data_dir / "dump.rdb").stat().st_size
        finally:
            stop_server(proc)

        print(
            f"bin={VEMORY_BIN} rdb_bytes={rdb_size} card={CARD} dim={DIM} "
            f"iters={ITERS}"
        )
        loads: list[int] = []
        rss_list: list[float] = []
        virt_list: list[float] = []

        for i in range(ITERS):
            log_load = tmp_path / f"load_{i}.log"
            proc = start_server(ini_load, log_load)
            try:
                wait_ping(LOAD_TIMEOUT_S)
                time.sleep(0.1)  # let load log flush
                ms = parse_load_ms(log_load)
                if ms is None:
                    die(f"load_ms not found in {log_load}")
                loads.append(ms)
                rss, virt = proc_rss_virt_mb(proc.pid)
                rss_list.append(rss)
                virt_list.append(virt)
                print(f"iter={i} load_ms={ms} rss_mb={rss:.1f} virt_mb={virt:.1f}")
            finally:
                stop_server(proc)

        print(
            f"load_ms_p50={statistics.median(loads):.0f} "
            f"load_ms_mean={statistics.mean(loads):.1f} "
            f"rss_mb_mean={statistics.mean(rss_list):.1f} "
            f"virt_mb_mean={statistics.mean(virt_list):.1f}"
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        die("interrupted", 130)
