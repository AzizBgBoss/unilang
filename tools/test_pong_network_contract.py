#!/usr/bin/env python3
"""Guard the joiner readiness and closed-peer removal edge cases in Pong."""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "examples/network/pong_multiplayer.c").read_text()
assert "connection_status == NET_CONNECTED || connection_status == NET_READY" in source
loop_start = source.index("i = 0;\n        while (i < count)")
loop_end = source.index("#else", loop_start)
loop = source[loop_start:loop_end]
assert "count = count - 1;\n                continue;" in loop
assert "i = i + 1;" in loop
print("Pong networking contract passed")
