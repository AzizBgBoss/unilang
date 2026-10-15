"""
A small C preprocessor for the uniClang compiler (compiler.py).

Supported directives:
  #include "file"   (searched next to the including file, then in -I dirs)
  #include <file>   (searched in -I dirs; the standard headers stdint.h,
                     stdbool.h, stdio.h, stdlib.h, stddef.h, string.h,
                     limits.h and stdarg.h are accepted and ignored, since
                     their types/functions are built into the compiler)
  #define / #undef  (object-like and function-like macros, with #stringify,
                     ## token pasting and __VA_ARGS__)
  #if / #ifdef / #ifndef / #elif / #else / #endif   (with defined(X) and
                     full integer constant expressions)
  #pragma once, #error, #warning, #line (ignored)

Predefined macros: NULL, RAND_MAX, CHAR_BIT, UINT8_MAX, UINT16_MAX,
UINT32_MAX, __UNILANG__, __LINE__, __FILE__.

The output keeps line numbers meaningful by emitting `#line N "file"`
markers (pycparser understands them), so parse errors point at the right
file and line even across #include boundaries.

Limitation: a function-like macro call must fit on one (spliced) line.
"""

import os
import re
import sys


class PreprocessorError(Exception):
    pass


STD_HEADERS = {
    "stdint.h", "stdbool.h", "stdio.h", "stdlib.h",
    "stddef.h", "string.h", "limits.h", "stdarg.h",
}

TOKEN_RE = re.compile(
    r"""\s+|[A-Za-z_]\w*|\d[\w.]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\#\#|\.\.\.|.""",
    re.S,
)


def strip_comments(source):
    """Remove // and /* */ comments. Preserves newlines (so line numbers stay
    accurate) and is string/char-literal aware so '//' or '/*' inside a
    string or char constant isn't mistaken for a comment."""
    out = []
    i, n = 0, len(source)
    in_string = in_char = in_line_comment = in_block_comment = False
    while i < n:
        c = source[i]
        nxt = source[i + 1] if i + 1 < n else ""
        if in_line_comment:
            if c == "\n":
                in_line_comment = False
                out.append(c)
            i += 1
            continue
        if in_block_comment:
            if c == "\n":
                out.append(c)  # keep line numbers aligned
            elif c == "*" and nxt == "/":
                in_block_comment = False
                i += 1
            i += 1
            continue
        if in_string or in_char:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(nxt)
                i += 2
                continue
            if (in_string and c == '"') or (in_char and c == "'"):
                in_string = in_char = False
            i += 1
            continue
        if c == '"':
            in_string = True
            out.append(c)
        elif c == "'":
            in_char = True
            out.append(c)
        elif c == "/" and nxt == "/":
            in_line_comment = True
            i += 1
        elif c == "/" and nxt == "*":
            in_block_comment = True
            out.append(" ")
            i += 1
        else:
            out.append(c)
        i += 1
    return "".join(out)


# ---------------------------------------------------------------------------
# #if expression evaluation
# ---------------------------------------------------------------------------
_IF_TOKEN = re.compile(
    r"\s*(?:(0[xX][0-9a-fA-F]+|\d+)[uUlL]*"
    r"|('(?:\\.|[^'\\])*')"
    r"|(&&|\|\||==|!=|<=|>=|<<|>>|[-+*/%<>&|^!~?:()])"
    r"|([A-Za-z_]\w*))"
)

_IF_PREC = {
    "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5,
    "==": 6, "!=": 6, "<": 7, ">": 7, "<=": 7, ">=": 7,
    "<<": 8, ">>": 8, "+": 9, "-": 9, "*": 10, "/": 10, "%": 10,
}


def _c_div(a, b):
    if b == 0:
        raise PreprocessorError("division by zero in #if expression")
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def _apply_op(op, a, b):
    if op == "||": return int(bool(a) or bool(b))
    if op == "&&": return int(bool(a) and bool(b))
    if op == "|": return a | b
    if op == "^": return a ^ b
    if op == "&": return a & b
    if op == "==": return int(a == b)
    if op == "!=": return int(a != b)
    if op == "<": return int(a < b)
    if op == ">": return int(a > b)
    if op == "<=": return int(a <= b)
    if op == ">=": return int(a >= b)
    if op == "<<": return a << b
    if op == ">>": return a >> b
    if op == "+": return a + b
    if op == "-": return a - b
    if op == "*": return a * b
    if op == "/": return _c_div(a, b)
    if op == "%": return a - b * _c_div(a, b)
    raise PreprocessorError(f"bad operator '{op}' in #if expression")


