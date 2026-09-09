'''
uniClang -> unilang bytecode (.ulc) compiler by AzizBgBoss
https://github.com/AzizBgBoss/unilang

Source files use the .uc extension; compiled bytecode uses .ulc.
Requires pycparser:  pip install pycparser

See README.md for full documentation of the uniClang subset and the .ulc bytecode format.

Function calls are implemented by INLINING the callee's body at the call site
(with fresh, uniquely-named parameter/local/return addresses per call) rather
than using the VM's 'subroutine'/'return' opcodes. This means:
  - functions can freely call other functions, including from inside other
    functions (not just from main) -- inlining just flattens it all out.
  - `return` inside a function is compiled as "write result, then setpc to
    the end of this inlined copy" -- so early returns work fine.
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
runtime size against how many bytes the program actually needed at compile
time and prints a warning (it still runs -- it just may misbehave, e.g. by
colliding with the graphics framebuffer) if there isn't enough room.

REGISTER/BYTE-ONLY REWRITE (matches main.py's clean ISA):
  - Memory is byte-addressable; every scalar type (bool, char, uint1..uint8,
    uint16, uint32) now occupies a whole number of bytes: 1 byte for anything
    uint8-or-narrower (bool/char/uint1-8), 2 bytes for uint16, 4 for uint32.
    The old sub-byte bit-packing (e.g. eight uint1's sharing one byte) is
    GONE -- uint1..uint4 are now identical to uint8 in storage (1 byte each).
    This trades some memory density for a much simpler, faster codegen.
  - All arithmetic/logic/comparison happens by loading operands into two
    scratch registers (r14, r15), doing the op, and storing the result back
    to memory -- registers are never allocated to persist across statements,
    since this compiler's model (inline everything, no real stack) doesn't
    need that yet. r0-r13 are unused by the compiler and free for hand-written
    inline register-asm blocks in the future if that's ever added.
  - Pointer dereferencing and array-element access with a runtime index now
    compile to `loadregi*/storeregi*` (address held in a register), replacing
    the old `setmem` double-indirection opcode.
  - `romread(pos, size)`'s `size` argument is now a BYTE count (1, 2, or 4),
    not a bit count -- same for __mem_load/__mem_store's size argument.
  - `alloc <N> name;` still takes N in bits for source-compatibility, but now
    rounds up to ceil(N/8) bytes -- `alloc 4 x;` and `alloc 8 x;` both now
    allocate a single byte.
'''

import re
import sys
import pycparser
from pycparser import c_ast


