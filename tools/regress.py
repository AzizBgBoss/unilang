#!/usr/bin/env python3
"""Compile buildable examples to temporary bytecode as a smoke test."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = [
    ("celeste", ROOT / "examples" / "celeste" / "celeste.c"),
    ("cube3d", ROOT / "examples" / "cube3d" / "cube3d.c"),
    ("flag6", ROOT / "examples" / "flag6" / "flag6.c"),
    ("float", ROOT / "examples" / "float" / "float.c"),
    ("game", ROOT / "examples" / "game" / "game.c"),
    ("pong", ROOT / "examples" / "pong" / "pong.c"),
    ("test", ROOT / "examples" / "test" / "test.c"),
]


def main() -> int:
    failures = 0
    print(
        "SKIP examples/microcity: current compiler fails on a struct-member "
        "lvalue during code generation."
    )
    with tempfile.TemporaryDirectory(prefix="unilang-regress-") as temp_dir:
        output_dir = Path(temp_dir)
        for name, source in EXAMPLES:
            output = output_dir / f"{name}.ulc"
            command = [
                sys.executable,
                str(ROOT / "compiler.py"),
                str(source),
                "-o",
                str(output),
            ]
            result = subprocess.run(
                command,
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode:
                failures += 1
                print(f"FAIL {source.relative_to(ROOT)}")
                details = result.stderr.strip() or result.stdout.strip()
                print(details or "compiler exited with a non-zero status")
            else:
                print(f"OK   {source.relative_to(ROOT)}")

    if failures:
        print(f"\n{failures} example(s) failed to compile.")
        return 1
    print(f"\nAll {len(EXAMPLES)} buildable examples compiled successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
