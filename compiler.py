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
    "or":        (0x23, [4, 1, 4, 1, 4, 1]),
    "and":       (0x24, [4, 1, 4, 1, 4, 1]),
    "nor":       (0x25, [4, 1, 4, 1, 4, 1]),
    "nand":      (0x26, [4, 1, 4, 1, 4, 1]),
    "xor":       (0x27, [4, 1, 4, 1, 4, 1]),
    "xnor":      (0x28, [4, 1, 4, 1, 4, 1]),
    "shl":       (0x29, [4, 1, 4, 1, 4, 1]),
    "shr":       (0x2A, [4, 1, 4, 1, 4, 1]),
    "if":        (0x2D, [4, 1, 4]),             # addr, size, pos
    "ifnot":     (0x2E, [4, 1, 4]),
    "sleep":     (0x31, [4]),                   # ms
    "getmemsize": (0x33, [4, 1]),               # addr, size
    "refreshscreen": (0x34, []),
    "getflag":   (0x35, [1, 4, 1]),             # flag_index, addr, size
    "romread":   (0x36, [4, 4, 1]),             # pos, addr, size
    "setpixel":  (0x37, [4, 1, 4, 1, 4, 1]),    # addrx, sizex, addry, sizey, addrc, sizec
    "exit":      (0xFF, []),
}

