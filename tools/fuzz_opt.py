#!/usr/bin/env python3
"""Random-program differential test for the optimizer.

Generates small C programs in the subset compiler.py supports, compiles each
with -O0 and with the optimizer, runs both on the headless VM host and
compares the printed output. Optionally (--gcc) also compares with gcc.

    cc -O2 -std=c11 -I C-interpreter tools/headless.c -o tools/headless -lm
    python3 tools/fuzz_opt.py --count 200 [--seed 1] [--gcc] [--keep DIR]
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TYPES = ["int", "unsigned", "uint8_t", "int8_t", "uint16_t", "int16_t"]
BINOPS = ["+", "-", "*", "&", "|", "^", "<", ">", "<=", ">=", "==", "!=", "&&", "||"]


class Gen:
    def __init__(self, rng):
        self.r = rng
        self.funcs = []   # (name, nparams)

    def const(self):
        r = self.r
        return str(r.choice([0, 1, 2, 3, 4, 7, 8, 15, 16, 100, 255, 256, 1000, 65535, 70000,
                             r.randint(-300, 300), r.randint(0, 1 << 20)]))

    def expr(self, scope, depth=0):
        r = self.r
        if depth > 3 or r.random() < 0.25:
            return r.choice(scope) if scope and r.random() < 0.75 else self.const()
        k = r.random()
        if k < 0.45:
            return f"({self.expr(scope, depth + 1)} {r.choice(BINOPS)} {self.expr(scope, depth + 1)})"
        if k < 0.55:
            op = r.choice(["/", "%"])
            d = r.choice(["2", "4", "8", "16", "256", "3", "10", f"({self.expr(scope, depth + 1)} | 1)"])
            return f"({self.expr(scope, depth + 1)} {op} {d})"
        if k < 0.62:
            return f"({self.expr(scope, depth + 1)} {r.choice(['<<', '>>'])} ({self.expr(scope, depth + 1)} & 15))"
        if k < 0.70:
            return f"({r.choice(['-', '~', '!'])}{self.expr(scope, depth + 1)})"
        if k < 0.78:
            return f"(({r.choice(TYPES)}){self.expr(scope, depth + 1)})"
        if k < 0.84:
            return f"({self.expr(scope, depth + 1)} ? {self.expr(scope, depth + 1)} : {self.expr(scope, depth + 1)})"
        if k < 0.92:
            arr = r.choice(["ga", "gb"])
            return f"{arr}[({self.expr(scope, depth + 1)}) & 7]"
        if self.funcs:
            name, n = r.choice(self.funcs)
            args = ", ".join(self.expr(scope, depth + 2) for _ in range(n))
            return f"{name}({args})"
        return self.const()

    def stmt(self, scope, depth, lines, ind, targets=None):
        r = self.r
        pad = "    " * ind
        k = r.random()
        targets = targets or scope
        target = r.choice(targets)
        if k < 0.35:
            lines.append(f"{pad}{target} = {self.expr(scope)};")
        elif k < 0.45:
            lines.append(f"{pad}{target} {r.choice(['+=', '-=', '*=', '&=', '|=', '^=', '<<=', '>>='])} "
                         f"{self.expr(scope) if r.random() < 0.6 else self.const()}"
                         + (" & 7" if r.random() < 0.3 else "") + ";")
        elif k < 0.52:
            lines.append(f"{pad}{target}{r.choice(['++', '--'])};")
        elif k < 0.58:
            lines.append(f"{pad}{r.choice(['ga', 'gb'])}[({self.expr(scope)}) & 7] = {self.expr(scope)};")
        elif k < 0.70 and depth < 3:
            lines.append(f"{pad}if ({self.expr(scope)}) {{")
            for _ in range(r.randint(1, 3)):
                self.stmt(scope, depth + 1, lines, ind + 1, targets)
            if r.random() < 0.5:
                lines.append(f"{pad}}} else {{")
                for _ in range(r.randint(1, 3)):
                    self.stmt(scope, depth + 1, lines, ind + 1, targets)
            lines.append(f"{pad}}}")
        elif k < 0.80 and depth < 2:
            v = f"i{depth}_{len(lines)}"
            lines.append(f"{pad}for (int {v} = 0; {v} < {r.randint(1, 6)}; {v}++) {{")
            inner = scope + [v]
            for _ in range(r.randint(1, 4)):
                self.stmt(inner, depth + 1, lines, ind + 1, targets)
            if r.random() < 0.3:
                lines.append(f"{pad}    if ({self.expr(inner)}) {r.choice(['break', 'continue'])};")
            lines.append(f"{pad}}}")
        elif k < 0.86 and depth < 2:
            v = f"w{depth}_{len(lines)}"
            lines.append(f"{pad}{{ int {v} = {r.randint(0, 5)};")
            lines.append(f"{pad}  while ({v} > 0) {{ {v}--; {target} = {self.expr(scope + [v])}; }} }}")
        elif k < 0.90:
            p = f"p{len(lines)}"
            t = r.choice(["ga", "gb"])
            ty = "int" if t == "ga" else "uint8_t"
            lines.append(f"{pad}{{ {ty} *{p} = &{t}[{r.randint(0, 7)}]; *{p} = *{p} + {self.expr(scope)}; }}")
        elif k < 0.94 and depth < 3:
            lines.append(f"{pad}switch (({self.expr(scope)}) & 3) {{")
            for c in range(3):
                lines.append(f"{pad}case {c}: {target} = {self.expr(scope)};" + (" break;" if r.random() < 0.7 else ""))
            lines.append(f"{pad}default: {target} += 1;")
            lines.append(f"{pad}}}")
        else:
            lines.append(f'{pad}printf("%d ", {self.expr(scope)});')

    def function(self, idx):
        r = self.r
        n = r.randint(1, 3)
        params = [f"a{k}" for k in range(n)]
        ptypes = [r.choice(TYPES) for _ in params]
        ret = r.choice(TYPES)
        name = f"f{idx}"
        lines = [f"{ret} {name}({', '.join(f'{t} {p}' for t, p in zip(ptypes, params))}) {{"]
        locs = []
        for k in range(r.randint(0, 3)):
            v = f"l{k}"
            lines.append(f"    {r.choice(TYPES)} {v} = {self.expr(params + locs)};")
            locs.append(v)
        scope = params + locs
        for _ in range(r.randint(1, 5)):
            self.stmt(scope, 1, lines, 1)
        if r.random() < 0.4:
            lines.append(f"    if ({self.expr(scope)}) return {self.expr(scope)};")
        lines.append(f"    return {self.expr(scope)};")
        lines.append("}")
        self.funcs.append((name, n))
        return lines

    def program(self):
        r = self.r
        out = ["#include <stdint.h>", "int ga[8];", "uint8_t gb[8];"]
        gvars = []
        for k in range(r.randint(1, 4)):
            v = f"g{k}"
            init = f" = {self.const()}" if r.random() < 0.5 else ""
            out.append(f"{r.choice(TYPES)} {v}{init};")
            gvars.append(v)
        for k in range(r.randint(0, 3)):
            out += self.function(k)
        out.append("int main() {")
        locs = []
        for k in range(r.randint(2, 6)):
            v = f"v{k}"
            out.append(f"    {r.choice(TYPES)} {v} = {self.expr(gvars + locs)};")
            locs.append(v)
        scope = gvars + locs
        body = []
        for _ in range(r.randint(4, 14)):
            self.stmt(scope, 0, body, 1)
        out += body
        for v in scope:
            out.append(f'    printf("{v}=%d ", {v});')
        out.append('    for (int k = 0; k < 8; k++) printf("%d,%d ", ga[k], gb[k]);')
        out.append('    printf("\\n");')
        out.append("    return 0;")
        out.append("}")
        return "\n".join(out) + "\n"


def run(cmd, **kw):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, **kw)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "TIMEOUT", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--gcc", action="store_true", help="also compare against gcc -fwrapv")
    ap.add_argument("--keep", help="directory to keep failing programs in")
    ap.add_argument("--host", default=str(ROOT / "tools" / "headless"))
    args = ap.parse_args()
    if not os.path.exists(args.host):
        print(f"build the headless host first: cc -O2 -std=c11 -I C-interpreter tools/headless.c -o {args.host} -lm")
        return 2
    fails = gcc_diffs = skipped = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for n in range(args.count):
            seed = args.seed * 100000 + n
            src = Gen(random.Random(seed)).program()
            c = tmp / "p.c"
            c.write_text(src)
            outs = {}
            for mode in ("-O0", "-O2"):
                res = run([sys.executable, str(ROOT / "compiler.py"), mode, str(c), "-o", str(tmp / f"p{mode}.ulc")])
                if res.returncode:
                    outs[mode] = "COMPILE ERROR " + res.stderr.strip().splitlines()[-1]
                    continue
                res = run([args.host, str(tmp / f"p{mode}.ulc"), "-frames", "1", "-instr", "50000000"], timeout=60)
                outs[mode] = res.stdout
            if outs["-O0"].startswith("COMPILE ERROR") and outs["-O2"].startswith("COMPILE ERROR"):
                skipped += 1
                continue
            bad = outs["-O0"] != outs["-O2"]
            gcc_bad = False
            if args.gcc and not bad:
                g = tmp / "g.c"
                g.write_text(src)
                res = run(["gcc", "-w", "-fwrapv", "-include", "stdio.h", "-o", str(tmp / "g"), str(g)])
                if res.returncode == 0:
                    res = run([str(tmp / "g")], timeout=10)
                    want = res.stdout.strip()
                    got = outs["-O2"].split("\nDIGEST")[0].strip()
                    gcc_bad = want != got
            if bad or gcc_bad:
                if bad:
                    fails += 1
                else:
                    gcc_diffs += 1
                tag = "OPT MISMATCH" if bad else "gcc differs"
                print(f"{tag}: seed {seed}")
                if args.keep:
                    os.makedirs(args.keep, exist_ok=True)
                    Path(args.keep, f"{'opt' if bad else 'gcc'}_{seed}.c").write_text(src)
    print(f"{args.count} programs: {fails} optimizer mismatches, {skipped} skipped (compile errors)"
          + (f", {gcc_diffs} differ from gcc" if args.gcc else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
