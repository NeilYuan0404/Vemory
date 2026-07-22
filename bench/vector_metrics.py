#!/usr/bin/env python3
"""Single-connection semantic-cache metrics: agree, p50/p99, QPS@agree>=0.95.

Loads a subsample of an ANN-Benchmarks HDF5 dataset into a running Vemory via
VSET, computes exact top-1 cosine oracle + distance gate, then times VGET.
"""

from __future__ import annotations

import os
import sys
import time
import urllib.request
from pathlib import Path
from typing import Optional

import h5py
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vemory_vec import connect, float_blob, vget, vset  # noqa: E402

HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "6379"))
DATASET = os.environ.get("DATASET", "glove-25-angular")
CARD = int(os.environ.get("CARD", "10000"))
QUERIES = int(os.environ.get("QUERIES", "200"))
THRESHOLD = float(os.environ.get("THRESHOLD", "0.2"))
DATA_DIR = Path(os.environ.get("DATA_DIR", Path(__file__).resolve().parent / "data"))
AGREE_GATE = 0.95
MAX_CARD = 65534
ANN_URLS = (
    "http://ann-benchmarks.com/{name}.hdf5",
    "http://vectors.erikbern.com/{name}.hdf5",
)
_USER_AGENT = "Vemory-bench/1.0 (+https://github.com/NeilYuan0404/Vemory)"


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def download_hdf5(name: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".partial")
    errors: list[str] = []

    def progress(block_num: int, block_size: int, total_size: int) -> None:
        if total_size <= 0:
            return
        done = min(block_num * block_size, total_size)
        pct = 100.0 * done / total_size
        mb = done / (1024 * 1024)
        total_mb = total_size / (1024 * 1024)
        print(f"\r    {pct:5.1f}%  {mb:.1f}/{total_mb:.1f} MiB", end="", file=sys.stderr)
        if done >= total_size:
            print(file=sys.stderr)

    for tmpl in ANN_URLS:
        url = tmpl.format(name=name)
        print(f"==> downloading {url}", file=sys.stderr)
        print(f"    -> {dest}", file=sys.stderr)
        try:
            req = urllib.request.Request(url, headers={"User-Agent": _USER_AGENT})
            with urllib.request.urlopen(req, timeout=120) as resp, open(
                tmp, "wb"
            ) as out:
                total = int(resp.headers.get("Content-Length", "0") or "0")
                chunk = 1024 * 256
                got = 0
                block = 0
                while True:
                    buf = resp.read(chunk)
                    if not buf:
                        break
                    out.write(buf)
                    got += len(buf)
                    block += 1
                    progress(block, chunk, total if total > 0 else got)
            tmp.replace(dest)
            return
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{url}: {exc}")
            if tmp.exists():
                tmp.unlink()

    die("download failed:\n  " + "\n  ".join(errors))


def resolve_dataset(name: str) -> Path:
    path = DATA_DIR / f"{name}.hdf5"
    if not path.is_file():
        download_hdf5(name, path)
    return path


def load_subset(path: Path, card: int, queries: int) -> tuple[np.ndarray, np.ndarray]:
    with h5py.File(path, "r") as f:
        if "train" not in f or "test" not in f:
            die(f"{path}: missing train/test datasets")
        train_n = f["train"].shape[0]
        test_n = f["test"].shape[0]
        if card > train_n:
            die(f"CARD={card} exceeds train size {train_n}")
        if queries > test_n:
            die(f"QUERIES={queries} exceeds test size {test_n}")
        train = np.asarray(f["train"][:card], dtype=np.float32)
        test = np.asarray(f["test"][:queries], dtype=np.float32)
    return train, test


def l2_normalize(x: np.ndarray, eps: float = 1e-12) -> np.ndarray:
    norms = np.linalg.norm(x, axis=1, keepdims=True)
    return x / np.maximum(norms, eps)


