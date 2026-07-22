"""Shared helpers for binary VSET / VGET / VDEL benches (redis-py, RESP2)."""

from __future__ import annotations

import struct
from typing import Iterable, Optional, Sequence, Union

import redis

FloatSeq = Union[Sequence[float], Iterable[float]]


def float_blob(vec: FloatSeq) -> bytes:
    """Pack floats as little-endian float32 raw bytes."""
    vals = list(vec)
    return struct.pack(f"<{len(vals)}f", *vals)


def connect(host: str, port: int) -> redis.Redis:
    # decode_responses=False: vector blobs are binary.
    # protocol=2: Vemory has no HELLO/RESP3.
    return redis.Redis(host=host, port=port, decode_responses=False, protocol=2)


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
    """Return answer bytes on hit, None on miss / null bulk."""
    reply = client.execute_command("VGET", blob, f"{threshold:.9g}")
    if reply is None:
        return None
    if isinstance(reply, str):
        return reply.encode()
    return bytes(reply)


def vdel(client: redis.Redis, user_key: str) -> int:
    reply = client.execute_command("VDEL", user_key)
    return int(reply)
