#!/usr/bin/env python3
"""Compile and run buildable examples through the deterministic headless host."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = [
    ("celeste", ROOT / "examples" / "celeste" / "celeste.c"),
    ("cube3d", ROOT / "examples" / "cube3d" / "cube3d.c"),
    ("flag9", ROOT / "examples" / "flag9" / "flag9.c"),
    ("float", ROOT / "examples" / "float" / "float.c"),
    ("game", ROOT / "examples" / "game" / "game.c"),
    ("pong", ROOT / "examples" / "pong" / "pong.c"),
    ("network_headless_smoke", ROOT / "examples" / "network" / "headless_smoke.c"),
    ("network_http_post", ROOT / "examples" / "network" / "http_post.c"),
    ("network_tcp_framed", ROOT / "examples" / "network" / "tcp_framed.c"),
    ("network_udp_ports", ROOT / "examples" / "network" / "udp_ports.c"),
    ("network_pong_host", ROOT / "examples" / "network" / "pong_multiplayer.c"),
    ("network_pong_join", ROOT / "examples" / "network" / "pong_multiplayer.c"),
    ("audio", ROOT / "examples" / "audio" / "audio.c"),
    ("save_counter", ROOT / "examples" / "save_counter" / "save_counter.c"),
    ("offscreen", ROOT / "examples" / "offscreen" / "offscreen.c"),
    ("test", ROOT / "examples" / "test" / "test.c"),
    ("void_functions", ROOT / "examples" / "test" / "void_functions.c"),
    ("memory_bulk", ROOT / "examples" / "test" / "memory_bulk.c"),
    ("goto_math", ROOT / "examples" / "test" / "goto_math.c"),
    ("math_integer", ROOT / "examples" / "test" / "math_integer.c"),
    ("graphics_shapes", ROOT / "examples" / "test" / "graphics_shapes.c"),
    ("union", ROOT / "examples" / "test" / "union.c"),
    ("system_random_time", ROOT / "examples" / "test" / "system_random_time.c"),
    ("value_stack", ROOT / "examples" / "test" / "value_stack.c"),
    ("switch_dispatch", ROOT / "examples" / "test" / "switch_dispatch.c"),
    ("printf_format", ROOT / "examples" / "test" / "printf_format.c"),
]


def main() -> int:
    failures = 0
    with tempfile.TemporaryDirectory(prefix="unilang-regress-") as temp_dir:
        output_dir = Path(temp_dir)
        opcode_result = subprocess.run([sys.executable, str(ROOT / "tools" / "test_opcode_contract.py")], cwd=ROOT)
        if opcode_result.returncode:
            failures += 1
            print("FAIL opcode contract")
        else:
            print("OK   opcode contract")
        pong_result = subprocess.run([sys.executable, str(ROOT / "tools" / "test_pong_network_contract.py")],
                                     cwd=ROOT, capture_output=True, text=True)
        if pong_result.returncode:
            failures += 1
            print("FAIL Pong networking contract")
            print((pong_result.stderr + pong_result.stdout).strip())
        else:
            print("OK   Pong networking contract")
        headless = output_dir / "headless"
        host_result = subprocess.run(["cc", "-O2", "-std=c11", "-I", str(ROOT / "C-interpreter"),
                                       str(ROOT / "tools" / "headless.c"), "-lm", "-o", str(headless)],
                                      cwd=ROOT, capture_output=True, text=True)
        if host_result.returncode:
            print(host_result.stderr.strip())
            return failures + 1
        compiled: list[tuple[str, Path]] = []
        examples = EXAMPLES + [("microcity", ROOT / "examples" / "microcity")]
        for name, source in examples:
            output = output_dir / f"{name}.ulc"
            command = [
                sys.executable,
                str(ROOT / "compiler.py"),
                str(source),
                "-o",
                str(output),
            ]
            if name == "microcity":
                command.extend(["-D", "_WIN32=1"])
            elif name == "network_pong_join":
                command.extend(["-D", "PONG_HOST=0"])
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
                compiled.append((name, output))

        netscore = output_dir / "netscore.c"
        netscore.write_text("uint32_t netscore(uint32_t x) { return x + 1; }\n"
                            "uint32_t main(void) { return netscore(3); }\n", encoding="utf-8")
        netscore_result = subprocess.run([sys.executable, str(ROOT / "compiler.py"), str(netscore),
                                          "-o", str(output_dir / "netscore.ulc")],
                                         cwd=ROOT, capture_output=True, text=True)
        if netscore_result.returncode:
            failures += 1
            print("FAIL netscore user function")
            print((netscore_result.stderr + netscore_result.stdout).strip())
        else:
            print("OK   netscore user function")

        for name, bytecode in compiled:
            run = subprocess.run([str(headless), str(bytecode), "-frames", "1", "-instr", "20000000", "-quiet"],
                                 cwd=ROOT, capture_output=True, text=True, timeout=30)
            diagnostic = run.stdout + run.stderr
            if run.returncode or "faulted=1" in diagnostic:
                failures += 1
                print(f"FAIL headless {name}")
                print(diagnostic.strip() or "headless host exited with a non-zero status")
            else:
                print(f"OK   headless {name}")

        variadic_source = output_dir / "unsupported_variadic.c"
        variadic_source.write_text(
            "uint32_t helper(uint32_t value, ...) { return value; }\n"
            "uint32_t main(void) { return helper(1, 2); }\n",
            encoding="utf-8",
        )
        variadic_result = subprocess.run(
            [sys.executable, str(ROOT / "compiler.py"), str(variadic_source),
             "-o", str(output_dir / "unsupported_variadic.ulc")],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        diagnostic = variadic_result.stderr + variadic_result.stdout
        if variadic_result.returncode == 0 or "variadic function 'helper' is not supported" not in diagnostic:
            failures += 1
            print("FAIL unsupported variadic diagnostic")
            print(diagnostic.strip() or "compiler accepted the unsupported variadic function")
        else:
            print("OK   unsupported variadic diagnostic")

        invalid_format_source = output_dir / "unsupported_printf_format.c"
        invalid_format_source.write_text(
            'int main(void) { printf("%q", 1); return 0; }\n',
            encoding="utf-8",
        )
        invalid_format_result = subprocess.run(
            [sys.executable, str(ROOT / "compiler.py"), str(invalid_format_source),
             "-o", str(output_dir / "unsupported_printf_format.ulc")],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        format_diagnostic = invalid_format_result.stderr + invalid_format_result.stdout
        if invalid_format_result.returncode == 0 or "unsupported printf format" not in format_diagnostic:
            failures += 1
            print("FAIL unsupported printf format diagnostic")
            print(format_diagnostic.strip() or "compiler accepted an unsupported printf conversion")
        else:
            print("OK   unsupported printf format diagnostic")

    if failures:
        print(f"\n{failures} example(s) failed to compile.")
        return 1
    print(f"\nAll {len(EXAMPLES) + 1} examples compiled; unsupported variadic functions and printf formats were rejected.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
