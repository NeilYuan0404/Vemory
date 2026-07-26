#!/usr/bin/env python3
"""tcmalloc on/off memory compare: SET insert then DEL clear, sample RSS/VIRT.

Builds (or uses) two binaries, starts each on a private port, loads N string
keys via redis-py pipeline, deletes them, and prints a Markdown table matching
the 9.3 memory-pool style report.

Requires Linux/WSL (/proc/<pid>/status), redis-py, and (when BUILD=1 + TCMALLOC)
libtcmalloc_minimal for the on side.
"""

from __future__ import annotations

import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

try:
    import redis
except ImportError:
    print(
        "error: redis-py not found (use bench/.venv after "
        "pip install -r bench/requirements.txt)",
        file=sys.stderr,
    )
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[1]
HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "16380"))
N = int(os.environ.get("N", "300000"))
D = int(os.environ.get("D", "64"))
SAMPLE_MS = int(os.environ.get("SAMPLE_MS", "50"))
SETTLE_S = float(os.environ.get("SETTLE_S", "0.5"))
BUILD = os.environ.get("BUILD", "1") not in ("0", "false", "False", "no")
PIPE = int(os.environ.get("PIPE", "1000"))
READY_TIMEOUT_S = float(os.environ.get("READY_TIMEOUT_S", "10"))
BIN_TCMALLOC = os.environ.get("BIN_TCMALLOC", str(ROOT / "bin" / "vemory.tcmalloc"))
BIN_SYS = os.environ.get("BIN_SYS", str(ROOT / "bin" / "vemory.sys"))
MAKE = os.environ.get("MAKE", "make")


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def kb_to_mb(kb: int) -> float:
    return kb / 1024.0


@dataclass
class MemSample:
    rss_mb: float
    virt_mb: float


def read_mem(pid: int) -> MemSample:
    path = Path(f"/proc/{pid}/status")
    rss_kb: Optional[int] = None
    virt_kb: Optional[int] = None
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        die(f"cannot read {path}: {exc}")
    for line in text.splitlines():
        if line.startswith("VmRSS:"):
            rss_kb = int(line.split()[1])
        elif line.startswith("VmSize:"):
            virt_kb = int(line.split()[1])
    if rss_kb is None or virt_kb is None:
        die(f"VmRSS/VmSize missing in {path}")
    return MemSample(kb_to_mb(rss_kb), kb_to_mb(virt_kb))


class PeakSampler:
    def __init__(self, pid: int, interval_s: float) -> None:
        self._pid = pid
        self._interval_s = interval_s
        self._stop = threading.Event()
        self.peak_rss_mb = 0.0
        self.peak_virt_mb = 0.0
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        m = read_mem(self._pid)
        self.peak_rss_mb = m.rss_mb
        self.peak_virt_mb = m.virt_mb
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=5)
        m = read_mem(self._pid)
        self.peak_rss_mb = max(self.peak_rss_mb, m.rss_mb)
        self.peak_virt_mb = max(self.peak_virt_mb, m.virt_mb)

    def _run(self) -> None:
        while not self._stop.wait(self._interval_s):
            try:
                m = read_mem(self._pid)
            except SystemExit:
                return
            except Exception:
                continue
            if m.rss_mb > self.peak_rss_mb:
                self.peak_rss_mb = m.rss_mb
            if m.virt_mb > self.peak_virt_mb:
                self.peak_virt_mb = m.virt_mb


def ensure_local_gperftools_env() -> None:
    """If ~/.local has tcmalloc, prepend PKG_CONFIG_PATH / LD_LIBRARY_PATH."""
    local = Path.home() / ".local"
    pc = local / "lib" / "pkgconfig"
    lib = local / "lib"
    if (pc / "libtcmalloc_minimal.pc").is_file() or list(
        lib.glob("libtcmalloc_minimal.so*")
    ):
        os.environ["PKG_CONFIG_PATH"] = (
            str(pc)
            + (os.pathsep + os.environ["PKG_CONFIG_PATH"]
               if os.environ.get("PKG_CONFIG_PATH")
               else "")
        )
        os.environ["LD_LIBRARY_PATH"] = (
            str(lib)
            + (os.pathsep + os.environ["LD_LIBRARY_PATH"]
               if os.environ.get("LD_LIBRARY_PATH")
               else "")
        )


