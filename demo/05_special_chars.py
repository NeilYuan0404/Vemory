#!/usr/bin/env python3
"""Demo 5 — special characters in string KVS (binary-safe bulk strings).

  # terminal A
  ./bin/vemory -c demo/vemory.demo.ini

  # terminal B
  python3 demo/05_special_chars.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import HOST, PORT, die, require_server, unique_key  # noqa: E402

# Keys/values exercise UTF-8, whitespace, quotes, escapes, and embedded NUL.
CASES: list[tuple[str, bytes]] = [
    ("ascii", b"plain ascii"),
    ("utf8", "中文 🌧️ café".encode()),
    ("newline", b"line1\nline2\r\nline3"),
    ("tab", b"col1\tcol2\tcol3"),
    ("quotes", b'single \' and double " quotes'),
    ("backslash", b"path\\to\\file"),
    ("null", b"before\x00after"),
    ("mixed", "混合\n\t\x00end".encode()),
]


def main() -> int:
    r = require_server()
    print(f"==> special-char demo  {HOST}:{PORT}  cases={len(CASES)}")

    prefix = unique_key("special")
    print("\n1) SET keys with special bytes")
    for name, val in CASES:
        key = f"{prefix}:{name}"
        r.set(key, val)
        print(f"  SET {key!r}  len={len(val)}  preview={val[:32]!r}")

    print("\n2) GET round-trip")
    for name, expect in CASES:
        key = f"{prefix}:{name}"
        got = r.get(key)
        if got != expect:
            die(f"GET {key!r}: expected {expect!r}, got {got!r}")
        print(f"  OK  {name}: len={len(got)}")

    print("\n3) overwrite with another special value")
    key = f"{prefix}:overwrite"
    first = b"v1:\x00\xff\xfe"
    second = "更新 ✓".encode()
    r.set(key, first)
    r.set(key, second)
    got = r.get(key)
    if got != second:
        die(f"overwrite failed: expected {second!r}, got {got!r}")
    print(f"  OK  {key} → {got!r}")

    print("\ndone (all special-char round-trips OK)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
