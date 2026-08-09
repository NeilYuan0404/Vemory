"""Shared helpers for Vemory demos (redis-py, RESP2)."""

from __future__ import annotations

import os
import struct
import sys
import time
import uuid
from typing import Iterable, Optional, Sequence, Union

import redis

FloatSeq = Union[Sequence[float], Iterable[float]]

HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "6379"))
SLAVE_HOST = os.environ.get("SLAVE_HOST", HOST)
SLAVE_PORT = int(os.environ.get("SLAVE_PORT", "6380"))
SYNC_TIMEOUT_S = float(os.environ.get("SYNC_TIMEOUT_S", "30"))


def die(msg: str, code: int = 1) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def float_blob(vec: FloatSeq) -> bytes:
    vals = list(vec)
    return struct.pack(f"<{len(vals)}f", *vals)


def connect(host: str = HOST, port: int = PORT) -> redis.Redis:
    # decode_responses=False: vector blobs are binary.
    # protocol=2: Vemory has no HELLO/RESP3.
    return redis.Redis(host=host, port=port, decode_responses=False, protocol=2)


def require_server(host: str = HOST, port: int = PORT) -> redis.Redis:
    try:
        client = connect(host, port)
        if client.ping() is not True:
            die(f"PING failed at {host}:{port}")
        return client
    except Exception as exc:  # noqa: BLE001
        die(f"server not responding at {host}:{port} ({exc})")


def wait_get(
    client: redis.Redis,
    key: str,
    expect: bytes,
    *,
    label: str = "GET",
    timeout_s: float = SYNC_TIMEOUT_S,
) -> None:
    deadline = time.time() + timeout_s
    got = None
    while time.time() < deadline:
        got = client.get(key)
        if got == expect:
            print(f"  {label}: GET {key} → {got!r}")
            return
        time.sleep(0.05)
    die(f"{label} failed within {timeout_s}s (expected {expect!r}, last GET={got!r})")


def unique_key(prefix: str) -> str:
    return f"demo:{prefix}:{uuid.uuid4().hex[:8]}"


def vset(
    client: redis.Redis,
    blob: bytes,
    user_key: str,
    question: str,
    answer: str,
) -> None:
    reply = client.execute_command("VSET", blob, user_key, question, answer)
    if reply != b"OK":
        raise RuntimeError(f"VSET unexpected reply: {reply!r}")


def vget(client: redis.Redis, blob: bytes, threshold: float) -> Optional[bytes]:
    reply = client.execute_command("VGET", blob, f"{threshold:.9g}")
    if reply is None:
        return None
    if isinstance(reply, str):
        return reply.encode()
    return bytes(reply)


def vdel(client: redis.Redis, user_key: str) -> int:
    return int(client.execute_command("VDEL", user_key))


def save(client: redis.Redis) -> None:
    reply = client.execute_command("SAVE")
    # redis-py may map +OK → True (same as SET).
    if reply not in (b"OK", True, "OK"):
        raise RuntimeError(f"SAVE unexpected reply: {reply!r}")
