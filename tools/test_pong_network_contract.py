#!/usr/bin/env python3
"""Guard the joiner readiness and closed-peer removal edge cases in Pong."""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "examples/network/pong_multiplayer.c").read_text()
assert "return s == NET_CONNECTED || s == NET_READY;" in source
assert "drawstring(tmp, 10, ry, c);" in source
assert "drawstring(tmp, 76, ry, cDim);" in source
assert "uint32_t digits = put_num(tmp, 0, ipo[k]);" in source
assert "drawchar('.', ox + digits * 4, 29, cDim);" in source
print("Pong networking contract passed")
