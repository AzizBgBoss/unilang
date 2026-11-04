#!/usr/bin/env python3
"""Keep compiler.py's opcode table synchronized with unilang.h."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import compiler  # noqa: E402


def c_opcodes() -> dict[str, int]:
    source = (ROOT / "C-interpreter" / "unilang.h").read_text(encoding="utf-8")
    body = re.search(r"enum\s+OP_CODES\s*\{(.*?)\};", source, re.S)
    assert body, "OP_CODES enum not found in unilang.h"
    enum_text = re.sub(r"/\*.*?\*/|//[^\n]*", "", body.group(1), flags=re.S)
    result: dict[str, int] = {}
    value = -1
    for part in enum_text.split(","):
        part = part.strip()
        if not part:
            continue
        match = re.fullmatch(r"(OP_[A-Z0-9_]+)(?:\s*=\s*(0[xX][0-9a-fA-F]+|\d+))?", part)
        assert match, f"Cannot parse C opcode enumerator: {part!r}"
        name, explicit = match.groups()
        value = int(explicit, 0) if explicit else value + 1
        result[name] = value
    return result


def main() -> None:
    c_values = c_opcodes()
    special = {
        "fillscreen": "OP_FILL_SCREEN",
        "fillcircle": "OP_FILL_CIRCLE",
        "drawrect": "OP_DRAW_RECT",
        "net_http": "OP_NET_HTTP_REQUEST",
        "net_lasterror": "OP_NET_LASTERROR",
    }
    missing = []
    mismatched = []
    for name, (value, _operands) in compiler.OPS.items():
        c_name = special.get(name, "OP_" + name.upper())
        if c_name not in c_values:
            missing.append((name, c_name))
        elif value != c_values[c_name]:
            mismatched.append((name, value, c_name, c_values[c_name]))
    assert not missing, f"Compiler opcodes lack C enum entries: {missing}"
    assert not mismatched, f"Compiler/C opcode values differ: {mismatched}"
    print(f"Opcode contract passed ({len(compiler.OPS)} compiler opcodes)")


if __name__ == "__main__":
    main()
