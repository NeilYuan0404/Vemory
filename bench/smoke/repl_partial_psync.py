#!/usr/bin/env python3
"""Mini PSYNC client: fullsync once, disconnect, reconnect with offset → CONTINUE."""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RuntimeError(f"EOF expecting {n} bytes, got {len(buf)}")
        buf.extend(chunk)
    return bytes(buf)


def recv_until(sock: socket.socket, delim: bytes) -> bytes:
    buf = bytearray()
    while delim not in buf:
        chunk = sock.recv(1)
        if not chunk:
            raise RuntimeError("EOF in recv_until")
        buf.extend(chunk)
    return bytes(buf)


def read_simple(sock: socket.socket) -> str:
    line = recv_until(sock, b"\r\n")
    if not line.startswith(b"+"):
        raise RuntimeError(f"expected simple string, got {line!r}")
    return line[1:-2].decode("utf-8", errors="replace")


def read_bulk_header(sock: socket.socket) -> int:
    line = recv_until(sock, b"\r\n")
    if not line.startswith(b"$"):
        raise RuntimeError(f"expected bulk header, got {line!r}")
    return int(line[1:-2])


def encode_psync(replid: str | None, offset: int | None) -> bytes:
    if replid is None or offset is None:
        parts = [b"PSYNC", b"?", b"-1"]
    else:
        parts = [b"PSYNC", replid.encode(), str(offset).encode()]
    out = f"*{len(parts)}\r\n".encode()
    for p in parts:
        out += f"${len(p)}\r\n".encode() + p + b"\r\n"
    return out


def fullsync(sock: socket.socket) -> tuple[str, int]:
    sock.sendall(encode_psync(None, None))
    reply = read_simple(sock)
    if not reply.startswith("FULLRESYNC "):
        raise RuntimeError(f"expected FULLRESYNC, got {reply!r}")
    _, replid, cut_s = reply.split(" ", 2)
    cut = int(cut_s)

    rdb_n = read_bulk_header(sock)
    if rdb_n > 0:
        recv_exact(sock, rdb_n)

    backlog_n = read_bulk_header(sock)
    if backlog_n > 0:
        recv_exact(sock, backlog_n)

    offset = cut + backlog_n
    return replid, offset


def expect_continue(sock: socket.socket, replid: str, offset: int) -> int:
    sock.sendall(encode_psync(replid, offset))
    reply = read_simple(sock)
    if reply != "CONTINUE":
        raise RuntimeError(f"expected CONTINUE, got {reply!r}")
    # Drain any catch-up frames for a short window (non-blocking-ish).
    sock.settimeout(0.5)
    drained = 0
    try:
        while True:
            hdr = sock.recv(4)
            if not hdr:
                break
            if len(hdr) < 4:
                hdr += recv_exact(sock, 4 - len(hdr))
            (plen,) = struct.unpack("<I", hdr)
            if plen:
                recv_exact(sock, plen)
            drained += 4 + plen
    except socket.timeout:
        pass
    finally:
        sock.settimeout(None)
    return drained


def redis_set(host: str, port: int, key: str, val: str) -> None:
    # Minimal RESP SET via plain socket (avoid redis-cli dependency in helper).
    payload = (
        b"*3\r\n$3\r\nSET\r\n"
        + f"${len(key)}\r\n".encode()
        + key.encode()
        + b"\r\n"
        + f"${len(val)}\r\n".encode()
        + val.encode()
        + b"\r\n"
    )
    with socket.create_connection((host, port), timeout=5) as c:
        c.sendall(payload)
        line = recv_until(c, b"\r\n")
        if line != b"+OK\r\n":
            raise RuntimeError(f"SET failed: {line!r}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=16379)
    args = ap.parse_args()

    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        replid, offset = fullsync(sock)
        print(f"  fullsync ok replid={replid[:8]}… offset={offset}", flush=True)

    key = f"partial:{int(time.time() * 1e9)}"
    val = f"v-{int(time.time())}"
    redis_set(args.host, args.port, key, val)
    print(f"  wrote {key}={val} while mini-slave down", flush=True)

    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        drained = expect_continue(sock, replid, offset)
        print(f"  CONTINUE ok catchup_bytes≈{drained}", flush=True)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)