def strip_comments(source):
    """Remove // and /* */ comments before handing source to pycparser,
    which (unlike a real compiler) expects comments to already be gone —
    normally the C preprocessor (cpp) does this, but this pipeline doesn't
    run one. Preserves newlines (so line numbers in parse errors stay
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
            i += 1
        else:
            out.append(c)
        i += 1
    return "".join(out)

def apply_defines(source):
    """Minimal #define support: object-like (#define X val) and
    function-like (#define f(x) expr) macros, textually substituted."""
    defines = {}  # name -> (params or None, body)
    obj_re = re.compile(r'^#define\s+(\w+)\s+(.*)$')
    func_re = re.compile(r'^#define\s+(\w+)\((\s*\w+(?:\s*,\s*\w+)*\s*)?\)\s*(.*)$')
    out_lines = []
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith("#define"):
            m = func_re.match(stripped)
            if m and "(" in stripped.split(None, 2)[1]:
                name, params, body = m.group(1), m.group(2) or "", m.group(3)
                params = [p.strip() for p in params.split(",")] if params.strip() else []
                defines[name] = (params, body.strip())
            else:
                m = obj_re.match(stripped)
                if m:
                    defines[m.group(1)] = (None, m.group(2).strip())
            continue  # drop the #define line itself
        out_lines.append(line)
    source = "\n".join(out_lines)

    # Repeatedly expand macros in the body text (handles nested macro use).
    for _ in range(8):  # bounded passes instead of tracking a changed-flag per token
        changed = False
        for name, (params, body) in defines.items():
            if params is None:
                pattern = re.compile(r'\b' + re.escape(name) + r'\b')
                new_source, n = pattern.subn(body, source)
                if n:
                    source = new_source
                    changed = True
            else:
                call_re = re.compile(r'\b' + re.escape(name) + r'\s*\(([^()]*)\)')
                def _expand(m, params=params, body=body):
                    args = [a.strip() for a in m.group(1).split(",")] if m.group(1).strip() else []
                    text = body
                    for p, a in zip(params, args):
                        text = re.sub(r'\b' + re.escape(p) + r'\b', a, text)
                    return "(" + text + ")"
                new_source, n = call_re.subn(_expand, source)
                if n:
                    source = new_source
                    changed = True
        if not changed:
            break
    return source




# ---------------------------------------------------------------------------
# Opcode table (subset of main.py's ISA needed by this compiler)
# name: (opcode byte, [operand widths in bytes, 0 = length-prefixed text])
# ---------------------------------------------------------------------------
OPS = {
    "flag":            (0x01, [1, 1]),               # flag, val
    "isflagsupported": (0x02, [1, 1, 1]),            # flag, val, reg
    "setbyte":         (0x03, [4, 1]),               # addr, byte
    "write":           (0x04, [4, 0]),               # addr, text
    "outc":            (0x05, [4]),                  # addr
    "out":             (0x06, [4]),                  # addr (single byte, 0-255)
    "input":           (0x07, [4, 4]),                # chars, addr
    "print":           (0x08, [0]),                  # text
    "printn":          (0x09, [0]),                  # text
    "printv":          (0x0A, [4, 4]),                # chars, addr
    "printvn":         (0x0B, [4, 4]),                # chars, addr
    "setpc":           (0x0F, [4]),                  # pos
    "sleep":           (0x12, [4]),                  # ms
    "refreshscreen":   (0x13, []),
    "romread8":        (0x14, [1, 1]),                # reg_addr, reg_dst
    "romread16":       (0x15, [1, 1]),
    "romread32":       (0x16, [1, 1]),
    "setpixel":        (0x17, [1, 1, 1]),             # regx, regy, regc
    "getpixel":        (0x18, [1, 1, 1]),             # regx, regy, regdst
    "getkeyboard":     (0x19, [1]),                   # reg
    "gettime":         (0x1A, [1]),                   # reg
    "getmemsize":      (0x1B, [1]),                   # reg
    "getflag":         (0x1C, [1, 1]),                # flag, reg

    "setreg":          (0x40, [1, 4]),                # reg, val
    "movreg":          (0x41, [1, 1]),                # dst, src
    "loadreg8":        (0x42, [1, 4]),                # reg, addr
    "loadreg16":       (0x43, [1, 4]),
    "loadreg32":       (0x44, [1, 4]),
    "storereg8":       (0x45, [1, 4]),                # reg, addr
    "storereg16":      (0x46, [1, 4]),
    "storereg32":      (0x47, [1, 4]),
    "addreg":          (0x48, [1, 1, 1]),             # r1, r2, dst
    "subreg":          (0x49, [1, 1, 1]),
    "mulreg":          (0x4A, [1, 1, 1]),
    "divreg":          (0x4B, [1, 1, 1]),
    "modreg":          (0x4C, [1, 1, 1]),
    "addregv":         (0x4D, [1, 4, 1]),             # r1, const, dst
    "subregv":         (0x4E, [1, 4, 1]),
    "mulregv":         (0x4F, [1, 4, 1]),
    "compreg":         (0x50, [1, 1, 1]),
    "ifreg":           (0x51, [1, 4]),                # reg, pos
    "ifnotreg":        (0x52, [1, 4]),
    "notreg":          (0x55, [1, 1]),                # r1, dst
    "orreg":           (0x56, [1, 1, 1]),
    "andreg":          (0x57, [1, 1, 1]),
    "norreg":          (0x58, [1, 1, 1]),
    "nandreg":         (0x59, [1, 1, 1]),
    "xorreg":          (0x5A, [1, 1, 1]),
    "xnorreg":         (0x5B, [1, 1, 1]),
    "shlreg":          (0x5C, [1, 1, 1]),
    "shrreg":          (0x5D, [1, 1, 1]),

    "loadregi8":       (0x60, [1, 1]),                # reg_addr, reg_dst
    "loadregi16":      (0x61, [1, 1]),
    "loadregi32":      (0x62, [1, 1]),
    "storeregi8":      (0x63, [1, 1]),                # reg_addr, reg_val
    "storeregi16":     (0x64, [1, 1]),
    "storeregi32":     (0x65, [1, 1]),

    "eqreg":           (0x66, [1, 1, 1]),
    "neqreg":          (0x67, [1, 1, 1]),
    "ltreg":           (0x68, [1, 1, 1]),
    "gtreg":           (0x69, [1, 1, 1]),
    "lereg":           (0x6A, [1, 1, 1]),
    "gereg":           (0x6B, [1, 1, 1]),
    "outreg":          (0x6C, [1]),                   # reg -- print full numeric value

    "exit":            (0xFF, []),
}

# Two scratch registers used for every arithmetic/logic/comparison emission.
# They're never left "live" across other emit() calls -- each helper below
# does load(s) -> op -> store as one atomic sequence, so reusing the same
# two registers everywhere is safe.
RA, RB = 14, 15

TYPE_BYTES = {
    "bool": 1,
    "char": 1,
    "uint1": 1,
    "uint2": 1,
    "uint4": 1,
    "uint8": 1,
    "uint16": 2,
    "uint32": 4,
}
DEFAULT_TYPE = "uint32"
INT_SIZE = TYPE_BYTES[DEFAULT_TYPE]  # width used by VM pointers and compiler scratch (4 bytes)
VARS_START = 0          # byte address where user variables start


class Label:
    """A jump target resolved once its position in the bytecode is known."""
    def __init__(self, name="L"):
        self.name = name
        self.addr = None


class Compiler:
    def __init__(self):
        self.buf = bytearray()
        self.patches = []          # (byte_offset_in_buf, Label, width)
        self.vars = {}              # name -> byte address (current scope view)
        self.var_sizes = {}         # name -> byte width (current scope view)
        self.addr_sizes = {}        # allocated address -> byte width
        self.alloc_meta = {}        # name -> {"kind": "scalar"|"array", "bits":..., "count":...}
        self.next_addr = VARS_START
        self.next_temp = 0
        self.function_asts = {}     # name -> FuncDef node
        self.inlining_stack = []    # names currently being inlined (recursion guard)
        self.return_stack = []      # (ret_addr, end_label) for the innermost inlined call
        self.next_instance = 0

        self.runtime_memsize_addr = None   # set by gen_prologue(); holds getmemsize()'s result
        self.uses_graphics = False         # set True by flag()/setpixel()/getkey()
        self.required_bytes_patch_pos = None  # byte offset to patch once we know final memory usage
        self.array_elem_bytes = {}   # array variable name -> element width in bytes

        self.const_arrays = {}       # name -> {base_addr, elem_bytes, count}
        self.rom_data = []           # {"patch_pos", "data"}

    @staticmethod
    def rewrite_alloc_syntax(source):
        alloc_meta = {}

        def replace_alloc(match):
            item_bits = int(match.group(1))
            count = match.group(2)
            name = match.group(3)
            array_count = match.group(4)

            if count is not None:
                array_count = int(count)
                if array_count <= 0:
                    raise NotImplementedError(f"alloc<{item_bits}> cannot allocate a non-positive array size")
                alloc_meta[name] = {"kind": "array", "bits": item_bits, "count": array_count}
                return f"uint32 {name}[{array_count}];"

            if array_count is not None:
                array_count = int(array_count)
                if array_count <= 0:
                    raise NotImplementedError(f"alloc<{item_bits}> cannot allocate a non-positive array size")
                alloc_meta[name] = {"kind": "array", "bits": item_bits, "count": array_count}
                return f"uint32 {name}[{array_count}];"

            alloc_meta[name] = {"kind": "scalar", "bits": item_bits}
            return f"uint32 {name};"

        pattern = re.compile(r"(?m)^\s*alloc\s+(\d+)\s*(?:\*\s*(\d+)\s*)?([A-Za-z_]\w*)\s*(?:\[(\d+)\])?\s*;\s*$")
        rewritten = pattern.sub(replace_alloc, source)
        return rewritten, alloc_meta

    @staticmethod
    def rewrite_mem_syntax(source):
        lines = []
        for line in source.split('\n'):
            if 'MEMORY' not in line or line.strip().startswith('printf'):
                lines.append(line)
            else:
                line = re.sub(r"MEMORY\s*\[([^:\]]+)\s*:\s*([^\]]+)\]\s*=\s*([^;]+);", r"__mem_store(\1, \2, \3);", line)
                line = re.sub(r"=\s*MEMORY\s*\[([^:\]]+)\s*:\s*([^\]]+)\]", r"= __mem_load(\1, \2)", line)
                line = re.sub(r"&\s*MEMORY\s*\[([^:\]]+)\s*:\s*([^\]]+)\]", r"__mem_ptr(\1, \2)", line)
                lines.append(line)
        return '\n'.join(lines)

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
    def _reg_width(size_bytes):
        if size_bytes not in (1, 2, 4):
            raise NotImplementedError(f"unsupported operand width ({size_bytes} bytes) -- only 1, 2, or 4 byte values are supported")
        return size_bytes * 8

    # -- register-level emission helpers --------------------------------------
    def emit_load(self, addr, size, reg):
        self.emit(f"loadreg{self._reg_width(size)}", reg, addr)

    def emit_store(self, reg, addr, size):
        self.emit(f"storereg{self._reg_width(size)}", reg, addr)

    def emit_set_const(self, reg, val):
        self.emit("setreg", reg, val)

    def emit_store_const(self, addr, size, val):
        # Sets a memory location to a compile-time-known constant.
        self.emit_set_const(RA, val)
        self.emit_store(RA, addr, size)

    def emit_copy(self, src, dest):
        # loadreg zero-extends into a full 32-bit register and storereg
        # truncates back down, so narrowing/widening between different-sized
        # variables just falls out of an ordinary load+store -- no special
        # casing needed (unlike the old bit-field right-justification model).
        self.emit_load(src, self.addr_size(src), RA)
        self.emit_store(RA, dest, self.addr_size(dest))

    def to_bool(self, addr):
        # Normalizes any value to a 1-byte 0/1 address (true iff addr != 0).
        dest = self.alloc_temp(1)
        self.emit_load(addr, self.addr_size(addr), RA)
        self.emit_set_const(RB, 0)
        self.emit("neqreg", RA, RB, RA)
        self.emit_store(RA, dest, 1)
        return dest

    def resolve_operand(self, node):
        # Fully evaluates an operand to either a compile-time constant or a
        # stable memory address, WITHOUT touching RA/RB as a side effect that
        # could be observed later. This must happen before loading any other
        # operand into a register -- if we loaded operand A into RA and then
        # resolved a compound operand B, evaluating B could itself use RA/RB
        # as scratch and clobber A before the caller ever uses it. Resolving
        # both operands first (each landing in its own temp/constant) and
        # only THEN loading them into registers, back to back, avoids that.
        if isinstance(node, c_ast.Constant) and node.type in ("int", "char"):
            return ("const", self.const_value(node))
        addr = self.gen_expr(node)
        return ("addr", addr, self.addr_size(addr))

    def load_resolved(self, resolved, reg):
        if resolved[0] == "const":
            self.emit_set_const(reg, resolved[1])
            return INT_SIZE
        _, addr, size = resolved
        self.emit_load(addr, size, reg)
        return size

    def load_operand(self, node, reg):
        # Only safe to use when nothing else is depending on a register's
        # contents surviving this call -- i.e. when this is the ONLY operand
        # being loaded right now (see resolve_operand/load_resolved for the
        # two-operand case, which must not interleave evaluation and loading).
        return self.load_resolved(self.resolve_operand(node), reg)

    def gen_bool_operand(self, node):
        # Like to_bool(gen_expr(node)), but skips the redundant
        # load+compare-to-zero+store normalization when node is already
        # guaranteed to be a 0/1 value (a comparison or another &&/||),
        # since gen_cond already produces exactly that.
        comparisons = ("==", "!=", "<", ">", "<=", ">=")
        if isinstance(node, c_ast.BinaryOp) and (node.op in comparisons or node.op in ("&&", "||")):
            return self.gen_expr(node)
        return self.to_bool(self.gen_expr(node))

    @staticmethod
    def type_name(type_node):
        if not isinstance(type_node, c_ast.TypeDecl) or not isinstance(type_node.type, c_ast.IdentifierType):
            raise NotImplementedError("only fixed-width unsigned types are supported")
        names = type_node.type.names
        if len(names) != 1 or names[0] not in TYPE_BYTES:
            raise NotImplementedError("only bool, char, uint1, uint2, uint4, uint8, uint16, and uint32 are supported")
        return names[0]

    @classmethod
    def type_size(cls, type_node):
        if isinstance(type_node, c_ast.ArrayDecl):
            elem_size = cls.type_size(type_node.type)
            if isinstance(type_node.dim, c_ast.Constant):
                count = int(type_node.dim.value, 0)
                return elem_size * count
            return elem_size
        if isinstance(type_node, c_ast.TypeDecl):
            return TYPE_BYTES[cls.type_name(type_node)]
        raise NotImplementedError(f"unsupported type node '{type(type_node).__name__}'")

    def function_return_size(self, fn):
        return self.type_size(fn.decl.type.type)

    def param_size(self, param):
        if isinstance(param.type, c_ast.ArrayDecl):
            return INT_SIZE
        return self.type_size(param.type)

    @staticmethod
    def is_const_decl(node):
        return "const" in getattr(node, "quals", []) or "const" in getattr(node.type, "quals", [])

    @staticmethod
    def const_value(node):
        if not isinstance(node, c_ast.Constant):
            raise NotImplementedError("const array values must be compile-time constants")
        if node.type == "char":
            return ord(bytes(node.value[1:-1], "utf-8").decode("unicode_escape"))
        return int(node.value, 0)

    @staticmethod
    def string_bytes(node):
        if not isinstance(node, c_ast.Constant) or node.type != "string":
            raise NotImplementedError("expected a string literal")
        return list(bytes(node.value[1:-1], "utf-8").decode("unicode_escape").encode("utf-8"))

    @staticmethod
    def pack_values(values, elem_bytes):
        packed = bytearray()
        for value in values:
            if value < 0 or value >= (1 << (elem_bytes * 8)):
                raise ValueError(f"constant value {value} does not fit in {elem_bytes} byte(s)")
            packed += value.to_bytes(elem_bytes, "big")
        return bytes(packed)

    def add_const_array(self, name, elem_bytes, values):
        base = self.alloc_temp(INT_SIZE)
        instr_start = len(self.buf)
        # placeholder ROM offset, patched in finalize() once we know it
        self.emit("setreg", RA, 0)
        patch_pos = instr_start + 1 + 1  # opcode(1) + reg(1), then the 4-byte val
        self.emit_store(RA, base, INT_SIZE)
        self.const_arrays[name] = {"base": base, "elem_bytes": elem_bytes, "count": len(values)}
        self.array_elem_bytes[name] = elem_bytes
        self.rom_data.append({"patch_pos": patch_pos, "data": self.pack_values(values, elem_bytes)})
        return base

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
        byte_offset = len(self.buf)
        for item in self.rom_data:
            self.buf[item["patch_pos"]:item["patch_pos"] + 4] = byte_offset.to_bytes(4, "big")
            byte_offset += len(item["data"])
        for item in self.rom_data:
            self.buf.extend(item["data"])
        return bytes(self.buf)

    # -- startup prologue: query real memory size, warn if too small ------------
    def gen_prologue(self):
        self.runtime_memsize_addr = self.alloc_temp()
        self.emit("getmemsize", RA)
        self.emit_store(RA, self.runtime_memsize_addr, INT_SIZE)

        required_addr = self.alloc_temp()
        instr_start = len(self.buf)
        self.emit("setreg", RA, 0)  # placeholder; patched once compilation finishes
        self.required_bytes_patch_pos = instr_start + 1 + 1
        self.emit_store(RA, required_addr, INT_SIZE)

        too_small = self.alloc_temp(1)
        self.emit_load(self.runtime_memsize_addr, INT_SIZE, RA)
        self.emit_load(required_addr, INT_SIZE, RB)
        self.emit("ltreg", RA, RB, RA)  # RA = 1 if runtime memsize < required
        self.emit_store(RA, too_small, 1)

        label_ok = Label("memsize_ok")
        self.emit_load(too_small, 1, RA)
        self.emit("ifnotreg", RA, label_ok)
        self.emit("printn", "WARNING: this program needs at least ")
        self.emit_load(required_addr, INT_SIZE, RA)
        self.emit("outreg", RA)
        self.emit("printn", " bytes of memory, but only ")
        self.emit_load(self.runtime_memsize_addr, INT_SIZE, RA)
        self.emit("outreg", RA)
        self.emit("print", " are available. Increase 'memsize' in main.py.")
        self.define_label(label_ok)

    # -- expressions: returns the byte-address holding the result --------------
    def gen_expr(self, node):
        if isinstance(node, c_ast.Constant):
            if node.type == "string":
                name = f"__str{len(self.rom_data)}"
                return self.add_const_array(name, TYPE_BYTES["char"], self.string_bytes(node) + [0])
            if node.type not in ("int", "char"):
                raise NotImplementedError(f"unsupported constant type '{node.type}'")
            val = self.const_value(node)
            dest = self.alloc_temp(INT_SIZE)
            self.emit_store_const(dest, INT_SIZE, val)
            return dest

        if isinstance(node, c_ast.ID):
            if node.name in ("true", "false"):
                dest = self.alloc_temp(INT_SIZE)
                self.emit_store_const(dest, INT_SIZE, 1 if node.name == "true" else 0)
                return dest
            if node.name in self.const_arrays:
                return self.const_arrays[node.name]["base"]
            return self.var_addr(node.name)

        if isinstance(node, c_ast.ArrayRef):
            if isinstance(node.name, c_ast.ID) and node.name.name in self.const_arrays:
                const_array = self.const_arrays[node.name.name]
                elem_bytes = const_array["elem_bytes"]
                index_addr = self.gen_expr(node.subscript)
                self.emit_load(index_addr, self.addr_size(index_addr), RA)
                self.emit("mulregv", RA, elem_bytes, RA)          # RA = index * elem_bytes
                self.emit_load(const_array["base"], INT_SIZE, RB)  # RB = ROM base offset
                self.emit("addreg", RA, RB, RA)                    # RA = absolute ROM offset
                self.emit(f"romread{elem_bytes * 8}", RA, RB)      # RB = value read from ROM
                result = self.alloc_temp(elem_bytes)
                self.emit_store(RB, result, elem_bytes)
                return result

            if not isinstance(node.name, c_ast.ID):
                raise NotImplementedError("only simple array names can be indexed")
            base_addr = self.var_addr(node.name.name)
            elem_bytes = self.array_elem_bytes.get(node.name.name, INT_SIZE)
            index_addr = self.gen_expr(node.subscript)
            self.emit_load(index_addr, self.addr_size(index_addr), RA)
            self.emit("mulregv", RA, elem_bytes, RA)      # RA = index * elem_bytes
            self.emit("addregv", RA, base_addr, RA)       # RA = absolute runtime address
            self.emit(f"loadregi{elem_bytes * 8}", RA, RB)
            result = self.alloc_temp(elem_bytes)
            self.emit_store(RB, result, elem_bytes)
            return result

        if isinstance(node, c_ast.UnaryOp):
            if node.op == "&":
                if isinstance(node.expr, c_ast.ID):
                    # Must produce a temp whose VALUE is the address (so
                    # emit_copy/gen_expr consumers -- which always treat a
                    # gen_expr result as "a location to load from" -- see a
                    # location holding the address, not the address itself
                    # misread as a location).
                    addr_val = self.var_addr(node.expr.name)
                    out = self.alloc_temp(INT_SIZE)
                    self.emit_store_const(out, INT_SIZE, addr_val)
                    return out
                if isinstance(node.expr, c_ast.ArrayRef):
                    if not isinstance(node.expr.name, c_ast.ID):
                        raise NotImplementedError("only simple array names can be indexed")
                    base_addr = self.var_addr(node.expr.name.name)
                    elem_bytes = self.array_elem_bytes.get(node.expr.name.name, INT_SIZE)
                    index_addr = self.gen_expr(node.expr.subscript)
                    self.emit_load(index_addr, self.addr_size(index_addr), RA)
                    self.emit("mulregv", RA, elem_bytes, RA)
                    self.emit("addregv", RA, base_addr, RA)
                    out = self.alloc_temp(INT_SIZE)
                    self.emit_store(RA, out, INT_SIZE)
                    return out
                raise NotImplementedError("only identifiers and array elements can be addressed")

        if isinstance(node, c_ast.Assignment):
            if node.op != "=":
                raise NotImplementedError(f"unsupported assignment operator '{node.op}'")
            rhs = self.gen_expr(node.rvalue)
            dest = self.var_addr(node.lvalue.name)
            self.emit_copy(rhs, dest)
            return dest

        if isinstance(node, c_ast.UnaryOp):
            if node.op in ("p++", "++", "p--", "--"):
                addr = self.var_addr(node.expr.name)
                size = self.addr_size(addr)
                self.emit_load(addr, size, RA)
                self.emit("addregv" if "+" in node.op else "subregv", RA, 1, RA)
                self.emit_store(RA, addr, size)
                return addr
            raise NotImplementedError(f"unsupported unary operator '{node.op}'")

        if isinstance(node, c_ast.BinaryOp):
            comparisons = ("==", "!=", "<", ">", "<=", ">=")
            if node.op in comparisons:
                # Comparisons now produce a usable 0/1 value, not just a
                # condition for if/while -- gen_cond already builds exactly
                # that, so reuse it here.
                return self.gen_cond(node)

            if node.op in ("&&", "||"):
                left_bool = self.gen_bool_operand(node.left)
                right_bool = self.gen_bool_operand(node.right)
                self.emit_load(left_bool, 1, RA)
                self.emit_load(right_bool, 1, RB)
                self.emit("andreg" if node.op == "&&" else "orreg", RA, RB, RA)
                dest = self.alloc_temp(1)
                self.emit_store(RA, dest, 1)
                return dest

            bitwise_ops = {"&": "andreg", "|": "orreg", "^": "xorreg", "<<": "shlreg", ">>": "shrreg"}
            if node.op in bitwise_ops:
                left_r = self.resolve_operand(node.left)
                right_r = self.resolve_operand(node.right)
                left_size = self.load_resolved(left_r, RA)
                right_size = self.load_resolved(right_r, RB)
                # shl/shr keep the left operand's width; the others take the wider of the two.
                result_size = left_size if node.op in ("<<", ">>") else max(left_size, right_size)
                self.emit(bitwise_ops[node.op], RA, RB, RA)
                dest = self.alloc_temp(result_size)
                self.emit_store(RA, dest, result_size)
                return dest

            const_ops = {"+": "addregv", "-": "subregv", "*": "mulregv"}
            reg_ops = {"+": "addreg", "-": "subreg", "*": "mulreg", "/": "divreg", "%": "modreg"}
            if node.op not in reg_ops:
                raise NotImplementedError(f"unsupported operator '{node.op}' in expression")
            right_is_const = isinstance(node.right, c_ast.Constant)
            left_is_const = isinstance(node.left, c_ast.Constant)
            if right_is_const and node.op in const_ops:
                left_size = self.load_operand(node.left, RA)
                self.emit(const_ops[node.op], RA, self.const_value(node.right), RA)
                result_size = left_size
            elif left_is_const and node.op in ("+", "*"):
                # commutative -- swap so the constant still uses the cheaper *v opcode
                right_size = self.load_operand(node.right, RA)
                self.emit(const_ops[node.op], RA, self.const_value(node.left), RA)
                result_size = right_size
            else:
                left_r = self.resolve_operand(node.left)
                right_r = self.resolve_operand(node.right)
                left_size = self.load_resolved(left_r, RA)
                right_size = self.load_resolved(right_r, RB)
                self.emit(reg_ops[node.op], RA, RB, RA)
                result_size = max(left_size, right_size)
            dest = self.alloc_temp(result_size)
            self.emit_store(RA, dest, result_size)
            return dest

        if isinstance(node, c_ast.FuncCall):
            name = node.name.name
            if name == "romread":
                args = node.args.exprs if node.args else []
                if len(args) != 2 or not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("romread(pos, size) takes a ROM byte-position expression and a constant size (1, 2, or 4 bytes)")
                pos = self.gen_expr(args[0])
                size = int(args[1].value, 0)
                self.emit_load(pos, self.addr_size(pos), RA)
                self.emit(f"romread{self._reg_width(size)}", RA, RB)
                dest = self.alloc_temp(size)
                self.emit_store(RB, dest, size)
                return dest
            if name == "getkey":
                return self.gen_getkey()
            if name == "getpixel":
                return self.gen_getpixel(node)
            if name == "isflagsupported":
                return self.gen_isflagsupported(node)
            if name == "input":
                args = node.args.exprs if node.args else []
                if len(args) != 1:
                    raise NotImplementedError("input(&name) takes exactly one pointer argument")
                arg = args[0]
                if isinstance(arg, c_ast.UnaryOp) and arg.op == "&":
                    target = self.gen_expr(arg.expr)
                elif isinstance(arg, c_ast.ID):
                    target = self.var_addr(arg.name)
                else:
                    raise NotImplementedError("input() requires the address of a variable, e.g. input(&name)")
                size = self.addr_size(target)
                self.emit("input", size, target)
                return target
            if name == "__mem_load":
                args = node.args.exprs if node.args else []
                if len(args) != 2:
                    raise NotImplementedError("__mem_load(addr, size) takes exactly two arguments")
                if not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("__mem_load() size must be a compile-time constant (1, 2, or 4 bytes)")
                addr = self.gen_expr(args[0])
                size = int(args[1].value, 0)
                self.emit_load(addr, INT_SIZE, RA)  # RA = the runtime pointer value stored at addr
                self.emit(f"loadregi{self._reg_width(size)}", RA, RB)
                dest = self.alloc_temp(size)
                self.emit_store(RB, dest, size)
                return dest
            if name == "__mem_ptr":
                args = node.args.exprs if node.args else []
                if len(args) != 2:
                    raise NotImplementedError("__mem_ptr(addr, size) takes exactly two arguments")
                if not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("__mem_ptr() size must be a compile-time constant")
                return self.gen_expr(args[0])
            if name in ("flag", "setpixel", "exit"):
                raise NotImplementedError(f"'{name}' does not return a value")
            return self.gen_call(node)

        raise NotImplementedError(f"unsupported expression node '{type(node).__name__}'")

    # -- conditions: returns a 1-byte boolean address ---------------------------
    def gen_cond(self, node):
        comparisons = ("==", "!=", "<", ">", "<=", ">=")
        opmap = {"==": "eqreg", "!=": "neqreg", "<": "ltreg", ">": "gtreg", "<=": "lereg", ">=": "gereg"}
        if isinstance(node, c_ast.BinaryOp) and node.op in comparisons:
            left_r = self.resolve_operand(node.left)
            right_r = self.resolve_operand(node.right)
            self.load_resolved(left_r, RA)
            self.load_resolved(right_r, RB)
            self.emit(opmap[node.op], RA, RB, RA)
            bool_addr = self.alloc_temp(1)
            self.emit_store(RA, bool_addr, 1)
            return bool_addr

        if isinstance(node, c_ast.Constant):
            bool_addr = self.alloc_temp(1)
            self.emit_store_const(bool_addr, 1, 1 if self.const_value(node) != 0 else 0)
            return bool_addr

        val = self.gen_expr(node)
        return self.to_bool(val)

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
                self.emit_load(addr, self.addr_size(addr), RA)
                self.emit("outreg", RA)  # prints the register's full numeric value, any width
        if trailing_newline:
            self.emit("print", "")

    # -- graphics / keyboard builtins ------------------------------------------
    def gen_setpixel(self, call):
        # setpixel(x, y, val) compiles to a single native VM opcode (0x17)
        # operating on registers -- the mode/width/framebuffer-offset math
        # happens once, in the VM's own dispatch, per pixel.
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 3:
            raise NotImplementedError("setpixel(x, y, val) takes exactly 3 arguments")
        x_node, y_node, val_node = args

        x_addr = self.gen_expr(x_node)
        y_addr = self.gen_expr(y_node)
        if isinstance(val_node, c_ast.Constant):
            val_addr = self.alloc_temp(1)
            self.emit_store_const(val_addr, 1, 1 if self.const_value(val_node) != 0 else 0)
        else:
            val_addr = self.gen_cond(val_node)

        self.emit_load(x_addr, self.addr_size(x_addr), 10)
        self.emit_load(y_addr, self.addr_size(y_addr), 11)
        self.emit_load(val_addr, self.addr_size(val_addr), 12)
        self.emit("setpixel", 10, 11, 12)

    def gen_getpixel(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("getpixel(x, y) takes exactly 2 arguments")
        x_addr = self.gen_expr(args[0])
        y_addr = self.gen_expr(args[1])
        self.emit_load(x_addr, self.addr_size(x_addr), 10)
        self.emit_load(y_addr, self.addr_size(y_addr), 11)
        self.emit("getpixel", 10, 11, 12)
        dest = self.alloc_temp(1)
        self.emit_store(12, dest, 1)
        return dest

    def gen_getkey(self):
        self.uses_graphics = True
        self.emit("getkeyboard", 12)
        dest = self.alloc_temp(1)
        self.emit_store(12, dest, 1)
        return dest

    def gen_isflagsupported(self, call):
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("isflagsupported(flag, val) takes exactly 2 arguments")
        if not isinstance(args[0], c_ast.Constant) or not isinstance(args[1], c_ast.Constant):
            raise NotImplementedError("isflagsupported(flag, val) requires constant arguments")
        flag = int(args[0].value, 0)
        val = int(args[1].value, 0)
        self.emit("isflagsupported", flag, val, 12)
        dest = self.alloc_temp(1)
        self.emit_store(12, dest, 1)
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
                f"recursion is not supported ({chain}) -- the compiler inlines function "
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
            param_size = self.param_size(p)
            addr = self.alloc_var(prefix + p.name, param_size)
            self.emit_copy(val, addr)
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
            # Block-scope: variables declared inside this block must not
            # leak into (or shadow past the end of) the surrounding scope.
            saved_vars = dict(self.vars)
            saved_var_sizes = dict(self.var_sizes)
            for item in node.block_items or []:
                self.gen_stmt(item)
            self.vars = saved_vars
            self.var_sizes = saved_var_sizes
            return

        if isinstance(node, c_ast.Decl):
            if self.is_const_decl(node):
                if not isinstance(node.type, c_ast.ArrayDecl):
                    raise NotImplementedError("const currently supports initialized arrays only")
                elem_bytes = self.type_size(node.type.type)
                if isinstance(node.init, c_ast.Constant) and node.init.type == "string":
                    if elem_bytes != TYPE_BYTES["char"]:
                        raise NotImplementedError("string initializers require const char arrays")
                    values = self.string_bytes(node.init) + [0]
                elif isinstance(node.init, c_ast.InitList):
                    values = [self.const_value(item) for item in node.init.exprs]
                else:
                    raise NotImplementedError("const currently supports initialized arrays only")

                if node.type.dim is None:
                    count = len(values)
                elif isinstance(node.type.dim, c_ast.Constant):
                    count = int(node.type.dim.value, 0)
                else:
                    raise NotImplementedError("const array size must be a compile-time constant")
                if len(values) > count:
                    raise NotImplementedError(f"const array '{node.name}' has too many initializers")
                values.extend([0] * (count - len(values)))

                self.add_const_array(node.name, elem_bytes, values)
                return

            meta = self.alloc_meta.get(node.name)
            if meta is not None:
                # alloc<N> still specifies N in BITS for source compatibility;
                # round up to whole bytes since sub-byte packing is gone.
                elem_size = max(1, (meta["bits"] + 7) // 8)
                if meta["kind"] == "scalar":
                    size = elem_size
                else:
                    size = elem_size * meta["count"]
                    self.array_elem_bytes[node.name] = elem_size
            else:
                size = self.type_size(node.type)
                if isinstance(node.type, c_ast.ArrayDecl):
                    self.array_elem_bytes[node.name] = self.type_size(node.type.type)
            addr = self.alloc_var(node.name, size)
            if node.init is not None:
                if isinstance(node.init, c_ast.InitList):
                    if not isinstance(node.type, c_ast.ArrayDecl):
                        raise NotImplementedError("initializer lists are only supported for 1D arrays")
                    elem_bytes = self.type_size(node.type.type)
                    for i, item in enumerate(node.init.exprs):
                        if not isinstance(item, c_ast.Constant):
                            raise NotImplementedError("array initializer values must be compile-time constants")
                        elem_addr = addr + i * elem_bytes
                        self.addr_sizes[elem_addr] = elem_bytes
                        self.emit_store_const(elem_addr, elem_bytes, self.const_value(item))
                else:
                    rhs = self.gen_expr(node.init)
                    self.emit_copy(rhs, addr)
            return

        if isinstance(node, c_ast.DeclList):
            for d in node.decls:
                self.gen_stmt(d)
            return

        if isinstance(node, c_ast.For):
            if node.init:
                self.gen_stmt(node.init)
            label_start = Label("forloop")
            label_end = Label("endforloop")
            self.define_label(label_start)
            if node.cond is not None:
                bool_addr = self.gen_cond(node.cond)
                self.emit_load(bool_addr, 1, RA)
                self.emit("ifnotreg", RA, label_end)
            self.gen_stmt(node.stmt)
            if node.next:
                self.gen_stmt(node.next)
            self.emit("setpc", label_start)
            self.define_label(label_end)
            return

        if isinstance(node, (c_ast.Assignment, c_ast.UnaryOp)):
            self.gen_expr(node)
            return

        if isinstance(node, c_ast.FuncCall):
            name = node.name.name
            if name == "printf":
                self.gen_printf(node)
            elif name == "exit":
                self.emit("exit")
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
            elif name == "input":
                args = node.args.exprs if node.args else []
                if len(args) != 1:
                    raise NotImplementedError("input(&name) takes exactly one pointer argument")
                arg = args[0]
                if isinstance(arg, c_ast.UnaryOp) and arg.op == "&":
                    target = self.gen_expr(arg.expr)
                elif isinstance(arg, c_ast.ID):
                    target = self.var_addr(arg.name)
                else:
                    raise NotImplementedError("input() requires the address of a variable, e.g. input(&name)")
                size = self.addr_size(target)
                self.emit("input", size, target)
            elif name == "__mem_store":
                args = node.args.exprs if node.args else []
                if len(args) != 3:
                    raise NotImplementedError("__mem_store(addr, size, value) takes exactly three arguments")
                if not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("__mem_store() size must be a compile-time constant (1, 2, or 4 bytes)")
                addr = self.gen_expr(args[0])
                width = int(args[1].value, 0)
                value = self.gen_expr(args[2])
                self.emit_load(value, self.addr_size(value), RB)  # RB = value to store
                self.emit_load(addr, INT_SIZE, RA)                # RA = target runtime address
                self.emit(f"storeregi{self._reg_width(width)}", RA, RB)
            else:
                self.gen_expr(node)  # covers getkey, isflagsupported, user functions; value discarded
            return

        if isinstance(node, c_ast.If):
            bool_addr = self.gen_cond(node.cond)
            label_else = Label("else")
            label_end = Label("endif")
            self.emit_load(bool_addr, 1, RA)
            self.emit("ifnotreg", RA, label_else if node.iffalse else label_end)
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
            self.emit_load(bool_addr, 1, RA)
            self.emit("ifnotreg", RA, label_end)
            self.gen_stmt(node.stmt)
            self.emit("setpc", label_start)
            self.define_label(label_end)
            return

        if isinstance(node, c_ast.Return):
            if self.return_stack:
                ret_addr, end_label = self.return_stack[-1]
                if node.expr is not None:
                    rhs = self.gen_expr(node.expr)
                    self.emit_copy(rhs, ret_addr)
                self.emit("setpc", end_label)
            return  # main()'s return value is ignored; 'exit' is emitted at the end

        raise NotImplementedError(f"unsupported statement '{type(node).__name__}'")

    # -- entry point ---------------------------------------------------------
    def compile(self, source):
        source = strip_comments(source)
        source = apply_defines(source)
        clean_source, self.alloc_meta = self.rewrite_alloc_syntax(source)
        clean_source = self.rewrite_mem_syntax(clean_source)
        clean_source = "\n".join(
            line for line in clean_source.splitlines() if not line.strip().startswith("#")
        )
        builtin_typedefs = "\n".join(
            f"typedef unsigned char {name};" for name in TYPE_BYTES if name != "char"
        )
        clean_source = builtin_typedefs + "\n" + clean_source
        ast = pycparser.CParser().parse(clean_source)

        func_defs = [n for n in ast.ext if isinstance(n, c_ast.FuncDef)]
        global_decls = [n for n in ast.ext if isinstance(n, c_ast.Decl)]
        main_fn = next((f for f in func_defs if f.decl.name == "main"), None)
        other_fns = [f for f in func_defs if f.decl.name != "main"]
        if main_fn is None:
            raise NotImplementedError("no main() function found")

        self.function_return_size(main_fn)

        for fn in other_fns:
            self.function_asts[fn.decl.name] = fn

        self.gen_prologue()
        for decl in global_decls:
            self.gen_stmt(decl)
        self.gen_stmt(main_fn.body)
        self.emit("exit")

        # Now that we know the program's total memory usage, patch the
        # prologue's placeholder "required bytes" value. Graphics/keyboard no
        # longer share the addressable memory space (they have their own
        # dedicated VM-side buffers), so only the program's own variables count.
        required_bytes = self.next_addr
        pos = self.required_bytes_patch_pos
        self.buf[pos:pos + 4] = required_bytes.to_bytes(4, "big")

        return self.finalize()


def main():
    if len(sys.argv) < 2:
        print("Usage: python compiler.py input.uc [-o output.ulc]")
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