TYPE_BITS = {
    "bool": 1,
    "char": 8,
    "uint1": 1,
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
        self.alloc_meta = {}        # name -> {"kind": "scalar"|"array", "bits":..., "count":...}
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
        self.array_elem_bits = {}   # array variable name -> element width in bits

        self.const_arrays = {}       # name -> {base, elem_bits, count}
        self.rom_data = []           # {"name", "patch_pos", "data"}

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

    def emit_copy(self, src, dest):
        # Values are stored right-justified (MSB-first) within their own
        # bit field, so a narrower field's real payload sits at the END of
        # its allocated bits, not at its base address. When src/dest widths
        # differ we must align on the low-order bits and zero-extend the
        # rest, or a direct same-address read/write either grabs the wrong
        # (zero-padded) bits or overruns into whatever memory follows.
        src_size = self.addr_size(src)
        dest_size = self.addr_size(dest)
        copy_size = min(src_size, dest_size)
        src_off = src + (src_size - copy_size)
        dest_off = dest + (dest_size - copy_size)
        if dest_size > copy_size:
            self.emit("mem", dest, dest_size - copy_size, 0)  # zero-extend
        self.emit("add", 0, src_off, copy_size, dest_off)

    def widen_to_int(self, addr):
        # Returns an INT_SIZE-wide address holding the same value as addr,
        # regardless of addr's own width. Needed before feeding addr into
        # ops (mul/addv/etc.) that assume INT_SIZE-wide operands, since
        # otherwise they'd read INT_SIZE bits starting at addr's base and
        # overrun into adjacent memory when addr is narrower (e.g. a uint8
        # coordinate passed to setpixel/getpixel).
        if self.addr_size(addr) == INT_SIZE:
            return addr
        wide = self.alloc_temp(INT_SIZE)
        self.emit_copy(addr, wide)
        return wide

    def to_bool(self, addr):
        # Normalizes any value to a 1-bit 0/1 address (true iff addr != 0).
        dest = self.alloc_temp(1)
        self.emit("isequal", 0, addr, self.addr_size(addr), dest)
        self.emit("not", dest, 1, dest, 1)
        return dest

    @staticmethod
    def type_name(type_node):
        if not isinstance(type_node, c_ast.TypeDecl) or not isinstance(type_node.type, c_ast.IdentifierType):
            raise NotImplementedError("only fixed-width unsigned types are supported")
        names = type_node.type.names
        if len(names) != 1 or names[0] not in TYPE_BITS:
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
            return TYPE_BITS[cls.type_name(type_node)]
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
    def pack_values(values, elem_bits):
        packed = bytearray()
        current_byte = 0
        bits_in_current_byte = 0
        for value in values:
            if value < 0 or value >= (1 << elem_bits):
                raise ValueError(f"constant value {value} does not fit in {elem_bits} bits")
            for bit_idx in range(elem_bits - 1, -1, -1):
                current_byte = (current_byte << 1) | ((value >> bit_idx) & 1)
                bits_in_current_byte += 1
                if bits_in_current_byte == 8:
                    packed.append(current_byte)
                    current_byte = 0
                    bits_in_current_byte = 0
        if bits_in_current_byte:
            packed.append(current_byte << (8 - bits_in_current_byte))
        return bytes(packed)

    def add_const_array(self, name, elem_bits, values):
        base = self.alloc_temp(INT_SIZE)
        instr_start = len(self.buf)
        self.emit("mem", base, INT_SIZE, 0)
        self.const_arrays[name] = {"base": base, "elem_bits": elem_bits, "count": len(values)}
        self.array_elem_bits[name] = elem_bits
        self.rom_data.append({
            "name": name,
            "patch_pos": instr_start + 1 + 4 + 1,
            "data": self.pack_values(values, elem_bits),
        })
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
        bit_offset = len(self.buf) * 8
        for item in self.rom_data:
            self.buf[item["patch_pos"]:item["patch_pos"] + 4] = bit_offset.to_bytes(4, "big")
            bit_offset += len(item["data"]) * 8
        for item in self.rom_data:
            self.buf.extend(item["data"])
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
            if node.type == "string":
                name = f"__str{len(self.rom_data)}"
                return self.add_const_array(name, TYPE_BITS["char"], self.string_bytes(node) + [0])
            if node.type not in ("int", "char"):
                raise NotImplementedError(f"unsupported constant type '{node.type}'")
            val = self.const_value(node)
            dest = self.alloc_temp(INT_SIZE)
            self.emit("mem", dest, INT_SIZE, val)
            return dest

        if isinstance(node, c_ast.ID):
            if node.name in ("true", "false"):
                dest = self.alloc_temp(INT_SIZE)
                self.emit("mem", dest, INT_SIZE, 1 if node.name == "true" else 0)
                return dest
            if node.name in self.const_arrays:
                return self.const_arrays[node.name]["base"]
            return self.var_addr(node.name)

        if isinstance(node, c_ast.ArrayRef):
            if isinstance(node.name, c_ast.ID) and node.name.name in self.const_arrays:
                const_array = self.const_arrays[node.name.name]
                index = self.widen_to_int(self.gen_expr(node.subscript))
                offset = self.alloc_temp(INT_SIZE)
                self.emit("mul", const_array["elem_bits"], index, INT_SIZE, offset)
                pos_addr = self.alloc_temp(INT_SIZE)
                self.emit("addv", const_array["base"], INT_SIZE, offset, INT_SIZE, pos_addr, INT_SIZE)
                result = self.alloc_temp(const_array["elem_bits"])
                self.emit("romread", pos_addr, result, const_array["elem_bits"])
                return result
            base = self.gen_expr(node.name)
            index = self.widen_to_int(self.gen_expr(node.subscript))
            elem_bits = self.array_elem_bits.get(node.name.name, INT_SIZE) if isinstance(node.name, c_ast.ID) else INT_SIZE
            offset = self.alloc_temp(INT_SIZE)
            self.emit("mul", elem_bits, index, INT_SIZE, offset)
            addr_val = self.alloc_temp(INT_SIZE)
            self.emit("add", base, offset, INT_SIZE, addr_val)
            result = self.alloc_temp(elem_bits)
            dest_ptr_addr = self.alloc_temp(INT_SIZE)
            self.emit("mem", dest_ptr_addr, INT_SIZE, result)
            self.emit("setmem", addr_val, elem_bits, dest_ptr_addr, elem_bits)
            return result

        if isinstance(node, c_ast.UnaryOp):
            if node.op == "&":
                if isinstance(node.expr, c_ast.ID):
                    return self.var_addr(node.expr.name)
                if isinstance(node.expr, c_ast.ArrayRef):
                    base = self.gen_expr(node.expr.name)
                    idx = self.gen_expr(node.expr.subscript)
                    elem_bits = self.array_elem_bits.get(node.expr.name.name, INT_SIZE) if isinstance(node.expr.name, c_ast.ID) else INT_SIZE
                    offset = self.alloc_temp(INT_SIZE)
                    self.emit("mul", elem_bits, idx, INT_SIZE, offset)
                    out = self.alloc_temp(INT_SIZE)
                    self.emit("addv", base, INT_SIZE, offset, INT_SIZE, out, INT_SIZE)
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
                self.emit("add" if "+" in node.op else "sub", 1, addr, self.addr_size(addr), addr)
                return addr
            raise NotImplementedError(f"unsupported unary operator '{node.op}'")

        if isinstance(node, c_ast.BinaryOp):
            comparisons = ("==", "!=", "<", ">", "<=", ">=")
            if node.op in comparisons:
                # Comparisons now produce a usable 1-bit 0/1 value, not just
                # a condition for if/while — gen_cond already builds exactly
                # that, so reuse it here.
                return self.gen_cond(node)

            if node.op in ("&&", "||"):
                left_bool = self.to_bool(self.gen_expr(node.left))
                right_bool = self.to_bool(self.gen_expr(node.right))
                dest = self.alloc_temp(1)
                self.emit("and" if node.op == "&&" else "or", left_bool, 1, right_bool, 1, dest, 1)
                return dest

            bitwise_ops = {"&": "and", "|": "or", "^": "xor", "<<": "shl", ">>": "shr"}
            if node.op in bitwise_ops:
                left = self.gen_expr(node.left)
                left_size = self.addr_size(left)
                right = self.gen_expr(node.right)
                right_size = self.addr_size(right)
                # shl/shr keep the left operand's width; the others take the wider of the two.
                result_size = left_size if node.op in ("<<", ">>") else max(left_size, right_size)
                dest = self.alloc_temp(result_size)
                self.emit(bitwise_ops[node.op], left, left_size, right, right_size, dest, result_size)
                return dest

            immediate_ops = {"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "mod"}
            var_ops = {"+": "addv", "-": "subv", "*": "mulv", "/": "divv", "%": "modv"}
            if node.op not in immediate_ops:
                raise NotImplementedError(f"unsupported operator '{node.op}' in expression")
            left = self.gen_expr(node.left)
            left_size = self.addr_size(left)
            if isinstance(node.right, c_ast.Constant):
                result_size = left_size
                dest = self.alloc_temp(result_size)
                self.emit(immediate_ops[node.op], self.const_value(node.right), left, left_size, dest)
            else:
                right = self.gen_expr(node.right)
                result_size = max(left_size, self.addr_size(right))
                dest = self.alloc_temp(result_size)
                self.emit(var_ops[node.op], left, left_size, right, self.addr_size(right), dest, result_size)
            return dest

        if isinstance(node, c_ast.FuncCall):
            name = node.name.name
            if name == "romread":
                args = node.args.exprs if node.args else []
                if len(args) != 2 or not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("romread(pos, size) takes a ROM bit-position expression and a constant size")
                pos = self.widen_to_int(self.gen_expr(args[0]))
                size = int(args[1].value, 0)
                dest = self.alloc_temp(size)
                self.emit("romread", pos, dest, size)
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
                chars = max(1, size // 8)
                self.emit("input", chars, target, size)
                return target
            if name == "__mem_load":
                args = node.args.exprs if node.args else []
                if len(args) != 2:
                    raise NotImplementedError("__mem_load(addr, size) takes exactly two arguments")
                if not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("__mem_load() size must be a compile-time constant")
                addr = self.gen_expr(args[0])
                size = int(args[1].value, 0)
                dest = self.alloc_temp(size)
                addr_ptr_addr = self.alloc_temp(INT_SIZE)
                dest_ptr_addr = self.alloc_temp(INT_SIZE)
                self.emit("mem", addr_ptr_addr, INT_SIZE, addr)
                self.emit("mem", dest_ptr_addr, INT_SIZE, dest)
                self.emit("setmem", addr_ptr_addr, size, dest_ptr_addr, size)
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

    # -- conditions: returns a 1-bit boolean address ---------------------------
    def gen_cond(self, node):
        comparisons = ("==", "!=", "<", ">", "<=", ">=")
        if isinstance(node, c_ast.BinaryOp) and node.op in comparisons:
            op = node.op
            bool_addr = self.alloc_temp(1)
            if isinstance(node.right, c_ast.Constant):
                left = self.gen_expr(node.left)
                rhs_val = self.const_value(node.right)
                result = self.alloc_temp(2)
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
                result = self.alloc_temp(2)
                self.emit("comparev", left, self.addr_size(left), right, self.addr_size(right), result, 2)
                code_for = {"==": 0, ">": 1, "<": 2}
                if op in code_for:
                    self.emit("isequal", code_for[op], result, 2, bool_addr)
                else:
                    inverse = {"!=": 0, "<=": 1, ">=": 2}[op]
                    self.emit("isequal", inverse, result, 2, bool_addr)
                    self.emit("not", bool_addr, 1, bool_addr, 1)
            return bool_addr

        if isinstance(node, c_ast.Constant):
            bool_addr = self.alloc_temp(1)
            self.emit("mem", bool_addr, 1, 1 if self.const_value(node) != 0 else 0)
            return bool_addr

        val = self.gen_expr(node)
        bool_addr = self.alloc_temp(1)
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
        # setpixel(x, y, val) now compiles to a single native VM opcode
        # (0x37) -- the mode/width/framebuffer-offset math that used to be
        # ~20 interpreted instructions per call now happens once, in the
        # VM's own dispatch, per pixel.
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 3:
            raise NotImplementedError("setpixel(x, y, val) takes exactly 3 arguments")
        x_node, y_node, val_node = args

        x_addr = self.gen_expr(x_node)
        y_addr = self.gen_expr(y_node)

        if isinstance(val_node, c_ast.Constant):
            val_addr = self.alloc_temp(1)
            self.emit("mem", val_addr, 1, 1 if self.const_value(val_node) != 0 else 0)
        else:
            val_addr = self.gen_cond(val_node)

        self.emit("setpixel", x_addr, self.addr_size(x_addr), y_addr, self.addr_size(y_addr),
                   val_addr, self.addr_size(val_addr))

    def gen_getpixel(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("getpixel(x, y) takes exactly 2 arguments")

        x_addr = self.gen_expr(args[0])
        y_addr = self.gen_expr(args[1])

        if self.setpixel_scratch is None:
            self.setpixel_scratch = {
                "mode": self.alloc_temp(4),
                "width": self.alloc_temp(),
                "fb_bits": self.alloc_temp(),
                "kbd_bits": self.alloc_temp(),
                "reserved": self.alloc_temp(),
                "y_mul": self.alloc_temp(),
                "xy": self.alloc_temp(),
                "base": self.alloc_temp(),
                "target": self.alloc_temp(),
                "tmp": self.alloc_temp(1),
                "src_ptr": self.alloc_temp(),
                "dest": self.alloc_temp(1),
                "dest_ptr": self.alloc_temp(),
                "bool_addr": self.alloc_temp(),
                "x32": self.alloc_temp(),
                "y32": self.alloc_temp(),
            }
        s = self.setpixel_scratch

        self.emit_copy(x_addr, s["x32"])
        self.emit_copy(y_addr, s["y32"])
        x_addr, y_addr = s["x32"], s["y32"]

        label_64 = Label("getpixel_64")
        label_128 = Label("getpixel_128")
        label_done = Label("getpixel_done")

        self.emit("getflag", 0, s["mode"], 4)
        self.emit("mem", s["width"], INT_SIZE, 64)
        self.emit("mem", s["fb_bits"], INT_SIZE, 64 * 64)
        self.emit("mem", s["kbd_bits"], INT_SIZE, 8)

        self.emit("isequal", 4, s["mode"], 4, s["tmp"])
        self.emit("ifnot", s["tmp"], 1, label_128)
        self.emit("mem", s["width"], INT_SIZE, 128)
        self.emit("mem", s["fb_bits"], INT_SIZE, 128 * 64)
        self.emit("setpc", label_done)

        self.define_label(label_128)
        self.emit("isequal", 1, s["mode"], 4, s["tmp"])
        self.emit("ifnot", s["tmp"], 1, label_64)
        self.emit("mem", s["width"], INT_SIZE, 64)
        self.emit("mem", s["fb_bits"], INT_SIZE, 64 * 64)
        self.emit("setpc", label_done)

        self.define_label(label_64)
        self.emit("mem", s["width"], INT_SIZE, 64)
        self.emit("mem", s["fb_bits"], INT_SIZE, 64 * 64)
        self.define_label(label_done)

        self.emit("addv", s["fb_bits"], INT_SIZE, s["kbd_bits"], INT_SIZE, s["reserved"], INT_SIZE)
        self.emit("mul", s["width"], y_addr, INT_SIZE, s["y_mul"])
        self.emit("addv", x_addr, INT_SIZE, s["y_mul"], INT_SIZE, s["xy"], INT_SIZE)
        self.emit("sub", 1, self.runtime_memsize_addr, INT_SIZE, s["base"])
        self.emit("subv", s["base"], INT_SIZE, s["xy"], INT_SIZE, s["target"], INT_SIZE)
        self.emit("mem", s["src_ptr"], INT_SIZE, s["target"])
        self.emit("mem", s["dest_ptr"], INT_SIZE, s["dest"])
        self.emit("setmem", s["src_ptr"], 1, s["dest_ptr"], 1)
        return s["dest"]

    def gen_getkey(self):
        # Reads the keyboard byte based on the currently active graphics and
        # keyboard flags, instead of assuming a single static offset.
        self.uses_graphics = True
        if self.getkey_scratch is None:
            self.getkey_scratch = {
                "gfx_mode": self.alloc_temp(4),
                "kbd_mode": self.alloc_temp(4),
                "fb_bits": self.alloc_temp(),
                "kbd_bits": self.alloc_temp(),
                "reserved": self.alloc_temp(),
                "ptr": self.alloc_temp(),
                "dest": self.alloc_temp(8),
                "dest_ptr": self.alloc_temp(),
                "tmp": self.alloc_temp(1),
            }
        s = self.getkey_scratch

        label_64 = Label("getkey_64")
        label_128 = Label("getkey_128")
        label_done = Label("getkey_done")

        self.emit("getflag", 0, s["gfx_mode"], 4)
        self.emit("getflag", 1, s["kbd_mode"], 4)
        self.emit("mem", s["fb_bits"], INT_SIZE, 0)
        self.emit("mem", s["kbd_bits"], INT_SIZE, 8)
        self.emit("mem", s["reserved"], INT_SIZE, 0)

        self.emit("isequal", 4, s["gfx_mode"], 4, s["tmp"])
        self.emit("ifnot", s["tmp"], 1, label_128)
        self.emit("mem", s["fb_bits"], INT_SIZE, 128 * 64)
        self.emit("setpc", label_done)

        self.define_label(label_128)
        self.emit("isequal", 1, s["gfx_mode"], 4, s["tmp"])
        self.emit("ifnot", s["tmp"], 1, label_64)
        self.emit("mem", s["fb_bits"], INT_SIZE, 64 * 64)
        self.emit("setpc", label_done)

        self.define_label(label_64)
        self.emit("mem", s["fb_bits"], INT_SIZE, 64 * 64)
        self.define_label(label_done)

        # Keyboard lives in the reserved tail immediately before the framebuffer.
        self.emit("addv", s["fb_bits"], INT_SIZE, s["kbd_bits"], INT_SIZE, s["reserved"], INT_SIZE)
        self.emit("subv", self.runtime_memsize_addr, INT_SIZE, s["reserved"], INT_SIZE, s["ptr"], INT_SIZE)
        self.emit("mem", s["dest_ptr"], INT_SIZE, s["dest"])
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
                elem_bits = self.type_size(node.type.type)
                if isinstance(node.init, c_ast.Constant) and node.init.type == "string":
                    if elem_bits != TYPE_BITS["char"]:
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

                self.add_const_array(node.name, elem_bits, values)
                return

            meta = self.alloc_meta.get(node.name)
            if meta is not None:
                if meta["kind"] == "scalar":
                    size = meta["bits"]
                else:
                    size = meta["bits"] * meta["count"]
                    self.array_elem_bits[node.name] = meta["bits"]
            else:
                size = self.type_size(node.type)
                if isinstance(node.type, c_ast.ArrayDecl):
                    self.array_elem_bits[node.name] = self.type_size(node.type.type)
            addr = self.alloc_var(node.name, size)
            if node.init is not None:
                if isinstance(node.init, c_ast.InitList):
                    if not isinstance(node.type, c_ast.ArrayDecl):
                        raise NotImplementedError("initializer lists are only supported for 1D arrays")
                    elem_bits = self.type_size(node.type.type)
                    for i, item in enumerate(node.init.exprs):
                        if not isinstance(item, c_ast.Constant):
                            raise NotImplementedError("array initializer values must be compile-time constants")
                        elem_addr = addr + i * elem_bits
                        self.addr_sizes[elem_addr] = elem_bits
                        self.emit("mem", elem_addr, elem_bits, self.const_value(item))
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
                self.emit("ifnot", bool_addr, 1, label_end)
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
                chars = max(1, size // 8)
                self.emit("input", chars, target, size)
            elif name == "__mem_store":
                args = node.args.exprs if node.args else []
                if len(args) != 3:
                    raise NotImplementedError("__mem_store(addr, size, value) takes exactly three arguments")
                if not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("__mem_store() size must be a compile-time constant")
                addr = self.gen_expr(args[0])
                width = int(args[1].value, 0)
                value = self.gen_expr(args[2])

                value_temp = self.alloc_temp(width)
                addr_ptr_addr = self.alloc_temp(INT_SIZE)
                value_ptr_addr = self.alloc_temp(INT_SIZE)

                self.emit("add", 0, value, width, value_temp)
                self.emit("mem", addr_ptr_addr, INT_SIZE, addr)
                self.emit("mem", value_ptr_addr, INT_SIZE, value_temp)
                self.emit("setmem", value_ptr_addr, width, addr_ptr_addr, width)
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
            f"typedef unsigned char {name};" for name in TYPE_BITS if name != "char"
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