def cosine_top1(base: np.ndarray, queries: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Return (indices, distances) for exact top-1 cosine neighbor per query.

    distance = 1 - cosine_similarity (aligned with VGET threshold).
    """
    base_n = l2_normalize(base)
    q_n = l2_normalize(queries)
    sims = q_n @ base_n.T
    idx = np.argmax(sims, axis=1)
    row = np.arange(sims.shape[0])
    best_sim = sims[row, idx]
    dist = 1.0 - best_sim
    return idx.astype(np.int64), dist.astype(np.float64)


def element_name(i: int) -> str:
    return f"e{i}"


def main() -> int:
    if CARD < 1:
        die("CARD must be >= 1")
    if CARD > MAX_CARD:
        die(f"CARD must be <= {MAX_CARD} (uint16 element ids)")
    if QUERIES < 1:
        die("QUERIES must be >= 1")
    if THRESHOLD < 0:
        die("THRESHOLD must be >= 0")

    path = resolve_dataset(DATASET)
    print(f"==> loading {path} card={CARD} queries={QUERIES}", file=sys.stderr)
    train, test = load_subset(path, CARD, QUERIES)
    dim = int(train.shape[1])
    if test.shape[1] != dim:
        die(f"train/test dim mismatch: {dim} vs {test.shape[1]}")

    print("==> computing cosine top-1 oracle", file=sys.stderr)
    gt_idx, gt_dist = cosine_top1(train, test)
    expected: list[Optional[bytes]] = []
    for qi in range(QUERIES):
        if gt_dist[qi] <= THRESHOLD:
            expected.append(element_name(int(gt_idx[qi])).encode())
        else:
            expected.append(None)

    try:
        client = connect(HOST, PORT)
        if client.ping() is not True:
            die("PING failed")
    except Exception as exc:  # noqa: BLE001
        die(f"server not responding at {HOST}:{PORT} ({exc})")

    print(f"==> VSET dim={dim} card={CARD}", file=sys.stderr)
    t_load0 = time.perf_counter()
    for i in range(CARD):
        name = element_name(i)
        vset(client, float_blob(train[i]), name, f"q{i}", name)
        if (i + 1) % 1000 == 0 or i + 1 == CARD:
            print(f"\r    loaded {i + 1}/{CARD}", end="", file=sys.stderr)
    print(file=sys.stderr)
    load_s = time.perf_counter() - t_load0
    print(f"    VSET done in {load_s:.2f}s ({CARD / load_s:.1f} ops/s)", file=sys.stderr)

    print(f"==> VGET threshold={THRESHOLD} queries={QUERIES}", file=sys.stderr)
    latencies: list[float] = []
    agrees: list[float] = []
    for qi in range(QUERIES):
        blob = float_blob(test[qi])
        t0 = time.perf_counter()
        actual = vget(client, blob, THRESHOLD)
        t1 = time.perf_counter()
        latencies.append(t1 - t0)
        agrees.append(1.0 if actual == expected[qi] else 0.0)

    lat_ms = np.asarray(latencies, dtype=np.float64) * 1000.0
    agree = float(np.mean(agrees))
    p50 = float(np.percentile(lat_ms, 50))
    p99 = float(np.percentile(lat_ms, 99))
    total_s = float(np.sum(latencies))
    qps = (QUERIES / total_s) if total_s > 0 else float("inf")
    gate_ok = agree >= AGREE_GATE

    print(f"dataset            {DATASET}")
    print(f"card               {CARD}")
    print(f"queries            {QUERIES}")
    print(f"threshold          {THRESHOLD}")
    print(f"agree              {agree:.4f}")
    print(f"latency_p50_ms     {p50:.2f}")
    print(f"latency_p99_ms     {p99:.2f}")
    if gate_ok:
        print(f"qps@agree>=0.95    {qps:.1f}")
        return 0
    print("qps@agree>=0.95    N/A")
    print(
        f"error: agree {agree:.4f} < {AGREE_GATE} gate; QPS withheld",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
