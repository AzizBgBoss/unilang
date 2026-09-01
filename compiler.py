'''
Basic C -> unilang bytecode (.ulc) compiler by AzizBgBoss
https://github.com/AzizBgBoss/unilang

Requires pycparser:  pip install pycparser

See README.md for full documentation of the C subset and the .ulc bytecode format.

Function calls are implemented by INLINING the callee's body at the call site
(with fresh, uniquely-named parameter/local/return addresses per call) rather
than using the VM's 'subroutine'/'return' opcodes. This means:
  - functions can freely call other functions, including from inside other
    functions (not just from main) — inlining just flattens it all out.
  - `return` inside a function is compiled as "write result, then setpc to
    the end of this inlined copy" — so early returns work fine.
  - RECURSION IS NOT SUPPORTED (direct or mutual) and is rejected at compile
    time with a clear error, since inlining a recursive call would mean
    infinite code.
  - every call gets its own copy of the function's locals in memory (the
    flat memory model has no stack), so heavily-called functions cost more
    bytecode size and memory, not runtime overhead.

Memory sizing: the compiler no longer assumes a fixed VM memory size. At the
very start of the compiled program it emits a `getmemsize` call to ask the
VM for its actual memory size at runtime, and uses that value (not a Python
constant) for setpixel()/getkey()'s address math. It also compares that
runtime size against how many bits the program actually needed at compile
time and prints a warning (it still runs — it just may misbehave, e.g. by
colliding with the graphics framebuffer) if there isn't enough room.
'''

import sys
import pycparser
from pycparser import c_ast

# ---------------------------------------------------------------------------
# Opcode table (subset of main.py's ISA needed by this compiler)
# name: (opcode byte, [operand widths in bytes, 0 = length-prefixed text])
# ---------------------------------------------------------------------------
OPS = {
    "flag":      (0x01, [1, 1]),                # flag, val
    "isflagsupported": (0x02, [1, 1, 4, 1]),   # flag, val, addr, size
    "mem":       (0x03, [4, 1, 4]),             # addr, size, val
    "setmem":    (0x04, [4, 1, 4, 1]),          # addr1(ptr), size1, addr2(ptr), size2
    "write":     (0x05, [0, 4, 1]),             # text, addr, size
    "out":       (0x07, [4, 1]),                # addr, size
    "print":     (0x09, [0]),                   # text
    "printn":    (0x0A, [0]),                   # text
    "add":       (0x0E, [4, 4, 1, 4]),          # val, addr1, size1, addr2
    "addv":      (0x0F, [4, 1, 4, 1, 4, 1]),
    "sub":       (0x10, [4, 4, 1, 4]),
    "subv":      (0x11, [4, 1, 4, 1, 4, 1]),
    "mul":       (0x12, [4, 4, 1, 4]),
    "mulv":      (0x13, [4, 1, 4, 1, 4, 1]),
    "div":       (0x14, [4, 4, 1, 4]),
    "divv":      (0x15, [4, 1, 4, 1, 4, 1]),
    "mod":       (0x16, [4, 4, 1, 4]),
    "modv":      (0x17, [4, 1, 4, 1, 4, 1]),
    "setpc":     (0x1B, [4]),                   # pos
    "compare":   (0x1F, [4, 4, 1, 4, 1]),       # val, addr1, size1, addr2, size2
    "comparev":  (0x20, [4, 1, 4, 1, 4, 1]),
    "isequal":   (0x21, [4, 4, 1, 4]),          # val, addr1, size1, addr2
    "not":       (0x22, [4, 1, 4, 1]),
    "if":        (0x2D, [4, 1, 4]),             # addr, size, pos
    "ifnot":     (0x2E, [4, 1, 4]),
    "sleep":     (0x31, [4]),                   # ms
    "getmemsize": (0x33, [4, 1]),               # addr, size
    "refreshscreen": (0x34, []),
    "exit":      (0xFF, []),
}

TYPE_BITS = {
    "bool": 1,
    "uint2": 2,
    "uint4": 4,
    "uint8": 8,
    "uint16": 16,
    "uint32": 32,
}
DEFAULT_TYPE = "uint32"
INT_SIZE = TYPE_BITS[DEFAULT_TYPE]  # width used by VM pointers and compiler scratch
VARS_START = 0          # bit address where user variables start