def build_bins() -> None:
    ensure_local_gperftools_env()
    bin_dir = ROOT / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    main_bin = bin_dir / "vemory"

    def run_make(tcmalloc: str) -> None:
        # LDFLAGS differ by TCMALLOC; force relink (object files unchanged).
        if main_bin.is_file():
            main_bin.unlink()
        cmd = [MAKE, f"TCMALLOC={tcmalloc}", "-j", str(os.cpu_count() or 2)]
        print(f"# {' '.join(cmd)}", flush=True)
        try:
            subprocess.check_call(cmd, cwd=ROOT)
        except subprocess.CalledProcessError as exc:
            die(f"make TCMALLOC={tcmalloc} failed ({exc})")
        if not main_bin.is_file():
            die(f"expected binary missing: {main_bin}")

    run_make("1")
    shutil.copy2(main_bin, BIN_TCMALLOC)
    run_make("0")
    shutil.copy2(main_bin, BIN_SYS)
    print(f"# wrote {BIN_TCMALLOC} and {BIN_SYS}", flush=True)


def write_ini(path: Path, port: int, kv_reserve: int) -> None:
    path.write_text(
        "\n".join(
            [
                "[server]",
                f"port = {port}",
                "bind = 127.0.0.1",
                "[logging]",
                "level = warn",
                "[storage]",
                f"kv_reserve = {kv_reserve}",
                "[index]",
                "default_capacity = 16",
                "[persistence]",
                "dir =",
                "load_on_startup = false",
                "aof = false",
                "",
            ]
        ),
        encoding="utf-8",
    )


def make_client(host: str, port: int, **kwargs: object) -> redis.Redis:
    # Vemory is RESP2-only; redis-py 5+ may send HELLO unless protocol=2.
    return redis.Redis(host=host, port=port, protocol=2, **kwargs)  # type: ignore[arg-type]


