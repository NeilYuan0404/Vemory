#!/usr/bin/env python3
"""Measure replica fullsync RDB receive+load (mmap vs stdio binaries).

Build both:
  make -j$(nproc)
  make -j$(nproc) RDB_MMAP=0

Then compare:
  VEMORY_BIN=./bin/vemory CARD=10000 python bench/repl_fullsync_load_bench.py
  VEMORY_BIN=./bin/vemory-stdio CARD=10000 python bench/repl_fullsync_load_bench.py

mmap binary: --slaveof enables USEARCH view. stdio binary: always copy-load.
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vemory_vec import connect, float_blob, vget, vset  # noqa: E402

_REPO = Path(__file__).resolve().parent.parent
VEMORY_BIN = str(
    Path(os.environ.get("VEMORY_BIN", str(_REPO / "bin" / "vemory"))).resolve()
)
CARD = int(os.environ.get("CARD", "10000"))
DIM = int(os.environ.get("DIM", "64"))
MASTER_PORT = int(os.environ.get("MASTER_PORT", "18990"))
SLAVE_PORT = int(os.environ.get("SLAVE_PORT", "18991"))
HOST = os.environ.get("HOST", "127.0.0.1")
SYNC_TIMEOUT_S = float(os.environ.get("SYNC_TIMEOUT_S", "120"))

_LOAD_RE = re.compile(
    r"RDB LoadFromPath ok .* mmap=(\d+) view=(\w+) load_ms=(\d+)"
)


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def synth_vec(dim: int, seed: int) -> list[float]:
    return [(seed + d + 1) / (dim + seed + 1) for d in range(dim)]


def write_master_ini(path: Path, data_dir: Path) -> None:
    path.write_text(
        "\n".join(
            [
                "[server]",
                f"port = {MASTER_PORT}",
                "bind = 127.0.0.1",
                "[logging]",
                "level = info",
                "[index]",
                "default_capacity = 16384",
                "[persistence]",
                f"dir = {data_dir}",
                "load_on_startup = false",
                "aof = false",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_slave_ini(path: Path, data_dir: Path) -> None:
    path.write_text(
        "\n".join(
            [
                "[server]",
                f"port = {SLAVE_PORT}",
                "bind = 127.0.0.1",
                "[logging]",
                "level = info",
                "[index]",
                "default_capacity = 16384",
                "[persistence]",
                f"dir = {data_dir}",
                "load_on_startup = false",
                "aof = false",
                "",
            ]
        ),
        encoding="utf-8",
    )


def start(bin_args: list[str], log_path: Path, cwd: Path) -> subprocess.Popen[str]:
    log_f = open(log_path, "w", encoding="utf-8")  # noqa: SIM115
    proc = subprocess.Popen(
        bin_args,
        stdout=log_f,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=str(cwd),
    )
    proc._log_f = log_f  # type: ignore[attr-defined]
    return proc


def stop(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    log_f = getattr(proc, "_log_f", None)
    if log_f is not None:
        log_f.close()


def wait_ping(port: int, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            if connect(HOST, port).ping() is True:
                return
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.05)
    die(f"no PONG on {HOST}:{port}")


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
        die(f"missing {VEMORY_BIN}")

    with tempfile.TemporaryDirectory(prefix="vemory_fullsync_") as tmp:
        tmp_path = Path(tmp)
        mdata = tmp_path / "mdata"
        sdata = tmp_path / "sdata"
        mdata.mkdir()
        sdata.mkdir()
        mini = tmp_path / "master.ini"
        sini = tmp_path / "slave.ini"
        write_master_ini(mini, mdata)
        write_slave_ini(sini, sdata)

        mlog = tmp_path / "master.log"
        master = start([VEMORY_BIN, "-c", str(mini)], mlog, tmp_path)
        try:
            wait_ping(MASTER_PORT, 20)
            mc = connect(HOST, MASTER_PORT)
            for i in range(CARD):
                vset(
                    mc,
                    float_blob(synth_vec(DIM, i)),
                    f"k{i}",
                    f"q{i}",
                    f"a{i}",
                )

            slog = tmp_path / "slave.log"
            t0 = time.time()
            slave = start(
                [
                    VEMORY_BIN,
                    "-c",
                    str(sini),
                    "--slaveof",
                    HOST,
                    str(MASTER_PORT),
                ],
                slog,
                tmp_path,
            )
            try:
                wait_ping(SLAVE_PORT, SYNC_TIMEOUT_S)
                # Wait until a key is visible on the slave.
                sc = connect(HOST, SLAVE_PORT)
                deadline = time.time() + SYNC_TIMEOUT_S
                synced = False
                while time.time() < deadline:
                    try:
                        ans = vget(sc, float_blob(synth_vec(DIM, 0)), 0.5)
                        if ans == b"a0":
                            synced = True
                            break
                    except Exception:  # noqa: BLE001
                        pass
                    time.sleep(0.05)
                if not synced:
                    die("replica did not sync within timeout")
                total_ms = int((time.time() - t0) * 1000)
                time.sleep(0.2)
                text = slog.read_text(encoding="utf-8", errors="replace")
                m = None
                for m in _LOAD_RE.finditer(text):
                    pass
                if m is None:
                    die(f"RDB LoadFromPath log not found in {slog}")
                mmap_flag, view_flag, load_ms = (
                    m.group(1),
                    m.group(2),
                    int(m.group(3)),
                )
                rss, virt = proc_rss_virt_mb(slave.pid)
                print(
                    f"card={CARD} dim={DIM} bin={VEMORY_BIN} "
                    f"mmap={mmap_flag} view={view_flag} "
                    f"rdb_load_ms={load_ms} fullsync_total_ms={total_ms} "
                    f"slave_rss_mb={rss:.1f} slave_virt_mb={virt:.1f}"
                )
            finally:
                stop(slave)
        finally:
            stop(master)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        die("interrupted", 130)