GFX_64x64_MONO_BITS = 64 * 64   # bits reserved off the end of memory when flag(0, 1) is set
KEYBOARD_BITS = 8               # bits reserved just before the framebuffer when flag(1, 1) is set


class Label:
    """A jump target resolved once its position in the bytecode is known."""
    def __init__(self, name="L"):
        self.name = name
        self.addr = None


class Compiler:
    def __init__(self):
        self.buf = bytearray()
        self.patches = []          # (byte_offset_in_buf, Label, width)
        self.vars = {}              # name -> bit address (current scope view)
        self.var_sizes = {}         # name -> bit width (current scope view)
        self.addr_sizes = {}        # allocated address -> bit width
        self.next_addr = VARS_START
        self.next_temp = 0
        self.function_asts = {}     # name -> FuncDef node
        self.inlining_stack = []    # names currently being inlined (recursion guard)
        self.return_stack = []      # (ret_addr, end_label) for the innermost inlined call
        self.next_instance = 0

        self.runtime_memsize_addr = None   # set by gen_prologue(); holds getmemsize()'s result
        self.uses_graphics = False         # set True by flag()/setpixel()/getkey()
        self.required_bits_patch_pos = None  # byte offset to patch once we know final memory usage
        self.setpixel_scratch = None       # shared scratch addresses, allocated once, reused per call
        self.getkey_scratch = None

    # -- memory allocation ---------------------------------------------------
    def alloc_var(self, name, size=INT_SIZE):
        addr = self.next_addr
        self.next_addr += size
        self.vars[name] = addr
        self.var_sizes[name] = size
        self.addr_sizes[addr] = size
        return addr

    def alloc_temp(self, size=INT_SIZE):
        name = f"__t{self.next_temp}"
        self.next_temp += 1
        return self.alloc_var(name, size)

    def var_addr(self, name):
        if name not in self.vars:
            raise NotImplementedError(f"use of undeclared variable '{name}'")
        return self.vars[name]

    def addr_size(self, addr):
        return self.addr_sizes.get(addr, INT_SIZE)

    @staticmethod
    def type_name(type_node):
        if not isinstance(type_node, c_ast.TypeDecl) or not isinstance(type_node.type, c_ast.IdentifierType):
            raise NotImplementedError("only fixed-width unsigned types are supported")
        names = type_node.type.names
        if len(names) != 1 or names[0] not in TYPE_BITS:
            raise NotImplementedError("only bool, uint2, uint4, uint8, uint16, and uint32 are supported")
        return names[0]

    @classmethod
    def type_size(cls, type_node):
        return TYPE_BITS[cls.type_name(type_node)]

    def function_return_size(self, fn):
        return self.type_size(fn.decl.type.type)

    # -- low level emission ---------------------------------------------------
    def emit(self, op, *operands):
        byte, sizes = OPS[op]
        if len(operands) != len(sizes):
            raise ValueError(f"{op} expects {len(sizes)} operands, got {len(operands)}")
        self.buf.append(byte)
        for val, width in zip(operands, sizes):
            if width == 0:
                text = val.encode("utf-8")
                self.buf += len(text).to_bytes(2, "big")
                self.buf += text
            elif isinstance(val, Label):
                self.patches.append((len(self.buf), val, width))
                self.buf += bytes(width)
            else:
                self.buf += (int(val) % (2 ** (width * 8))).to_bytes(width, "big")

    def define_label(self, label):
        label.addr = len(self.buf)

    def finalize(self):
        for pos, label, width in self.patches:
            if label.addr is None:
                raise RuntimeError(f"label '{label.name}' used but never defined")
            self.buf[pos:pos + width] = label.addr.to_bytes(width, "big")
        return bytes(self.buf)

    # -- startup prologue: query real memory size, warn if too small ------------
    def gen_prologue(self):
        self.runtime_memsize_addr = self.alloc_temp()
        self.emit("getmemsize", self.runtime_memsize_addr, INT_SIZE)

        required_addr = self.alloc_temp()
        instr_start = len(self.buf)
        self.emit("mem", required_addr, INT_SIZE, 0)  # placeholder; patched once compilation finishes
        # 'mem' operand layout is (addr[4], size[1], val[4]); val starts after
        # opcode(1) + addr(4) + size(1) = 6 bytes into this instruction.
        self.required_bits_patch_pos = instr_start + 1 + 4 + 1

        cmp_result = self.alloc_temp()
        self.emit("comparev", self.runtime_memsize_addr, INT_SIZE, required_addr, INT_SIZE, cmp_result, INT_SIZE)
        # comparev: 0=equal, 1=runtime>required, 2=runtime<required
        too_small = self.alloc_temp()
        self.emit("isequal", 2, cmp_result, INT_SIZE, too_small)

        label_ok = Label("memsize_ok")
        self.emit("ifnot", too_small, 1, label_ok)
        self.emit("printn", "WARNING: this program needs at least ")
        self.emit("out", required_addr, INT_SIZE)
        self.emit("printn", " bits of memory, but only ")
        self.emit("out", self.runtime_memsize_addr, INT_SIZE)
        self.emit("print", " are available. Increase 'memsize' in main.py.")
        self.define_label(label_ok)

    # -- expressions: returns the bit-address holding the result --------------
    def gen_expr(self, node):
        if isinstance(node, c_ast.Constant):
            if node.type not in ("int", "char"):
                raise NotImplementedError(f"unsupported constant type '{node.type}'")
            dest = self.alloc_temp()
            self.emit("mem", dest, INT_SIZE, int(node.value, 0))
            return dest

        if isinstance(node, c_ast.ID):
            return self.var_addr(node.name)

        if isinstance(node, c_ast.Assignment):
            if node.op != "=":
                raise NotImplementedError(f"unsupported assignment operator '{node.op}'")
            rhs = self.gen_expr(node.rvalue)
            dest = self.var_addr(node.lvalue.name)
            self.emit("add", 0, rhs, self.addr_size(rhs), dest)
            return dest

        if isinstance(node, c_ast.UnaryOp):
            if node.op in ("p++", "++", "p--", "--"):
                addr = self.var_addr(node.expr.name)
                self.emit("add" if "+" in node.op else "sub", 1, addr, self.addr_size(addr), addr)
                return addr
            raise NotImplementedError(f"unsupported unary operator '{node.op}'")

        if isinstance(node, c_ast.BinaryOp):
            immediate_ops = {"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "mod"}
            var_ops = {"+": "addv", "-": "subv", "*": "mulv", "/": "divv", "%": "modv"}
            if node.op not in immediate_ops:
                raise NotImplementedError(f"unsupported operator '{node.op}' in expression")
            left = self.gen_expr(node.left)
            left_size = self.addr_size(left)
            if isinstance(node.right, c_ast.Constant):
                result_size = left_size
                dest = self.alloc_temp(result_size)
                self.emit(immediate_ops[node.op], int(node.right.value, 0), left, left_size, dest)
            else:
                right = self.gen_expr(node.right)
                result_size = max(left_size, self.addr_size(right))
                dest = self.alloc_temp(result_size)
                self.emit(var_ops[node.op], left, left_size, right, self.addr_size(right), dest, result_size)
            return dest

        if isinstance(node, c_ast.FuncCall):
            name = node.name.name
            if name == "getkey":
                return self.gen_getkey()
            if name == "isflagsupported":
                return self.gen_isflagsupported(node)
            if name in ("flag", "setpixel"):
                raise NotImplementedError(f"'{name}' does not return a value")
            return self.gen_call(node)

        raise NotImplementedError(f"unsupported expression node '{type(node).__name__}'")

    # -- conditions: returns a 1-bit boolean address ---------------------------
    def gen_cond(self, node):
        comparisons = ("==", "!=", "<", ">", "<=", ">=")
        if isinstance(node, c_ast.BinaryOp) and node.op in comparisons:
            op = node.op
            bool_addr = self.alloc_temp()
            if isinstance(node.right, c_ast.Constant):
                left = self.gen_expr(node.left)
                rhs_val = int(node.right.value, 0)
                result = self.alloc_temp()
                self.emit("compare", rhs_val, left, self.addr_size(left), result, 2)
                code_for = {"==": 0, "<": 1, ">": 2}
                if op in code_for:
                    self.emit("isequal", code_for[op], result, 2, bool_addr)
                else:
                    inverse = {"!=": 0, "<=": 2, ">=": 1}[op]
                    self.emit("isequal", inverse, result, 2, bool_addr)
                    self.emit("not", bool_addr, 1, bool_addr, 1)
            else:
                left = self.gen_expr(node.left)
                right = self.gen_expr(node.right)
                result = self.alloc_temp()
                self.emit("comparev", left, self.addr_size(left), right, self.addr_size(right), result, 32)
                code_for = {"==": 0, ">": 1, "<": 2}
                if op in code_for:
                    self.emit("isequal", code_for[op], result, INT_SIZE, bool_addr)
                else:
                    inverse = {"!=": 0, "<=": 1, ">=": 2}[op]
                    self.emit("isequal", inverse, result, INT_SIZE, bool_addr)
                    self.emit("not", bool_addr, 1, bool_addr, 1)
            return bool_addr

        if isinstance(node, c_ast.Constant):
            bool_addr = self.alloc_temp()
            self.emit("mem", bool_addr, 1, 1 if int(node.value, 0) != 0 else 0)
            return bool_addr

        val = self.gen_expr(node)
        bool_addr = self.alloc_temp()
        self.emit("isequal", 0, val, self.addr_size(val), bool_addr)
        self.emit("not", bool_addr, 1, bool_addr, 1)
        return bool_addr

    # -- printf ----------------------------------------------------------------
    def gen_printf(self, call):
        args = call.args.exprs if call.args else []
        if not args or not isinstance(args[0], c_ast.Constant) or args[0].type != "string":
            raise NotImplementedError("printf's first argument must be a string literal")
        fmt = bytes(args[0].value[1:-1], "utf-8").decode("unicode_escape")
        rest = args[1:]

        parts = fmt.split("%d")
        trailing_newline = parts[-1].endswith("\n")
        if trailing_newline:
            parts[-1] = parts[-1][:-1]

        if len(parts) - 1 > len(rest):
            raise NotImplementedError("printf format has more %d specifiers than arguments")

        for i, literal in enumerate(parts):
            if literal:
                self.emit("printn", literal)
            if i < len(rest):
                addr = self.gen_expr(rest[i])
                self.emit("out", addr, self.addr_size(addr))
        if trailing_newline:
            self.emit("print", "")

    # -- graphics / keyboard builtins ------------------------------------------
    def gen_setpixel(self, call):
        # setpixel(x, y, val) — monochrome 64x64 graphics (flag(0, 1)) only.
        # Uses the runtime memory size (from gen_prologue's getmemsize call),
        # not a compile-time constant, so it stays correct if main.py's
        # `memsize` changes. Scratch addresses are allocated once and reused
        # across every call site (safe: each call's sequence of instructions
        # fully completes before the next one runs), instead of a fresh set
        # per call — since setpixel is usually called from inside an inlined
        # helper that itself gets inlined at several call sites, this avoids
        # multiplying scratch memory by every place that helper is used.
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 3:
            raise NotImplementedError("setpixel(x, y, val) takes exactly 3 arguments")
        x_node, y_node, val_node = args

        x_addr = self.gen_expr(x_node)
        y_addr = self.gen_expr(y_node)

        if self.setpixel_scratch is None:
            self.setpixel_scratch = {
                "y64": self.alloc_temp(),
                "xy": self.alloc_temp(),
                "base": self.alloc_temp(),
                "target_val": self.alloc_temp(),
                "bool_addr": self.alloc_temp(),
                "src_ptr": self.alloc_temp(),
            }
        s = self.setpixel_scratch

        self.emit("mul", 64, y_addr, INT_SIZE, s["y64"])
        self.emit("addv", x_addr, INT_SIZE, s["y64"], INT_SIZE, s["xy"], INT_SIZE)
        self.emit("sub", 1, self.runtime_memsize_addr, INT_SIZE, s["base"])  # base = memsize - 1
        self.emit("subv", s["base"], INT_SIZE, s["xy"], INT_SIZE, s["target_val"], INT_SIZE)

        if isinstance(val_node, c_ast.Constant):
            self.emit("mem", s["bool_addr"], 1, 1 if int(val_node.value, 0) != 0 else 0)
        else:
            computed_bool = self.gen_cond(val_node)
            self.emit("add", 0, computed_bool, 1, s["bool_addr"])

        self.emit("mem", s["src_ptr"], INT_SIZE, s["bool_addr"])
        self.emit("setmem", s["src_ptr"], 1, s["target_val"], 1)

    def gen_getkey(self):
        # Reads the 8-bit NES-style keyboard state (see main.py's update_keyboard).
        # ASSUMES flag(0, 1) (mono 64x64 graphics) and flag(1, 1) (NES keyboard)
        # have already been set and stay set. Uses the runtime memory size
        # (not a compile-time constant) plus pointer-indirect addressing
        # (setmem) to find the keyboard byte, since its address depends on
        # the VM's actual memory size, known only at runtime.
        self.uses_graphics = True
        if self.getkey_scratch is None:
            self.getkey_scratch = {
                "ptr": self.alloc_temp(),
                "dest": self.alloc_temp(),
                "dest_ptr": self.alloc_temp(),
            }
        s = self.getkey_scratch

        # ptr = runtime_memsize - (framebuffer bits + keyboard bits) = keyboard's bit address
        self.emit("sub", GFX_64x64_MONO_BITS + KEYBOARD_BITS, self.runtime_memsize_addr, INT_SIZE, s["ptr"])

        # The VM's bitfields are MSB-anchored, so an 8-bit value must be stored
        # at the right edge of a 32-bit temp to preserve its plain integer
        # value when later read back as a 32-bit int (writing it at offset 0
        # would make it 256x too large). dest_ptr holds the (compile-time
        # constant) address of that right edge, so setmem writes there.
        self.emit("mem", s["dest_ptr"], INT_SIZE, s["dest"] + (INT_SIZE - 8))
        self.emit("setmem", s["ptr"], 8, s["dest_ptr"], 8)
        return s["dest"]

    def gen_isflagsupported(self, call):
        # isflagsupported(flag, val) -> 1 if that flag/val combo is supported.
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("isflagsupported(flag, val) takes exactly 2 arguments")
        if not isinstance(args[0], c_ast.Constant) or not isinstance(args[1], c_ast.Constant):
            raise NotImplementedError("isflagsupported(flag, val) requires constant arguments")
        flag = int(args[0].value, 0)
        val = int(args[1].value, 0)
        dest = self.alloc_temp()
        self.emit("isflagsupported", flag, val, dest + (INT_SIZE - 1), 1)
        return dest

    # -- functions (compiled by inlining at each call site) -----------------------
    def gen_call(self, node):
        name = node.name.name
        args = node.args.exprs if node.args else []

        if name not in self.function_asts:
            raise NotImplementedError(f"unsupported/undeclared function call '{name}'")
        if name in self.inlining_stack:
            chain = " -> ".join(self.inlining_stack + [name])
            raise NotImplementedError(
                f"recursion is not supported ({chain}) — the compiler inlines function "
                "calls, so a recursive call would mean infinite code."
            )

        fn = self.function_asts[name]
        params = fn.decl.type.args.params if fn.decl.type.args else []
        if len(args) != len(params):
            raise NotImplementedError(f"'{name}' expects {len(params)} argument(s), got {len(args)}")

        instance = self.next_instance
        self.next_instance += 1
        prefix = f"{name}#{instance}."

        # Evaluate arguments *before* shadowing names, so `foo(x)` uses the
        # caller's x even if the callee also has a parameter named x.
        arg_vals = [self.gen_expr(a) for a in args]

        shadowed = {}  # name -> previous address (or NO_PREV sentinel) to restore after inlining
        NO_PREV = object()
        for p, val in zip(params, arg_vals):
            param_size = self.type_size(p.type)
            addr = self.alloc_var(prefix + p.name, param_size)
            self.emit("add", 0, val, self.addr_size(val), addr)
            shadowed[p.name] = (
                self.vars.get(p.name, NO_PREV),
                self.var_sizes.get(p.name, NO_PREV),
            )
            self.vars[p.name] = addr
            self.var_sizes[p.name] = param_size

        ret_addr = self.alloc_var(prefix + "__ret", self.function_return_size(fn))
        end_label = Label(f"end_{prefix}")

        self.inlining_stack.append(name)
        self.return_stack.append((ret_addr, end_label))
        self.gen_stmt(fn.body)
        self.return_stack.pop()
        self.inlining_stack.pop()
        self.define_label(end_label)

        for pname, (prev, prev_size) in shadowed.items():
            if prev is NO_PREV:
                del self.vars[pname]
                del self.var_sizes[pname]
            else:
                self.vars[pname] = prev
                self.var_sizes[pname] = prev_size

        return ret_addr

    # -- statements --------------------------------------------------------------
    def gen_stmt(self, node):
        if node is None:
            return

        if isinstance(node, c_ast.Compound):
            for item in node.block_items or []:
                self.gen_stmt(item)
            return

        if isinstance(node, c_ast.Decl):
            size = self.type_size(node.type)
            addr = self.alloc_var(node.name, size)
            if node.init is not None:
                rhs = self.gen_expr(node.init)
                self.emit("add", 0, rhs, self.addr_size(rhs), addr)
            return

        if isinstance(node, (c_ast.Assignment, c_ast.UnaryOp)):
            self.gen_expr(node)
            return

        if isinstance(node, c_ast.FuncCall):
            name = node.name.name
            if name == "printf":
                self.gen_printf(node)
            elif name == "flag":
                self.uses_graphics = True
                args = node.args.exprs
                self.emit("flag", int(args[0].value, 0), int(args[1].value, 0))
            elif name == "setpixel":
                self.gen_setpixel(node)
            elif name == "sleep":
                args = node.args.exprs if node.args else []
                if len(args) != 1 or not isinstance(args[0], c_ast.Constant):
                    raise NotImplementedError("sleep(ms) takes exactly 1 constant argument")
                self.emit("sleep", int(args[0].value, 0))
            elif name == "refreshscreen":
                self.emit("refreshscreen")
            else:
                self.gen_expr(node)  # covers getkey, isflagsupported, user functions; value discarded
            return

        if isinstance(node, c_ast.If):
            bool_addr = self.gen_cond(node.cond)
            label_else = Label("else")
            label_end = Label("endif")
            self.emit("ifnot", bool_addr, 1, label_else if node.iffalse else label_end)
            self.gen_stmt(node.iftrue)
            if node.iffalse:
                self.emit("setpc", label_end)
                self.define_label(label_else)
                self.gen_stmt(node.iffalse)
            self.define_label(label_end)
            return

        if isinstance(node, c_ast.While):
            label_start = Label("loop")
            label_end = Label("endloop")
            self.define_label(label_start)
            bool_addr = self.gen_cond(node.cond)
            self.emit("ifnot", bool_addr, 1, label_end)
            self.gen_stmt(node.stmt)
            self.emit("setpc", label_start)
            self.define_label(label_end)
            return

        if isinstance(node, c_ast.Return):
            if self.return_stack:
                ret_addr, end_label = self.return_stack[-1]
                if node.expr is not None:
                    rhs = self.gen_expr(node.expr)
                    self.emit("add", 0, rhs, self.addr_size(rhs), ret_addr)
                self.emit("setpc", end_label)
            return  # main()'s return value is ignored; 'exit' is emitted at the end

        raise NotImplementedError(f"unsupported statement '{type(node).__name__}'")

    # -- entry point ---------------------------------------------------------
    def compile(self, source):
        clean_source = "\n".join(
            line for line in source.splitlines() if not line.strip().startswith("#")
        )
        builtin_typedefs = "\n".join(
            f"typedef unsigned char {name};" for name in TYPE_BITS
        )
        clean_source = builtin_typedefs + "\n" + clean_source
        ast = pycparser.CParser().parse(clean_source)

        func_defs = [n for n in ast.ext if isinstance(n, c_ast.FuncDef)]
        main_fn = next((f for f in func_defs if f.decl.name == "main"), None)
        other_fns = [f for f in func_defs if f.decl.name != "main"]
        if main_fn is None:
            raise NotImplementedError("no main() function found")

        self.function_return_size(main_fn)

        for fn in other_fns:
            self.function_asts[fn.decl.name] = fn

        self.gen_prologue()
        self.gen_stmt(main_fn.body)
        self.emit("exit")

        # Now that we know the program's total memory usage, patch the
        # prologue's placeholder "required bits" value. If the program uses
        # graphics, the framebuffer/keyboard also has to fit alongside the
        # program's own variables, so include that in the requirement.
        required_bits = self.next_addr
        if self.uses_graphics:
            required_bits += GFX_64x64_MONO_BITS + KEYBOARD_BITS
        pos = self.required_bits_patch_pos
        self.buf[pos:pos + 4] = required_bits.to_bytes(4, "big")

        return self.finalize()


def main():
    if len(sys.argv) < 2:
        print("Usage: python compiler.py input.c [-o output.ulc]")
        sys.exit(1)

    in_path = sys.argv[1]
    out_path = "out.ulc"
    if "-o" in sys.argv:
        out_path = sys.argv[sys.argv.index("-o") + 1]

    with open(in_path, "r") as f:
        source = f.read()

    bytecode = Compiler().compile(source)

    with open(out_path, "wb") as f:
        f.write(bytecode)

    print(f"Compiled {in_path} -> {out_path} ({len(bytecode)} bytes)")


if __name__ == "__main__":
    main()