def wait_ready(host: str, port: int, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    last_err = ""
    while time.time() < deadline:
        try:
            r = make_client(host, port, socket_connect_timeout=0.5)
            if r.ping():
                return
        except Exception as exc:  # noqa: BLE001 — readiness poll
            last_err = str(exc)
            time.sleep(0.05)
    die(f"server not ready at {host}:{port} within {timeout_s}s ({last_err})")


def start_server(bin_path: str, ini_path: Path, log_path: Path) -> subprocess.Popen[bytes]:
    if not Path(bin_path).is_file():
        die(f"binary not found: {bin_path}")
    ensure_local_gperftools_env()
    logf = open(log_path, "wb")
    proc = subprocess.Popen(
        [bin_path, "-c", str(ini_path)],
        stdout=logf,
        stderr=subprocess.STDOUT,
        cwd=ROOT,
        env=os.environ.copy(),
    )
    return proc


def stop_server(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)


def pipeline_set(r: redis.Redis, n: int, value: bytes, pipe: int) -> None:
    pipe_batch = r.pipeline(transaction=False)
    for i in range(n):
        pipe_batch.set(f"k:{i}", value)
        if (i + 1) % pipe == 0:
            pipe_batch.execute()
            pipe_batch = r.pipeline(transaction=False)
    if n % pipe != 0:
        pipe_batch.execute()


def pipeline_del(r: redis.Redis, n: int, pipe: int) -> None:
    pipe_batch = r.pipeline(transaction=False)
    for i in range(n):
        pipe_batch.delete(f"k:{i}")
        if (i + 1) % pipe == 0:
            pipe_batch.execute()
            pipe_batch = r.pipeline(transaction=False)
    if n % pipe != 0:
        pipe_batch.execute()


@dataclass
class ModeResult:
    label: str
    initial: MemSample
    peak: MemSample
    final: MemSample


def run_mode(label: str, bin_path: str, port: int) -> ModeResult:
    value = b"x" * D
    kv_reserve = max(N, 100000)
    with tempfile.TemporaryDirectory(prefix="vemory_tcmalloc_mem_") as tmp:
        tmp_path = Path(tmp)
        ini_path = tmp_path / "vemory.ini"
        log_path = tmp_path / "server.log"
        write_ini(ini_path, port, kv_reserve)
        proc = start_server(bin_path, ini_path, log_path)
        try:
            wait_ready(HOST, port, READY_TIMEOUT_S)
            if proc.pid is None:
                die("server pid missing")
            initial = read_mem(proc.pid)
            sampler = PeakSampler(proc.pid, SAMPLE_MS / 1000.0)
            sampler.start()
            r = make_client(HOST, port, decode_responses=False)
            print(f"# {label}: SET N={N} D={D} …", flush=True)
            pipeline_set(r, N, value, PIPE)
            sampler.stop()
            peak = MemSample(sampler.peak_rss_mb, sampler.peak_virt_mb)
            print(f"# {label}: DEL N={N} …", flush=True)
            pipeline_del(r, N, PIPE)
            time.sleep(SETTLE_S)
            final = read_mem(proc.pid)
            return ModeResult(label, initial, peak, final)
        finally:
            stop_server(proc)
            # Keep log on failure for debugging
            if proc.returncode not in (0, -signal.SIGTERM, None):
                try:
                    print(
                        f"# server log ({log_path}):\n"
                        f"{log_path.read_text(encoding='utf-8', errors='replace')}",
                        file=sys.stderr,
                    )
                except OSError:
                    pass


def print_table(rows: list[ModeResult]) -> None:
    headers = (
        "模式",
        "初始 RSS (MB)",
        "初始 VIRT (MB)",
        "插入峰值 RSS (MB)",
        "插入峰值 VIRT (MB)",
        "清空后最终 RSS (MB)",
        "清空后最终 VIRT (MB)",
    )
    print()
    print("| " + " | ".join(headers) + " |")
    print("| " + " | ".join(["---"] * len(headers)) + " |")
    for row in rows:
        cells = [
            row.label,
            f"{row.initial.rss_mb:.2f}",
            f"{row.initial.virt_mb:.2f}",
            f"{row.peak.rss_mb:.2f}",
            f"{row.peak.virt_mb:.2f}",
            f"{row.final.rss_mb:.2f}",
            f"{row.final.virt_mb:.2f}",
        ]
        print("| " + " | ".join(cells) + " |")
    print()
    print(
        "mode,initial_rss_mb,initial_virt_mb,peak_rss_mb,peak_virt_mb,"
        "final_rss_mb,final_virt_mb"
    )
    for row in rows:
        print(
            f"{row.label},{row.initial.rss_mb:.2f},{row.initial.virt_mb:.2f},"
            f"{row.peak.rss_mb:.2f},{row.peak.virt_mb:.2f},"
            f"{row.final.rss_mb:.2f},{row.final.virt_mb:.2f}"
        )


def main() -> None:
    if not Path("/proc/self/status").is_file():
        die("needs Linux/WSL /proc for RSS/VIRT sampling")
    if N <= 0 or D <= 0 or PIPE <= 0:
        die("N, D, and PIPE must be positive")

    if BUILD:
        build_bins()
    else:
        ensure_local_gperftools_env()
        for p in (BIN_TCMALLOC, BIN_SYS):
            if not Path(p).is_file():
                die(f"binary not found: {p} (set BUILD=1 or BIN_*)")

    rows = [
        run_mode("tcmalloc 开启", BIN_TCMALLOC, PORT),
        run_mode("tcmalloc 关闭", BIN_SYS, PORT + 1),
    ]
    if rows[0].peak.rss_mb <= rows[0].initial.rss_mb:
        print(
            "warning: tcmalloc peak RSS did not exceed initial "
            f"({rows[0].peak.rss_mb:.2f} <= {rows[0].initial.rss_mb:.2f}); "
            "try larger N",
            file=sys.stderr,
        )
    print_table(rows)


if __name__ == "__main__":
    main()