def eval_if_expression(text):
    text = text.strip()
    toks = []
    pos = 0
    while pos < len(text):
        m = _IF_TOKEN.match(text, pos)
        if not m:
            raise PreprocessorError(f"invalid token in #if expression: {text[pos:]!r}")
        pos = m.end()
        if m.group(1) is not None:
            s = m.group(1)
            if s[:2].lower() == "0x":
                toks.append(("num", int(s, 16)))
            elif len(s) > 1 and s[0] == "0":
                toks.append(("num", int(s, 8)))
            else:
                toks.append(("num", int(s)))
        elif m.group(2) is not None:
            toks.append(("num", ord(bytes(m.group(2)[1:-1], "utf-8").decode("unicode_escape"))))
        elif m.group(3) is not None:
            toks.append(("op", m.group(3)))
        else:
            toks.append(("num", 0))  # unknown identifiers evaluate to 0, like in C

    idx = [0]

    def peek():
        return toks[idx[0]] if idx[0] < len(toks) else (None, None)

    def take():
        t = peek()
        idx[0] += 1
        return t

    def expect(val):
        if take() != ("op", val):
            raise PreprocessorError(f"expected '{val}' in #if expression")

    def unary():
        kind, val = take()
        if kind == "num":
            return val
        if kind == "op":
            if val == "(":
                v = ternary()
                expect(")")
                return v
            if val == "-": return -unary()
            if val == "+": return unary()
            if val == "!": return int(not unary())
            if val == "~": return ~unary()
        raise PreprocessorError("malformed #if expression")

    def binary(minp):
        left = unary()
        while True:
            kind, val = peek()
            if kind != "op" or val not in _IF_PREC or _IF_PREC[val] < minp:
                return left
            take()
            right = binary(_IF_PREC[val] + 1)
            left = _apply_op(val, left, right)

    def ternary():
        c = binary(1)
        if peek() == ("op", "?"):
            take()
            a = ternary()
            expect(":")
            b = ternary()
            return a if c else b
        return c

    result = ternary()
    if idx[0] != len(toks):
        raise PreprocessorError("malformed #if expression")
    return result


