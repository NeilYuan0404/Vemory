#!/usr/bin/env python3
"""Generate compile_commands.json for clangd / VS Code C++ IntelliSense."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "include"
GENERATED = ROOT / "generated"
USEARCH_INC = ROOT / "third_party/usearch/include"
USEARCH_FP16_INC = ROOT / "third_party/usearch/fp16/include"
SPDLOG_INC = ROOT / "third_party/spdlog/include"
GTEST_INC = ROOT / "third_party/googletest/googletest/include"
GTEST_SRC = ROOT / "third_party/googletest/googletest"


def add(entries: list, file: Path, extra: list[str] | None = None) -> None:
    flags = [
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-g",
        f"-I{INCLUDE}",
        f"-I{GENERATED}",
        f"-I{USEARCH_INC}",
        f"-I{USEARCH_FP16_INC}",
        f"-I{SPDLOG_INC}",
    ]
    if extra:
        flags.extend(extra)
    rel = file.relative_to(ROOT)
    entries.append(
        {
            "directory": str(ROOT),
            "command": "g++ " + " ".join(flags) + f" -c {rel}",
            "file": str(rel),
        }
    )


def main() -> None:
    entries: list = []
    for p in sorted((ROOT / "src").rglob("*.cc")):
        add(entries, p)
    gen_cc = GENERATED / "VNode.pb.cc"
    if gen_cc.exists():
        add(entries, gen_cc)
    for p in sorted((ROOT / "tests").rglob("*.cc")):
        extras: list[str] = []
        if "unit" in p.parts and GTEST_INC.exists():
            extras = [f"-I{GTEST_INC}", f"-I{GTEST_SRC}", "-pthread"]
        add(entries, p, extras)

    out = ROOT / "compile_commands.json"
    out.write_text(json.dumps(entries, indent=2) + "\n")
    print(f"wrote {out} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
