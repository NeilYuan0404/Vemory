#!/usr/bin/env python3
"""Demo 2 — semantic cache vectors (VSET / VGET / VDEL).

Vectors are raw little-endian float32 blobs. VGET uses cosine distance ≤ threshold.

  # terminal A
  ./bin/vemory -c demo/vemory.demo.ini

  # terminal B
  python3 demo/02_vector.py
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import HOST, PORT, die, float_blob, require_server, vdel, vget, vset  # noqa: E402

# Tiny hand-written vectors (dim=4). Same direction → hit; orthogonal → miss.
ENTRIES = [
    ("weather", [1.0, 0.0, 0.0, 0.0], "will it rain?", "bring an umbrella"),
    ("math", [0.0, 1.0, 0.0, 0.0], "what is 2+2?", "4"),
    ("food", [0.0, 0.0, 1.0, 0.0], "best noodle shop?", "try the ramen place"),
]

THRESHOLD = 0.15


def show(label: str, ans: Optional[bytes]) -> None:
    if ans is None:
        print(f"  {label}: (miss)")
    else:
        print(f"  {label}: {ans.decode()}")


def main() -> int:
    r = require_server()
    print(f"==> vector demo  {HOST}:{PORT}  dim=4  threshold={THRESHOLD}")

    print("\n1) VSET three Q&A entries")
    for key, vec, q, a in ENTRIES:
        vset(r, float_blob(vec), key, q, a)
        print(f"  VSET {key!r}  q={q!r}  a={a!r}")

    print("\n2) VGET near 'weather' vector → expect hit")
    # Slightly noisy copy of weather embedding.
    query = [0.99, 0.01, 0.0, 0.0]
    show("VGET", vget(r, float_blob(query), THRESHOLD))

    print("\n3) VGET orthogonal vector → expect miss")
    show("VGET", vget(r, float_blob([0.0, 0.0, 0.0, 1.0]), THRESHOLD))

    print("\n4) VDEL weather, then VGET again → miss")
    n = vdel(r, "weather")
    print(f"  VDEL weather → {n}")
    show("VGET", vget(r, float_blob(query), THRESHOLD))

    print("\n5) VGET math still hits")
    show("VGET", vget(r, float_blob([0.0, 0.98, 0.02, 0.0]), THRESHOLD))

    # Sanity: second delete is 0
    if vdel(r, "weather") != 0:
        die("second VDEL weather expected 0")

    print("\ndone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