class Preprocessor:
    def __init__(self, include_dirs=(), defines=None):
        self.include_dirs = [os.path.abspath(d) for d in include_dirs]
        self.macros = {  # name -> (params list or None, body text)
            "NULL": (None, "0"),
            "RAND_MAX": (None, "32767"),
            "CHAR_BIT": (None, "8"),
            "UINT8_MAX": (None, "255"),
            "UINT16_MAX": (None, "65535"),
            "UINT32_MAX": (None, "4294967295"),
            "__UNILANG__": (None, "1"),
        }
        for name, value in (defines or {}).items():
            self.macros[name] = (None, value)
        self.once = set()
        self.cur_file = "<input>"
        self.cur_line = 0

    # -- public entry point ----------------------------------------------------
    def process(self, source, filename="<input>"):
        abs_path = os.path.abspath(filename) if os.path.isfile(filename) else None
        cur_dir = os.path.dirname(abs_path) if abs_path else os.getcwd()
        lines = self._process_text(source, filename, abs_path, cur_dir, 0)
        return "\n".join([f'#line 1 "{self._lname(filename)}"'] + lines)

    @staticmethod
    def _lname(path):
        return os.path.basename(path).replace("\\", "/").replace('"', "")

    # -- core line loop ----------------------------------------------------------
    def _process_text(self, text, filename, abs_path, cur_dir, depth):
        if depth > 64:
            raise PreprocessorError(f"{filename}: #include nested too deeply (recursive include?)")
        text = strip_comments(text.replace("\r\n", "\n").replace("\r", "\n"))
        phys = text.split("\n")
        out = []
        cond = []  # stack of [parent_active, active, any_branch_taken]
        saved_file, saved_line = self.cur_file, self.cur_line
        self.cur_file = self._lname(filename)

        i = 0
        while i < len(phys):
            line = phys[i]
            start = i + 1
            n = 1
            while line.endswith("\\") and i + n < len(phys):  # line splicing
                line = line[:-1] + phys[i + n]
                n += 1
            i += n
            self.cur_line = start
            active = cond[-1][1] if cond else True
            stripped = line.lstrip()

            if stripped.startswith("#"):
                result = self._directive(stripped[1:].strip(), filename, abs_path, cur_dir,
                                         start, n, cond, depth)
                if result is None:
                    out.extend([""] * n)
                else:
                    out.extend(result)
            elif active:
                out.append(self.expand(line))
                out.extend([""] * (n - 1))
            else:
                out.extend([""] * n)

        if cond:
            raise PreprocessorError(f"{filename}: unterminated #if/#ifdef/#ifndef")
        self.cur_file, self.cur_line = saved_file, saved_line
        return out

    # -- directives ----------------------------------------------------------------
    def _directive(self, body, filename, abs_path, cur_dir, lineno, nlines, cond, depth):
        where = f"{self._lname(filename)}:{lineno}"
        m = re.match(r"(\w+)\s*(.*)$", body, re.S)
        if not m:
            return None  # null directive, or a '# 12 "file"' line marker
        name, rest = m.group(1), m.group(2).strip()
        active = cond[-1][1] if cond else True

        if name in ("if", "ifdef", "ifndef"):
            val = False
            if active:
                if name == "if":
                    val = self._eval_if(rest, where)
                else:
                    if not rest:
                        raise PreprocessorError(f"{where}: #{name} needs a macro name")
                    defined = rest.split()[0] in self.macros
                    val = defined if name == "ifdef" else not defined
            cond.append([active, active and bool(val), active and bool(val)])
            return None
        if name == "elif":
            if not cond:
                raise PreprocessorError(f"{where}: #elif without #if")
            c = cond[-1]
            if c[2] or not c[0]:
                c[1] = False
            else:
                c[1] = bool(self._eval_if(rest, where))
                c[2] = c[1]
            return None
        if name == "else":
            if not cond:
                raise PreprocessorError(f"{where}: #else without #if")
            c = cond[-1]
            c[1] = c[0] and not c[2]
            c[2] = True
            return None
        if name == "endif":
            if not cond:
                raise PreprocessorError(f"{where}: #endif without #if")
            cond.pop()
            return None

        if not active:
            return None

        if name == "define":
            self._define(rest, where)
        elif name == "undef":
            if rest:
                self.macros.pop(rest.split()[0], None)
        elif name == "include":
            return self._include(rest, filename, cur_dir, lineno, nlines, depth, where)
        elif name == "pragma":
            if rest.split()[:1] == ["once"] and abs_path:
                self.once.add(abs_path)
        elif name == "error":
            raise PreprocessorError(f"{where}: #error {rest}")
        elif name == "warning":
            print(f"{where}: warning: {rest}", file=sys.stderr)
        elif name == "line":
            pass
        else:
            raise PreprocessorError(f"{where}: unknown preprocessor directive '#{name}'")
        return None

    def _eval_if(self, expr, where):
        def repl_defined(m):
            return "1" if (m.group(1) or m.group(2)) in self.macros else "0"

        expr = re.sub(r"\bdefined\s*(?:\(\s*(\w+)\s*\)|(\w+))", repl_defined, expr)
        expr = self.expand(expr)
        try:
            return eval_if_expression(expr)
        except PreprocessorError as e:
            raise PreprocessorError(f"{where}: {e}")

    def _define(self, rest, where):
        m = re.match(r"([A-Za-z_]\w*)(\(([^)]*)\))?\s*(.*)$", rest, re.S)
        if not m:
            raise PreprocessorError(f"{where}: malformed #define")
        name, has_params, plist, body = m.group(1), m.group(2), m.group(3), m.group(4).strip()
        if has_params is not None:
            params = [p.strip() for p in plist.split(",")] if plist.strip() else []
            self.macros[name] = (params, body)
        else:
            self.macros[name] = (None, body)

    # -- #include ------------------------------------------------------------------
    def _find_include(self, name, quoted, cur_dir):
        dirs = ([cur_dir] if quoted else []) + self.include_dirs
        for d in dirs:
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return os.path.abspath(p)
        return None

    def _include(self, rest, filename, cur_dir, lineno, nlines, depth, where):
        if not rest or rest[0] not in '"<':
            rest = self.expand(rest).strip()  # #include MACRO_NAME
        m = re.match(r'"([^"]+)"|<([^>]+)>', rest)
        if not m:
            raise PreprocessorError(f"{where}: malformed #include")
        quoted = m.group(1) is not None
        name = m.group(1) or m.group(2)
        found = self._find_include(name, quoted, cur_dir)
        if found is None:
            if os.path.basename(name) in STD_HEADERS:
                return None  # built into the compiler
            raise PreprocessorError(f"{where}: fatal error: '{name}': no such file (use -I to add search directories)")
        if found in self.once:
            return None
        with open(found, "r", encoding="utf-8-sig") as f:
            text = f.read()
        inner = self._process_text(text, found, found, os.path.dirname(found), depth + 1)
        return ([f'#line 1 "{self._lname(found)}"'] + inner +
                [f'#line {lineno + nlines} "{self._lname(filename)}"'])

    # -- macro expansion --------------------------------------------------------
    def expand(self, text, hide=frozenset()):
        toks = TOKEN_RE.findall(text)
        out = []
        i, n = 0, len(toks)
        while i < n:
            t = toks[i]
            if t[0].isalpha() or t[0] == "_":
                if t == "__LINE__":
                    out.append(str(self.cur_line))
                    i += 1
                    continue
                if t == "__FILE__":
                    out.append('"' + self.cur_file + '"')
                    i += 1
                    continue
                macro = self.macros.get(t)
                if macro is not None and t not in hide:
                    params, body = macro
                    if params is None:
                        out.append(self.expand(body, hide | {t}))
                        i += 1
                        continue
                    j = i + 1
                    while j < n and toks[j].isspace():
                        j += 1
                    if j < n and toks[j] == "(":
                        args, k = self._collect_args(toks, j, t)
                        replaced = self._substitute(t, params, body, args, hide)
                        out.append(self.expand(replaced, hide | {t}))
                        i = k
                        continue
            out.append(t)
            i += 1
        return "".join(out)

    @staticmethod
    def _collect_args(toks, j, name):
        depth = 0
        args = [[]]
        k = j
        while k < len(toks):
            t = toks[k]
            if t == "(":
                depth += 1
                if depth > 1:
                    args[-1].append(t)
            elif t == ")":
                depth -= 1
                if depth == 0:
                    return ["".join(a).strip() for a in args], k + 1
                args[-1].append(t)
            elif t == "," and depth == 1:
                args.append([])
            else:
                args[-1].append(t)
            k += 1
        raise PreprocessorError(f"unterminated argument list invoking macro '{name}'")

    def _substitute(self, name, params, body, args, hide):
        variadic = bool(params) and params[-1] == "..."
        names = list(params[:-1]) if variadic else list(params)
        if not params and args == [""]:
            args = []
        if variadic:
            if len(args) < len(names):
                raise PreprocessorError(f"macro '{name}' needs at least {len(names)} argument(s)")
            args = args[:len(names)] + [",".join(args[len(names):])]
            names.append("__VA_ARGS__")
        elif len(args) != len(names):
            raise PreprocessorError(f"macro '{name}' expects {len(names)} argument(s), got {len(args)}")

        raw = dict(zip(names, args))
        expanded = {}
        btoks = TOKEN_RE.findall(body)
        n = len(btoks)

        def next_to_paste(k, step):
            k += step
            while 0 <= k < n and btoks[k].isspace():
                k += step
            return 0 <= k < n and btoks[k] == "##"

        out = []
        i = 0
        while i < n:
            t = btoks[i]
            if t == "#":
                j = i + 1
                while j < n and btoks[j].isspace():
                    j += 1
                if j < n and btoks[j] in raw:
                    s = " ".join(raw[btoks[j]].split())
                    out.append('"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"')
                    i = j + 1
                    continue
            if t in raw:
                if next_to_paste(i, -1) or next_to_paste(i, 1):
                    out.append(raw[t])
                else:
                    if t not in expanded:
                        expanded[t] = self.expand(raw[t], hide)
                    out.append(expanded[t])
            else:
                out.append(t)
            i += 1
        return re.sub(r"\s*##\s*", "", "".join(out))
