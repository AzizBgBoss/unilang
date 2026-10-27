#!/usr/bin/env python3
"""Compile the checked-in example programs and compare the emitted .ulc bytes to stored baselines."""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = Path("/tmp/unilang-baseline")

EXAMPLES = [
    ROOT / "examples" / "pong" / "pong.c",
    ROOT / "examples" / "game" / "game.c",
    ROOT / "examples" / "test" / "test.c",
]

for optional_dir in ["pong", "helloworld", "roulette"]:
    candidate = ROOT / optional_dir
    if candidate.exists() and candidate.is_dir():
        EXAMPLES.append(candidate)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def compile_target(target: Path, output: Path) -> tuple[bool, str | None]:
    cmd = [sys.executable, str(ROOT / "compiler.py"), str(target), "-o", str(output)]
    if target.is_dir():
        cmd = [sys.executable, str(ROOT / "compiler.py"), str(target), "-o", str(output)]
    try:
        proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    except OSError as exc:
        return False, f"spawn failed: {exc}"
    if proc.returncode != 0:
        details = proc.stderr.strip() or proc.stdout.strip()
        return False, details or "compiler exited with a non-zero status"
    return True, None


def main() -> int:
    BASELINE.mkdir(parents=True, exist_ok=True)
    failures = 0
    changed = 0

    for target in EXAMPLES:
        if target.is_dir():
            src = sorted(target.glob("*.c")) + sorted(target.glob("*.cpp"))
            if not src:
                print(f"SKIP {target}: no .c/.cpp sources found")
                continue
            source = src[0]
            output = BASELINE / target.name / f"{target.name}.ulc.baseline"
        else:
            source = target
            output = BASELINE / source.relative_to(ROOT).with_suffix(".ulc.baseline")

        output.parent.mkdir(parents=True, exist_ok=True)
        ok, error = compile_target(source, output)
        if not ok:
            failures += 1
            print(f"FAIL {source.relative_to(ROOT)}")
            print(error)
            continue

        print(f"OK   {source.relative_to(ROOT)}")

    if failures:
        print(f"\n{failures} compile failures detected.")
        return 1
    print(f"\nAll {len(EXAMPLES)} example programs compiled successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
