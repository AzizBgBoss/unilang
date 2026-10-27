'''
uniClang -> unilang bytecode (.ulc) compiler by AzizBgBoss
https://github.com/AzizBgBoss/unilang

Source files are plain C (.c); compiled bytecode uses .ulc.
Requires pycparser:  pip install pycparser

See README.md for full documentation of the supported C subset and the .ulc bytecode format.

C-closeness summary (details in README.md):
  - a real preprocessor (preprocessor.py): #include, #define/#undef, #if/#ifdef/
    #elif/#else/#endif, #pragma once, -I and -D command line options
  - standard type names (int, unsigned, short, long, uint8_t..uint32_t, size_t,
    _Bool, void), user typedefs, enums, const scalars
  - pointers (T *p, &x, *p, p[i], pointer arithmetic, array-to-pointer decay,
    pointer parameters), casts, sizeof, compile-time constant folding
  - operators: unary - + ! ~, ?:, && and || with short-circuit evaluation,
    compound assignment (+= -= ...), pre/post ++ and -- on any lvalue, comma
  - statements: if/else, while, do/while, for, switch/case/default, break,
    continue, return (also an early return from main)
  - printf(%d %i %u %c %s %%), puts, putchar

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

import os
import re
import sys
import pycparser
from pycparser import c_ast
from preprocessor import Preprocessor, PreprocessorError




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
    "randreg":         (0x1D, [1, 1, 1]),             # r1(min), r2(max), dst
    "srandreg":        (0x1E, [1]),                   # reg
    "drawstring":      (0x1F, [1, 1, 1, 1, 1, 1, 1, 1]),  # regx, regy, regcolor, regaddr, reglen, src(0=ram,1=rom), regreturnx, regreturny
    "numtostr":        (0x20, [1, 4, 1]),             # reg, addr, reglen

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
RC = 13

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

# Standard C type names accepted as aliases: name -> (bytes, is_signed).
# The VM is unsigned-only, so signed types are stored like their unsigned
# twins (see README: +, -, * wrap correctly; <, /, %, >> and printing don't).
TYPE_ALIASES = {
    "uint8_t": (1, False), "uint16_t": (2, False), "uint32_t": (4, False),
    "int8_t": (1, True), "int16_t": (2, True), "int32_t": (4, True),
    "size_t": (4, False), "uintptr_t": (4, False),
}

M32 = 0xFFFFFFFF


class _NotConst(Exception):
    """Raised internally when an expression isn't a compile-time constant."""


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
        self.const_array_types = {}  # name -> declared ArrayDecl type, for nested ROM indexing
        self.rom_data = []           # {"patch_pos", "data"}

        self.typedefs = {}           # user typedef name -> byte size
        self.structs = {}            # struct tag or typedef name -> {"size": n, "fields": {name: {"offset": o, "size": s}}}
        self.struct_defs = {}        # struct tag or typedef name -> original Struct AST node
        self.constants = {}          # enum constants and const scalars -> int value
        self.ptr_elem = {}           # address of a pointer variable -> pointee byte size
        self.array_by_addr = {}      # base address of a RAM array -> element byte size
        self.var_types = {}          # variable name -> declared type AST node
        self.break_stack = []        # jump targets for `break`
        self.continue_stack = []     # jump targets for `continue`
        self.used_signed = False     # set when a signed integer type is used
        self.cur_coord = None        # source position of the statement being compiled
        self.at_global_scope = False
        self.max_addr = 0            # peak RAM use: temporaries are reused, so next_addr alone isn't enough

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

    @staticmethod
    def rewrite_cpp_tag_typedefs(source):
        def add_enum(match):
            keyword, tag = match.group(1), match.group(2)
            return f"{keyword}\ntypedef enum {tag} {tag};"

        def add_struct(match):
            keyword, tag = match.group(1), match.group(2)
            return f"{keyword}\ntypedef struct {tag} {tag};"

        source = re.sub(r"(?s)(\benum\s+([A-Za-z_]\w*)\s*\{.*?\}\s*;)", add_enum, source)
        source = re.sub(r"(?s)(\bstruct\s+([A-Za-z_]\w*)\s*\{.*?\}\s*;)", add_struct, source)
        return source

    # -- memory allocation ---------------------------------------------------
    def alloc_var(self, name, size=INT_SIZE):
        addr = self.next_addr
        self.constants.pop(name, None)  # a variable shadows a same-named constant
        self.next_addr += size
        if self.next_addr > self.max_addr:
            self.max_addr = self.next_addr
        # this address may have held something else before it was freed
        self.ptr_elem.pop(addr, None)
        self.array_by_addr.pop(addr, None)
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
        value = self.try_const(node)
        if value is not None:
            return ("const", value)
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
        if isinstance(node, c_ast.UnaryOp) and node.op == "!":
            return self.gen_expr(node)
        return self.to_bool(self.gen_expr(node))

    @staticmethod
    def is_void_type(type_node):
        return (isinstance(type_node, c_ast.TypeDecl)
                and isinstance(type_node.type, c_ast.IdentifierType)
                and list(type_node.type.names) == ["void"])

    def struct_field_size(self, decl):
        if decl.bitsize is not None:
            width = self.const_value(decl.bitsize, "bitfield width")
            return max(1, (width + 7) // 8)
        return self.type_size(decl.type)

    def struct_size(self, struct_node):
        if struct_node is None or struct_node.decls is None:
            raise NotImplementedError("incomplete struct type is not supported")
        name = struct_node.name
        if name in self.structs and self.structs[name]["fields"]:
            return self.structs[name]["size"]
        offset = 0
        fields = {}
        for decl in struct_node.decls:
            if decl.name is None:
                continue
            size = self.struct_field_size(decl)
            fields[decl.name] = {"offset": offset, "size": size}
            offset += size
        if name is not None:
            self.structs[name] = {"size": offset, "fields": fields}
        return offset

    def struct_field_info(self, struct_type, field_name):
        if not isinstance(struct_type, c_ast.Struct):
            raise NotImplementedError("expected a struct type")
        info = self.structs.get(struct_type.name)
        if info is not None and field_name in info["fields"]:
            return info["fields"][field_name]
        for decl in struct_type.decls or []:
            if decl.name == field_name:
                offset = 0
                for prev in struct_type.decls:
                    if prev.name is None:
                        continue
                    if prev.name == field_name:
                        break
                    offset += self.struct_field_size(prev)
                size = self.struct_field_size(decl)
                return {"offset": offset, "size": size}
        raise NotImplementedError(f"struct field '{field_name}' not found")

    def scalar_size(self, type_node):
        """Byte width of a scalar TypeDecl: uniClang names, standard C names,
        fixed-width stdint names, user typedefs and enums."""
        t = type_node.type
        if isinstance(t, c_ast.Enum):
            return INT_SIZE
        if isinstance(t, c_ast.Struct):
            return self.struct_size(t)
        if isinstance(t, c_ast.Union):
            raise NotImplementedError("unions are not supported")
        if not isinstance(t, c_ast.IdentifierType):
            raise NotImplementedError(f"unsupported type node '{type(t).__name__}'")
        words = list(t.names)
        if len(words) == 1:
            w = words[0]
            if w in TYPE_BYTES:
                return TYPE_BYTES[w]
            if w in TYPE_ALIASES:
                size, signed = TYPE_ALIASES[w]
                self.used_signed = self.used_signed or signed
                return size
            if w in self.typedefs:
                return self.typedefs[w]
        if "float" in words or "double" in words:
            raise NotImplementedError("floating point types are not supported")
        if "void" in words:
            raise NotImplementedError("'void' is only valid as a function return type, a (void) parameter list, or a pointer target")
        if words.count("long") > 1:
            raise NotImplementedError("64-bit integers (long long) are not supported")
        if "char" in words:
            if "signed" in words:
                self.used_signed = True
            return 1
        if "_Bool" in words:
            return 1
        if not set(words) <= {"int", "short", "long", "signed", "unsigned"}:
            raise NotImplementedError(f"unknown or unsupported type '{' '.join(words)}'")
        if "unsigned" not in words:
            self.used_signed = True
        return 2 if "short" in words else 4

    def type_size(self, type_node):
        if isinstance(type_node, c_ast.ArrayDecl):
            elem_size = self.type_size(type_node.type)
            if type_node.dim is None:
                return elem_size  # size inferred from the initializer by the caller
            return elem_size * self.const_value(type_node.dim, "array size")
        if isinstance(type_node, c_ast.PtrDecl):
            return INT_SIZE
        if isinstance(type_node, c_ast.TypeDecl):
            if isinstance(type_node.type, c_ast.Struct):
                return self.struct_size(type_node.type)
            return self.scalar_size(type_node)
        if isinstance(type_node, c_ast.Struct):
            return self.struct_size(type_node)
        raise NotImplementedError(f"unsupported type node '{type(type_node).__name__}'")

    def ptr_pointee_size(self, ptr_node):
        target = ptr_node.type
        if isinstance(target, c_ast.PtrDecl):
            return INT_SIZE  # pointer to pointer: the pointee is itself a 4-byte address
        if isinstance(target, c_ast.TypeDecl):
            if self.is_void_type(target):
                return 1  # void * is byte-addressed
            if isinstance(target.type, c_ast.Struct):
                return self.struct_size(target.type)
            return self.scalar_size(target)
        raise NotImplementedError("unsupported pointer target type")

    def function_return_size(self, fn):
        ret = fn.decl.type.type
        if self.is_void_type(ret):
            return INT_SIZE  # unused slot; keeps call codegen uniform
        return self.type_size(ret)

    @staticmethod
    def fn_params(fn):
        args = fn.decl.type.args
        params = args.params if args else []
        if len(params) == 1 and isinstance(params[0], c_ast.Typename):
            return []  # f(void)
        return params

    def param_size(self, param):
        if isinstance(param.type, (c_ast.ArrayDecl, c_ast.PtrDecl)):
            return INT_SIZE
        return self.type_size(param.type)

    def param_pointee(self, param):
        """Pointee/element byte size if the parameter is a pointer or array, else None."""
        if isinstance(param.type, c_ast.ArrayDecl):
            return self.type_size(param.type.type)
        if isinstance(param.type, c_ast.PtrDecl):
            return self.ptr_pointee_size(param.type)
        return None

    @staticmethod
    def is_const_decl(node):
        return "const" in getattr(node, "quals", []) or "const" in getattr(node.type, "quals", [])

    # -- compile-time constants ---------------------------------------------------
    @staticmethod
    def parse_int_literal(text):
        text = text.rstrip("uUlL")
        if len(text) > 1 and text[0] == "0" and text[1] not in "xXbB":
            return int(text, 8)
        return int(text, 0)

    def const_value(self, node, what="expression"):
        value = self.try_const(node)
        if value is None:
            raise NotImplementedError(f"{what} must be a compile-time constant")
        return value

    def try_const(self, node):
        """The value of a compile-time constant expression (folded to uint32
        semantics), or None if the expression isn't constant."""
        try:
            return self.eval_const(node)
        except _NotConst:
            return None

    def eval_const(self, node):
        if isinstance(node, c_ast.Constant):
            if node.type == "char":
                return ord(bytes(node.value[1:-1], "utf-8").decode("unicode_escape"))
            if node.type.endswith("int"):
                return self.parse_int_literal(node.value) & M32
            raise _NotConst()
        if isinstance(node, c_ast.ID):
            if node.name in self.vars:
                raise _NotConst()
            if node.name in ("true", "false"):
                return 1 if node.name == "true" else 0
            if node.name in ("NULL", "nullptr"):
                return 0
            if node.name in self.constants:
                return self.constants[node.name]
            raise _NotConst()
        if isinstance(node, c_ast.UnaryOp):
            if node.op == "sizeof":
                return self.sizeof_value(node.expr)
            if node.op in ("-", "+", "~", "!"):
                v = self.eval_const(node.expr)
                if node.op == "-":
                    return (-v) & M32
                if node.op == "~":
                    return (~v) & M32
                if node.op == "!":
                    return int(v == 0)
                return v
            raise _NotConst()
        if isinstance(node, c_ast.BinaryOp):
            a = self.eval_const(node.left)
            b = self.eval_const(node.right)
            op = node.op
            if op == "+": r = a + b
            elif op == "-": r = a - b
            elif op == "*": r = a * b
            elif op == "/" or op == "%":
                if b == 0:
                    raise _NotConst()
                r = a // b if op == "/" else a % b
            elif op == "&": r = a & b
            elif op == "|": r = a | b
            elif op == "^": r = a ^ b
            elif op == "<<": r = (a << b) if b < 32 else 0
            elif op == ">>": r = (a >> b) if b < 32 else 0
            elif op == "==": r = int(a == b)
            elif op == "!=": r = int(a != b)
            elif op == "<": r = int(a < b)
            elif op == ">": r = int(a > b)
            elif op == "<=": r = int(a <= b)
            elif op == ">=": r = int(a >= b)
            elif op == "&&": r = int(bool(a) and bool(b))
            elif op == "||": r = int(bool(a) or bool(b))
            else:
                raise _NotConst()
            return r & M32
        if isinstance(node, c_ast.TernaryOp):
            return self.eval_const(node.iftrue if self.eval_const(node.cond) else node.iffalse)
        if isinstance(node, c_ast.Cast):
            target = node.to_type.type
            if self.is_void_type(target):
                raise _NotConst()
            v = self.eval_const(node.expr)
            size = INT_SIZE if isinstance(target, c_ast.PtrDecl) else self.scalar_size(target)
            return v & ((1 << (size * 8)) - 1)
        raise _NotConst()

    def resolve_struct_type(self, type_node):
        if isinstance(type_node, c_ast.PtrDecl):
            return self.resolve_struct_type(type_node.type)
        if isinstance(type_node, c_ast.ArrayDecl):
            return self.resolve_struct_type(type_node.type)
        if isinstance(type_node, c_ast.TypeDecl):
            if isinstance(type_node.type, c_ast.Struct):
                return type_node.type
            if isinstance(type_node.type, c_ast.IdentifierType):
                for name in type_node.type.names:
                    if name in self.struct_defs:
                        return self.struct_defs[name]
            return None
        return None

    def resolve_type_for_expr(self, expr):
        if isinstance(expr, c_ast.ID):
            typ = self.var_types.get(expr.name, self.const_array_types.get(expr.name))
        else:
            typ = None
        if isinstance(expr, c_ast.ArrayRef):
            base_type = self.resolve_type_for_expr(expr.name)
            if isinstance(base_type, c_ast.ArrayDecl):
                return base_type.type
            if isinstance(base_type, c_ast.PtrDecl):
                return base_type.type
            if isinstance(base_type, c_ast.TypeDecl) and isinstance(base_type.type, c_ast.ArrayDecl):
                return base_type.type.type
            return None
        if isinstance(expr, c_ast.UnaryOp) and expr.op == "*":
            base_type = self.resolve_type_for_expr(expr.expr)
            if isinstance(base_type, c_ast.PtrDecl):
                return base_type.type
            if isinstance(base_type, c_ast.TypeDecl) and isinstance(base_type.type, c_ast.PtrDecl):
                return base_type.type.type
            return None
        if isinstance(expr, c_ast.StructRef):
            base = getattr(expr, "expr", getattr(expr, "name", None))
            base_type = self.resolve_type_for_expr(base)
            field_name = expr.field.name
            target_type = base_type
            if isinstance(target_type, c_ast.PtrDecl):
                target_type = target_type.type
            if isinstance(target_type, c_ast.TypeDecl):
                if isinstance(target_type.type, c_ast.IdentifierType):
                    for name in target_type.type.names:
                        if name in self.struct_defs:
                            target_type = c_ast.TypeDecl(declname=target_type.declname, quals=target_type.quals, align=target_type.align, type=self.struct_defs[name])
                            break
                if isinstance(target_type.type, c_ast.Struct):
                    struct = target_type.type
                    for decl in struct.decls or []:
                        if decl.name == field_name:
                            return decl.type
            if isinstance(base_type, c_ast.ArrayDecl):
                elem = base_type.type
                if isinstance(elem, c_ast.TypeDecl) and isinstance(elem.type, c_ast.Struct):
                    struct = elem.type
                    for decl in struct.decls or []:
                        if decl.name == field_name:
                            return decl.type
            return None
        if isinstance(expr, c_ast.Cast):
            return expr.to_type
        if isinstance(typ, c_ast.TypeDecl) and isinstance(typ.type, c_ast.IdentifierType):
            for name in typ.type.names:
                if name in self.struct_defs:
                    return c_ast.TypeDecl(declname=typ.declname, quals=typ.quals, align=typ.align, type=self.struct_defs[name])
        return typ

    def expr_type(self, expr):
        if isinstance(expr, c_ast.Constant):
            if expr.type == "char":
                return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                     type=c_ast.IdentifierType(names=["char"]))
            words = expr.type.replace("const", "").strip().split()
            return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                  type=c_ast.IdentifierType(names=words or ["int"]))
        if isinstance(expr, c_ast.ID):
            return self.var_types.get(expr.name)
        if isinstance(expr, c_ast.Cast):
            return expr.to_type.type
        if isinstance(expr, c_ast.UnaryOp):
            if expr.op == "sizeof":
                return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                     type=c_ast.IdentifierType(names=["size_t"]))
            if expr.op == "*":
                return self.resolve_type_for_expr(expr)
            return self.expr_type(expr.expr)
        if isinstance(expr, (c_ast.ArrayRef, c_ast.StructRef)):
            return self.resolve_type_for_expr(expr)
        if isinstance(expr, c_ast.BinaryOp):
            if expr.op in ("==", "!=", "<", ">", "<=", ">=", "&&", "||"):
                return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                     type=c_ast.IdentifierType(names=["bool"]))
            return self.expr_type(expr.left) or self.expr_type(expr.right)
        if isinstance(expr, c_ast.TernaryOp):
            return self.expr_type(expr.iftrue) or self.expr_type(expr.iffalse)
        return None

    def scalar_type_signed(self, type_node):
        if isinstance(type_node, c_ast.PtrDecl):
            return False
        if isinstance(type_node, c_ast.TypeDecl):
            type_node = type_node.type
        if isinstance(type_node, c_ast.Enum):
            return True
        if not isinstance(type_node, c_ast.IdentifierType):
            return False
        words = list(type_node.names)
        if len(words) == 1 and words[0] in TYPE_ALIASES:
            return TYPE_ALIASES[words[0]][1]
        return "unsigned" not in words and (
            any(word in ("signed", "int", "short", "long") for word in words)
        )

    def scalar_type_size(self, type_node):
        try:
            return self.type_size(type_node)
        except NotImplementedError:
            return INT_SIZE

    def comparison_is_signed(self, left, right, left_size, right_size):
        left_type = self.expr_type(left)
        right_type = self.expr_type(right)
        if isinstance(left_type, c_ast.PtrDecl) or isinstance(right_type, c_ast.PtrDecl):
            return False
        left_signed = self.scalar_type_signed(left_type)
        right_signed = self.scalar_type_signed(right_type)
        left_promoted_size = max(INT_SIZE, left_size)
        right_promoted_size = max(INT_SIZE, right_size)
        left_signed = left_signed or left_size < INT_SIZE
        right_signed = right_signed or right_size < INT_SIZE
        if left_signed and right_signed:
            return True
        if left_signed and not right_signed:
            return left_promoted_size > right_promoted_size
        if right_signed and not left_signed:
            return right_promoted_size > left_promoted_size
        return False

    def load_comparison_operand(self, node, resolved, reg):
        size = self.load_resolved(resolved, reg)
        type_node = self.expr_type(node)
        if self.scalar_type_signed(type_node) and size < INT_SIZE:
            sign_bit = 1 << (size * 8 - 1)
            self.emit_set_const(RC, sign_bit)
            self.emit("xorreg", reg, RC, reg)
            self.emit("subreg", reg, RC, reg)

    def sizeof_value(self, expr):
        if isinstance(expr, c_ast.Typename):
            return self.type_size(expr.type)
        if isinstance(expr, c_ast.ID):
            name = expr.name
            if name in self.vars:
                return self.var_sizes[name]
            if name in self.const_arrays:
                c = self.const_arrays[name]
                return c["elem_bytes"] * c["count"]
            raise _NotConst()
        if isinstance(expr, c_ast.ArrayRef) or (isinstance(expr, c_ast.UnaryOp) and expr.op == "*"):
            return self.lvalue_elem_size(expr)
        if isinstance(expr, c_ast.StructRef):
            base = getattr(expr, "expr", getattr(expr, "name", None))
            base_type = self.resolve_type_for_expr(base)
            if isinstance(base_type, c_ast.PtrDecl):
                target = base_type.type
                if isinstance(target, c_ast.TypeDecl) and isinstance(target.type, c_ast.IdentifierType):
                    for name in target.type.names:
                        if name in self.struct_defs:
                            target = c_ast.TypeDecl(declname=target.declname, quals=target.quals, align=target.align, type=self.struct_defs[name])
                            break
                if isinstance(target, c_ast.TypeDecl) and isinstance(target.type, c_ast.Struct):
                    return self.struct_field_info(target.type, expr.field.name)["size"]
            if isinstance(base_type, c_ast.TypeDecl) and isinstance(base_type.type, c_ast.IdentifierType):
                for name in base_type.type.names:
                    if name in self.struct_defs:
                        base_type = c_ast.TypeDecl(declname=base_type.declname, quals=base_type.quals, align=base_type.align, type=self.struct_defs[name])
                        break
            return self.struct_field_info(base_type.type, expr.field.name)["size"]
        raise _NotConst()

    # -- pointer typing (compile-time only; the VM has no types) -------------------
    def expr_ptr_elem(self, node):
        """If `node` is a pointer or array expression, the byte size of what it
        points at; otherwise None."""
        if isinstance(node, c_ast.ID):
            if node.name in self.vars:
                addr = self.vars[node.name]
                if addr in self.ptr_elem:
                    return self.ptr_elem[addr]
                if addr in self.array_by_addr:
                    return self.array_by_addr[addr]
                return None
            if node.name in self.const_arrays:
                array_type = self.const_array_types.get(node.name)
                if array_type is not None and isinstance(array_type, c_ast.ArrayDecl):
                    return self.type_size(array_type.type)
                return self.const_arrays[node.name]["elem_bytes"]
            return None
        if isinstance(node, c_ast.StructRef):
            base = getattr(node, "expr", getattr(node, "name", None))
            base_type = self.resolve_type_for_expr(base)
            if isinstance(base_type, c_ast.TypeDecl) and isinstance(base_type.type, c_ast.Struct):
                field_decl = None
                for decl in base_type.type.decls or []:
                    if decl.name == node.field.name:
                        field_decl = decl
                        break
                if field_decl is None:
                    return None
                if isinstance(field_decl.type, c_ast.ArrayDecl):
                    return self.type_size(field_decl.type.type)
                if isinstance(field_decl.type, c_ast.PtrDecl):
                    return self.ptr_pointee_size(field_decl.type)
                return None
            return None
        if isinstance(node, c_ast.ArrayRef):
            result_type = self.resolve_type_for_expr(node)
            if isinstance(result_type, c_ast.ArrayDecl):
                return self.type_size(result_type.type)
            if isinstance(result_type, c_ast.PtrDecl):
                return self.ptr_pointee_size(result_type)
            return None
        if isinstance(node, c_ast.Constant):
            return 1 if node.type == "string" else None
        if isinstance(node, c_ast.UnaryOp):
            if node.op == "&":
                return self.lvalue_elem_size(node.expr)
            if node.op in ("p++", "++", "p--", "--"):
                return self.expr_ptr_elem(node.expr)
            return None
        if isinstance(node, c_ast.BinaryOp) and node.op in ("+", "-"):
            left = self.expr_ptr_elem(node.left)
            if left is not None:
                return None if (node.op == "-" and self.expr_ptr_elem(node.right) is not None) else left
            return self.expr_ptr_elem(node.right) if node.op == "+" else None
        if isinstance(node, c_ast.Cast):
            if isinstance(node.to_type.type, c_ast.PtrDecl):
                return self.ptr_pointee_size(node.to_type.type)
            return None
        if isinstance(node, c_ast.TernaryOp):
            found = self.expr_ptr_elem(node.iftrue)
            return found if found is not None else self.expr_ptr_elem(node.iffalse)
        if isinstance(node, c_ast.Assignment):
            return self.expr_ptr_elem(node.lvalue)
        if isinstance(node, c_ast.FuncCall) and isinstance(node.name, c_ast.ID):
            fn = self.function_asts.get(node.name.name)
            if fn is not None and isinstance(fn.decl.type.type, c_ast.PtrDecl):
                return self.ptr_pointee_size(fn.decl.type.type)
        return None

    def lvalue_elem_size(self, node):
        """Width in bytes of the object an lvalue designates (emits no code)."""
        if isinstance(node, c_ast.ID):
            addr = self.var_addr(node.name)
            if addr in self.array_by_addr:
                return self.array_by_addr[addr]
            return self.addr_size(addr)
        if isinstance(node, c_ast.ArrayRef):
            elem = self.expr_ptr_elem(node.name)
            if elem is None:
                raise NotImplementedError("subscripted value is neither an array nor a pointer")
            return elem
        if isinstance(node, c_ast.StructRef):
            base_type = self.resolve_type_for_expr(node.expr)
            if isinstance(base_type, c_ast.TypeDecl) and isinstance(base_type.type, c_ast.Struct):
                info = self.struct_field_info(base_type.type, node.field.name)
                return info["size"]
            return 1
        if isinstance(node, c_ast.UnaryOp) and node.op == "*":
            elem = self.expr_ptr_elem(node.expr)
            if elem is None:
                raise NotImplementedError("cannot dereference a value that is not a pointer")
            return elem
        raise NotImplementedError("expression is not an lvalue")

    def expr_is_rom(self, node):
        """True for string literals and const arrays, which live in ROM and
        therefore can't be used through a (RAM-only) pointer."""
        if isinstance(node, c_ast.Constant):
            return node.type == "string"
        if isinstance(node, c_ast.BinaryOp) and node.op in ("+", "-"):
            return self.expr_is_rom(node.left) or self.expr_is_rom(node.right)
        if isinstance(node, c_ast.Cast):
            return self.expr_is_rom(node.expr)
        return isinstance(node, c_ast.ID) and node.name in self.const_arrays and node.name not in self.vars

    def check_not_rom(self, node):
        """Allow read-only ROM-backed pointers for the VM's flat-memory model.

        MicroCity relies on AVR-style PROGMEM pointers and string literals that
        point into ROM; these are read-only references, not writable RAM
        addresses. The compiler's earlier blanket rejection blocks valid code
        like `const char *s = "text";` and `const BuildingInfo *info =
        &BuildingMetaData[...]`.
        """
        return

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

    def add_const_array(self, name, elem_bytes, values, type_node=None, relocations=None):
        base = self.alloc_temp(INT_SIZE)
        instr_start = len(self.buf)
        # placeholder ROM offset, patched in finalize() once we know it
        self.emit("setreg", RA, 0)
        patch_pos = instr_start + 1 + 1  # opcode(1) + reg(1), then the 4-byte val
        self.emit_store(RA, base, INT_SIZE)
        self.const_arrays[name] = {"base": base, "elem_bytes": elem_bytes, "count": len(values)}
        if type_node is not None:
            self.const_array_types[name] = type_node
        self.array_elem_bytes[name] = elem_bytes
        self.rom_data.append({
            "name": name,
            "patch_pos": patch_pos,
            "data": self.pack_values(values, elem_bytes),
            "relocations": relocations or [],
        })
        return base

    def const_array_access(self, node):
        indices = []
        current = node
        while isinstance(current, c_ast.ArrayRef):
            indices.append(current.subscript)
            current = current.name
        if not isinstance(current, c_ast.ID) or current.name not in self.const_arrays:
            return None
        array_type = self.const_array_types.get(current.name)
        if array_type is None:
            if len(indices) != 1:
                return None
            return current.name, list(reversed(indices)), None
        index_types = []
        for index in reversed(indices):
            if not isinstance(array_type, c_ast.ArrayDecl):
                return None
            array_type = array_type.type
            index_types.append(array_type)
        return current.name, list(reversed(indices)), array_type

    def gen_const_array_address(self, node):
        access = self.const_array_access(node)
        if access is None:
            return None
        name, indices, result_type = access
        if result_type is None:
            elem_bytes = self.const_arrays[name]["elem_bytes"]
            stride_sizes = [elem_bytes]
        else:
            stride_sizes = []
            current_type = self.const_array_types[name]
            for _ in indices:
                current_type = current_type.type
                stride_sizes.append(self.type_size(current_type))

        index_values = [self.gen_expr(index) for index in indices]
        offset = self.alloc_temp(INT_SIZE)
        self.emit_copy(self.const_arrays[name]["base"], offset)
        for index_value, stride in zip(index_values, stride_sizes):
            self.emit_load(offset, INT_SIZE, RA)
            self.emit_load(index_value, self.addr_size(index_value), RB)
            if stride != 1:
                self.emit("mulregv", RB, stride, RB)
            self.emit("addreg", RA, RB, RA)
            self.emit_store(RA, offset, INT_SIZE)
        return offset, result_type

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
        rom_offsets = {}
        for item in self.rom_data:
            self.buf[item["patch_pos"]:item["patch_pos"] + 4] = byte_offset.to_bytes(4, "big")
            rom_offsets[item["name"]] = byte_offset
            byte_offset += len(item["data"])
        for item in self.rom_data:
            data = bytearray(item["data"])
            for offset, target in item["relocations"]:
                if target not in rom_offsets:
                    raise ValueError(f"ROM pointer in '{item['name']}' refers to unknown array '{target}'")
                data[offset:offset + INT_SIZE] = rom_offsets[target].to_bytes(INT_SIZE, "big")
            self.buf.extend(data)
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

    # -- lvalues / pointer helpers ---------------------------------------------
    @staticmethod
    def has_side_effects(node):
        if isinstance(node, (c_ast.Assignment, c_ast.FuncCall)):
            return True
        if isinstance(node, c_ast.UnaryOp) and node.op in ("p++", "++", "p--", "--"):
            return True
        if isinstance(node, (c_ast.ArrayRef, c_ast.StructRef)):
            return False
        if isinstance(node, c_ast.UnaryOp) and node.op in ("*", "&"):
            return False
        if isinstance(node, c_ast.Cast):
            return Compiler.has_side_effects(node.expr)
        return any(Compiler.has_side_effects(child) for _, child in node.children())

    def emit_addr_in_ra(self, node):
        """Emit code that leaves the runtime byte address designated by an lvalue
        (array element, pointer subscript, or *pointer) in RA, and return the
        element width in bytes. Nothing else may be emitted between this call
        and the instruction that consumes RA."""
        if isinstance(node, c_ast.ArrayRef):
            const_access = self.gen_const_array_address(node)
            if const_access is not None:
                offset, result_type = const_access
                if isinstance(result_type, c_ast.ArrayDecl):
                    elem_bytes = self.type_size(result_type.type)
                elif result_type is not None:
                    elem_bytes = self.type_size(result_type)
                else:
                    elem_bytes = self.const_arrays[self.const_array_access(node)[0]]["elem_bytes"]
                self.emit_load(offset, INT_SIZE, RA)
                return elem_bytes
        if isinstance(node, c_ast.StructRef):
            field = node.field.name
            base = getattr(node, "expr", getattr(node, "name", None))
            base_type = None
            if isinstance(base, c_ast.ID):
                base_type = self.resolve_type_for_expr(base)
                base_addr = self.var_addr(base.name)
                self.emit_set_const(RA, base_addr)
            elif isinstance(base, c_ast.ArrayRef) or (isinstance(base, c_ast.UnaryOp) and base.op == "*"):
                self.emit_addr_in_ra(base)
                base_type = self.resolve_type_for_expr(base)
            elif isinstance(base, c_ast.StructRef):
                self.emit_addr_in_ra(base)
                base_type = self.resolve_type_for_expr(base)
            else:
                base_addr = self.gen_expr(base)
                self.emit_load(base_addr, self.addr_size(base_addr), RA)
                base_type = self.resolve_type_for_expr(base)
            if base_type is None:
                raise NotImplementedError(f"cannot resolve type for struct field access '{field}'")
            if isinstance(base_type, c_ast.TypeDecl) and isinstance(base_type.type, c_ast.IdentifierType):
                for name in base_type.type.names:
                    if name in self.struct_defs:
                        base_type = c_ast.TypeDecl(declname=base_type.declname, quals=base_type.quals, align=base_type.align, type=self.struct_defs[name])
                        break
            if isinstance(base_type, c_ast.PtrDecl):
                target = base_type.type
                if isinstance(target, c_ast.TypeDecl) and isinstance(target.type, c_ast.IdentifierType):
                    for name in target.type.names:
                        if name in self.struct_defs:
                            target = c_ast.TypeDecl(declname=target.declname, quals=target.quals, align=target.align, type=self.struct_defs[name])
                            break
                if not isinstance(target, c_ast.TypeDecl) or not isinstance(target.type, c_ast.Struct):
                    raise NotImplementedError(f"field '{field}' requires a struct or pointer-to-struct base")
                base_type = target
            if not isinstance(base_type, c_ast.TypeDecl) or not isinstance(base_type.type, c_ast.Struct):
                raise NotImplementedError(f"field '{field}' requires a struct or pointer-to-struct base")
            info = self.struct_field_info(base_type.type, field)
            field_decl = None
            for decl in base_type.type.decls or []:
                if decl.name == field:
                    field_decl = decl
                    break
            if field_decl is not None and isinstance(field_decl.type, c_ast.ArrayDecl):
                self.emit("addregv", RA, info["offset"], RA)
                return self.type_size(field_decl.type.type)
            self.emit("addregv", RA, info["offset"], RA)
            return info["size"]
        if isinstance(node, c_ast.ArrayRef):
            if (isinstance(node.name, c_ast.ID) and node.name.name in self.vars
                    and self.vars[node.name.name] in self.array_by_addr):
                base_addr = self.vars[node.name.name]
                elem_bytes = self.array_by_addr[base_addr]
                const_index = self.try_const(node.subscript)
                if const_index is not None:
                    self.emit_set_const(RA, base_addr + const_index * elem_bytes)
                    return elem_bytes
                index_addr = self.gen_expr(node.subscript)
                self.emit_load(index_addr, self.addr_size(index_addr), RA)
                if elem_bytes != 1:
                    self.emit("mulregv", RA, elem_bytes, RA)   # RA = index * elem_bytes
                self.emit("addregv", RA, base_addr, RA)        # RA = absolute runtime address
                return elem_bytes
            elem_bytes = self.expr_ptr_elem(node.name)
            if elem_bytes is None:
                raise NotImplementedError("subscripted value is neither an array nor a pointer")
            self.check_not_rom(node.name)
            ptr_addr = self.gen_expr(node.name)
            const_index = self.try_const(node.subscript)
            if const_index is not None:
                self.emit_load(ptr_addr, self.addr_size(ptr_addr), RA)
                if const_index * elem_bytes:
                    self.emit("addregv", RA, const_index * elem_bytes, RA)
                return elem_bytes
            index_addr = self.gen_expr(node.subscript)
            self.emit_load(index_addr, self.addr_size(index_addr), RA)
            if elem_bytes != 1:
                self.emit("mulregv", RA, elem_bytes, RA)
            self.emit_load(ptr_addr, self.addr_size(ptr_addr), RB)
            self.emit("addreg", RA, RB, RA)
            return elem_bytes
        if isinstance(node, c_ast.UnaryOp) and node.op == "*":
            elem_bytes = self.expr_ptr_elem(node.expr)
            if elem_bytes is None:
                raise NotImplementedError("cannot dereference a value that is not a pointer")
            self.check_not_rom(node.expr)
            ptr_addr = self.gen_expr(node.expr)
            self.emit_load(ptr_addr, self.addr_size(ptr_addr), RA)
            return elem_bytes
        raise NotImplementedError("expression is not an assignable location")

    def gen_incdec(self, node, want_value):
        """++/-- (prefix or postfix) on a variable, array element or *pointer.
        Pointers move by whole elements. With want_value=False (statement
        context) no copy of the old value is made."""
        increment = "+" in node.op
        post = node.op.startswith("p")
        target = node.expr
        step = self.expr_ptr_elem(target) or 1
        opname = "addregv" if increment else "subregv"
        if isinstance(target, c_ast.ID):
            addr = self.var_addr(target.name)
            size = self.addr_size(addr)
            old = None
            if want_value and post:
                old = self.alloc_temp(size)
                self.emit_copy(addr, old)
            self.emit_load(addr, size, RA)
            self.emit(opname, RA, step, RA)
            self.emit_store(RA, addr, size)
            return old if old is not None else addr
        elem = self.emit_addr_in_ra(target)
        self.emit(f"loadregi{self._reg_width(elem)}", RA, RB)
        old = None
        if want_value and post:
            old = self.alloc_temp(elem)
            self.emit_store(RB, old, elem)
        self.emit(opname, RB, step, RB)
        self.emit(f"storeregi{self._reg_width(elem)}", RA, RB)
        if not want_value:
            return None
        if post:
            return old
        result = self.alloc_temp(elem)
        self.emit_store(RB, result, elem)
        return result

    def gen_unary(self, node):
        op = node.op
        if op == "&":
            target = node.expr
            if isinstance(target, c_ast.ID):
                # Must produce a temp whose VALUE is the address (so
                # emit_copy/gen_expr consumers -- which always treat a
                # gen_expr result as "a location to load from" -- see a
                # location holding the address, not the address itself
                # misread as a location).
                addr_val = self.var_addr(target.name)
                out = self.alloc_temp(INT_SIZE)
                self.emit_store_const(out, INT_SIZE, addr_val)
                return out
            self.emit_addr_in_ra(target)
            out = self.alloc_temp(INT_SIZE)
            self.emit_store(RA, out, INT_SIZE)
            return out
        if op in ("p++", "++", "p--", "--"):
            return self.gen_incdec(node, want_value=True)
        if op == "*":
            elem = self.emit_addr_in_ra(node)
            self.emit(f"loadregi{self._reg_width(elem)}", RA, RB)
            result = self.alloc_temp(elem)
            self.emit_store(RB, result, elem)
            return result
        if op == "+":
            return self.gen_expr(node.expr)
        if op == "-":
            operand = self.resolve_operand(node.expr)
            self.emit_set_const(RA, 0)
            self.load_resolved(operand, RB)
            self.emit("subreg", RA, RB, RA)
            dest = self.alloc_temp(INT_SIZE)
            self.emit_store(RA, dest, INT_SIZE)
            return dest
        if op == "~":
            operand = self.resolve_operand(node.expr)
            self.load_resolved(operand, RA)
            self.emit("notreg", RA, RA)
            dest = self.alloc_temp(INT_SIZE)
            self.emit_store(RA, dest, INT_SIZE)
            return dest
        if op == "!":
            operand = self.resolve_operand(node.expr)
            self.load_resolved(operand, RA)
            self.emit_set_const(RB, 0)
            self.emit("eqreg", RA, RB, RA)
            dest = self.alloc_temp(1)
            self.emit_store(RA, dest, 1)
            return dest
        if op == "sizeof":
            raise NotImplementedError("sizeof is only supported on types, variables and array/pointer elements")
        raise NotImplementedError(f"unsupported unary operator '{op}'")

    def gen_pointer_arith(self, node, left_elem, right_elem):
        op = node.op
        if left_elem is not None and right_elem is not None:
            if op != "-":
                raise NotImplementedError("cannot add two pointers")
            left_r = self.resolve_operand(node.left)
            right_r = self.resolve_operand(node.right)
            self.load_resolved(left_r, RA)
            self.load_resolved(right_r, RB)
            self.emit("subreg", RA, RB, RA)
            if left_elem != 1:
                self.emit_set_const(RB, left_elem)
                self.emit("divreg", RA, RB, RA)  # distance in elements, like C
            dest = self.alloc_temp(INT_SIZE)
            self.emit_store(RA, dest, INT_SIZE)
            return dest
        if left_elem is not None:
            ptr_node, int_node, elem = node.left, node.right, left_elem
        else:
            if op != "+":
                raise NotImplementedError("cannot subtract a pointer from an integer")
            ptr_node, int_node, elem = node.right, node.left, right_elem
        ptr_r = self.resolve_operand(ptr_node)
        int_r = self.resolve_operand(int_node)
        self.load_resolved(ptr_r, RA)
        self.load_resolved(int_r, RB)
        if elem != 1:
            self.emit("mulregv", RB, elem, RB)  # scale by the element size, like C
        self.emit("addreg" if op == "+" else "subreg", RA, RB, RA)
        dest = self.alloc_temp(INT_SIZE)
        self.emit_store(RA, dest, INT_SIZE)
        return dest

    # -- expressions: returns the byte-address holding the result --------------
    def gen_expr(self, node):
        if isinstance(node, (c_ast.UnaryOp, c_ast.BinaryOp, c_ast.TernaryOp, c_ast.Cast)):
            value = self.try_const(node)  # constant folding
            if value is not None:
                dest = self.alloc_temp(INT_SIZE)
                self.emit_store_const(dest, INT_SIZE, value)
                return dest

        if isinstance(node, c_ast.Constant):
            if node.type == "string":
                name = f"__str{len(self.rom_data)}"
                return self.add_const_array(name, TYPE_BYTES["char"], self.string_bytes(node) + [0])
            if node.type != "char" and not node.type.endswith("int"):
                raise NotImplementedError(f"unsupported constant type '{node.type}'")
            dest = self.alloc_temp(INT_SIZE)
            self.emit_store_const(dest, INT_SIZE, self.const_value(node))
            return dest

        if isinstance(node, c_ast.ID):
            if node.name in self.vars:
                addr = self.vars[node.name]
                if addr in self.array_by_addr:
                    # a RAM array decays to the address of its first element
                    out = self.alloc_temp(INT_SIZE)
                    self.emit_store_const(out, INT_SIZE, addr)
                    return out
                return addr
            if node.name in self.const_arrays:
                return self.const_arrays[node.name]["base"]
            if node.name in ("true", "false", "NULL", "nullptr") or node.name in self.constants:
                dest = self.alloc_temp(INT_SIZE)
                self.emit_store_const(dest, INT_SIZE, self.const_value(node))
                return dest
            return self.var_addr(node.name)  # raises: use of undeclared variable

        if isinstance(node, c_ast.StructRef):
            field_name = node.field.name
            base = getattr(node, "expr", getattr(node, "name", None))
            base_type = self.resolve_type_for_expr(base)
            struct_type = self.resolve_struct_type(base_type)
            if struct_type is None:
                raise NotImplementedError(f"unsupported struct field access '{field_name}'")
            field_size = self.struct_field_info(struct_type, field_name)["size"]
            field_decl = None
            for decl in (struct_type.decls or []):
                if decl.name == field_name:
                    field_decl = decl
                    break
            if field_decl is not None and isinstance(field_decl.type, c_ast.ArrayDecl):
                result = self.alloc_temp(INT_SIZE)
                self.emit_addr_in_ra(node)
                self.emit_store(RA, result, INT_SIZE)
                return result
            if node.type == '.':
                if isinstance(base, c_ast.ID):
                    base_addr = self.var_addr(base.name)
                    self.emit_set_const(RA, base_addr)
                elif isinstance(base, c_ast.ArrayRef) or (isinstance(base, c_ast.UnaryOp) and base.op == "*"):
                    self.emit_addr_in_ra(base)
                else:
                    self.emit_load(self.gen_expr(base), self.addr_size(self.gen_expr(base)), RA)
                self.emit("addregv", RA, self.struct_field_info(struct_type, field_name)["offset"], RA)
            else:
                self.load_resolved(self.resolve_operand(base), RA)
                self.emit("addregv", RA, self.struct_field_info(struct_type, field_name)["offset"], RA)
            self.emit(f"loadregi{self._reg_width(field_size)}", RA, RB)
            result = self.alloc_temp(field_size)
            self.emit_store(RB, result, field_size)
            return result

        if isinstance(node, c_ast.ArrayRef):
            const_access = self.gen_const_array_address(node)
            if const_access is not None:
                offset, result_type = const_access
                if isinstance(result_type, c_ast.ArrayDecl):
                    result = self.alloc_temp(INT_SIZE)
                    self.emit_copy(offset, result)
                    return result
                elem_bytes = self.type_size(result_type) if result_type is not None else self.const_arrays[
                    self.const_array_access(node)[0]]["elem_bytes"]
                self.emit_load(offset, INT_SIZE, RA)
                self.emit(f"romread{elem_bytes * 8}", RA, RB)
                result = self.alloc_temp(elem_bytes)
                self.emit_store(RB, result, elem_bytes)
                return result

            elem_bytes = self.emit_addr_in_ra(node)
            self.emit(f"loadregi{self._reg_width(elem_bytes)}", RA, RB)
            result = self.alloc_temp(elem_bytes)
            self.emit_store(RB, result, elem_bytes)
            return result

        if isinstance(node, c_ast.UnaryOp):
            return self.gen_unary(node)

        if isinstance(node, c_ast.Assignment):
            if node.op != "=":
                # compound assignment: `a OP= b` is compiled as `a = a OP b`
                if self.has_side_effects(node.lvalue):
                    raise NotImplementedError(
                        f"'{node.op}' on a target with side effects (e.g. a[i++] {node.op} 1) is not supported")
                node = c_ast.Assignment("=", node.lvalue,
                                        c_ast.BinaryOp(node.op[:-1], node.lvalue, node.rvalue, node.coord),
                                        node.coord)

            if isinstance(node.lvalue, c_ast.ID):
                name = node.lvalue.name
                if name not in self.vars and name in self.constants:
                    raise NotImplementedError(f"cannot assign to constant '{name}'")
                if self.vars.get(name) in self.ptr_elem:
                    self.check_not_rom(node.rvalue)

            rhs = self.gen_expr(node.rvalue)

            if isinstance(node.lvalue, c_ast.ID):
                dest = self.var_addr(node.lvalue.name)
                self.emit_copy(rhs, dest)
                return dest

            # array element, pointer subscript or *pointer: the address goes
            # in RA only after rhs is already evaluated into a temp
            elem_bytes = self.emit_addr_in_ra(node.lvalue)
            self.emit_load(rhs, self.addr_size(rhs), RB)
            self.emit(f"storeregi{self._reg_width(elem_bytes)}", RA, RB)
            return rhs

        if isinstance(node, c_ast.TernaryOp):
            dest = self.alloc_temp(INT_SIZE)
            else_label = Label("ternary_else")
            end_label = Label("ternary_end")
            cond = self.gen_cond(node.cond)
            self.emit_load(cond, 1, RA)
            self.emit("ifnotreg", RA, else_label)
            self.emit_copy(self.gen_expr(node.iftrue), dest)
            self.emit("setpc", end_label)
            self.define_label(else_label)
            self.emit_copy(self.gen_expr(node.iffalse), dest)
            self.define_label(end_label)
            return dest

        if isinstance(node, c_ast.Cast):
            target = node.to_type.type
            if self.is_void_type(target):
                self.gen_expr(node.expr)  # evaluated only for its side effects
                return self.alloc_temp(INT_SIZE)
            if isinstance(target, c_ast.ArrayDecl):
                raise NotImplementedError("cannot cast to an array type")
            size = INT_SIZE if isinstance(target, c_ast.PtrDecl) else self.scalar_size(target)
            src = self.gen_expr(node.expr)
            dest = self.alloc_temp(size)
            self.emit_load(src, self.addr_size(src), RA)  # zero-extend / truncate through the register
            self.emit_store(RA, dest, size)
            return dest

        if isinstance(node, c_ast.ExprList):
            result = None
            for item in node.exprs:
                result = self.gen_expr(item)
            return result

        if isinstance(node, c_ast.BinaryOp):
            comparisons = ("==", "!=", "<", ">", "<=", ">=")
            if node.op in comparisons:
                # Comparisons now produce a usable 0/1 value, not just a
                # condition for if/while -- gen_cond already builds exactly
                # that, so reuse it here.
                return self.gen_cond(node)

            if node.op in ("&&", "||"):
                # short-circuit like C: the right operand is only evaluated
                # when the left one doesn't already decide the result
                dest = self.alloc_temp(1)
                end_label = Label("shortcircuit")
                left_bool = self.gen_bool_operand(node.left)
                self.emit_load(left_bool, 1, RA)
                self.emit_store(RA, dest, 1)
                self.emit("ifnotreg" if node.op == "&&" else "ifreg", RA, end_label)
                right_bool = self.gen_bool_operand(node.right)
                self.emit_load(right_bool, 1, RA)
                self.emit_store(RA, dest, 1)
                self.define_label(end_label)
                return dest

            # Arithmetic and bitwise results are 32-bit (C's integer promotion):
            # `uint8 a = 200; a + 100` is 300, not 44. Narrowing only happens
            # when the value is stored back into a narrower variable.
            bitwise_ops = {"&": "andreg", "|": "orreg", "^": "xorreg", "<<": "shlreg", ">>": "shrreg"}
            if node.op in bitwise_ops:
                left_r = self.resolve_operand(node.left)
                right_r = self.resolve_operand(node.right)
                self.load_resolved(left_r, RA)
                self.load_resolved(right_r, RB)
                self.emit(bitwise_ops[node.op], RA, RB, RA)
                dest = self.alloc_temp(INT_SIZE)
                self.emit_store(RA, dest, INT_SIZE)
                return dest

            const_ops = {"+": "addregv", "-": "subregv", "*": "mulregv"}
            reg_ops = {"+": "addreg", "-": "subreg", "*": "mulreg", "/": "divreg", "%": "modreg"}
            if node.op not in reg_ops:
                raise NotImplementedError(f"unsupported operator '{node.op}' in expression")
            if node.op in ("+", "-"):
                left_elem = self.expr_ptr_elem(node.left)
                right_elem = self.expr_ptr_elem(node.right)
                if left_elem is not None or right_elem is not None:
                    return self.gen_pointer_arith(node, left_elem, right_elem)
            right_const = self.try_const(node.right)
            left_const = self.try_const(node.left)
            if right_const is not None and node.op in const_ops:
                self.load_operand(node.left, RA)
                self.emit(const_ops[node.op], RA, right_const, RA)
            elif left_const is not None and node.op in ("+", "*"):
                # commutative -- swap so the constant still uses the cheaper *v opcode
                self.load_operand(node.right, RA)
                self.emit(const_ops[node.op], RA, left_const, RA)
            else:
                left_r = self.resolve_operand(node.left)
                right_r = self.resolve_operand(node.right)
                self.load_resolved(left_r, RA)
                self.load_resolved(right_r, RB)
                self.emit(reg_ops[node.op], RA, RB, RA)
            dest = self.alloc_temp(INT_SIZE)
            self.emit_store(RA, dest, INT_SIZE)
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
            if name == "rand":
                return self.gen_rand(node)
            if name == "numtostr":
                return self.gen_numtostr(node)
            if name == "gettime":
                return self.gen_gettime()
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
            if name in ("flag", "setpixel", "exit", "srand", "drawchar", "drawstring"):
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
            left_size = INT_SIZE if left_r[0] == "const" else left_r[2]
            right_size = INT_SIZE if right_r[0] == "const" else right_r[2]
            self.load_comparison_operand(node.left, left_r, RA)
            self.load_comparison_operand(node.right, right_r, RB)
            if node.op not in ("==", "!=") and self.comparison_is_signed(
                    node.left, node.right, left_size, right_size):
                self.emit_set_const(RC, 0x80000000)
                self.emit("xorreg", RA, RC, RA)
                self.emit("xorreg", RB, RC, RB)
            self.emit(opmap[node.op], RA, RB, RA)
            bool_addr = self.alloc_temp(1)
            self.emit_store(RA, bool_addr, 1)
            return bool_addr

        value = self.try_const(node)
        if value is not None:
            bool_addr = self.alloc_temp(1)
            self.emit_store_const(bool_addr, 1, 1 if value != 0 else 0)
            return bool_addr

        return self.gen_bool_operand(node)

    # -- printf ----------------------------------------------------------------
    def gen_printf(self, call):
        args = call.args.exprs if call.args else []
        if not args or not isinstance(args[0], c_ast.Constant) or args[0].type != "string":
            raise NotImplementedError("printf's first argument must be a string literal")
        fmt = bytes(args[0].value[1:-1], "utf-8").decode("unicode_escape")
        rest = args[1:]
        trailing_newline = fmt.endswith("\n")
        if trailing_newline:
            fmt = fmt[:-1]

        pieces = []  # ("text", str) or ("spec", conversion char)
        literal = []
        i = 0
        while i < len(fmt):
            if fmt[i] != "%":
                literal.append(fmt[i])
                i += 1
                continue
            m = re.match(r"%(%|(?:hh|h|ll|l|z)?([diucs]))", fmt[i:])
            if not m:
                raise NotImplementedError(
                    f"unsupported printf format at '{fmt[i:i + 4]}' (supported: %d %i %u %c %s %%)")
            if m.group(1) == "%":
                literal.append("%")
            else:
                if literal:
                    pieces.append(("text", "".join(literal)))
                    literal = []
                pieces.append(("spec", m.group(2)))
            i += m.end()
        if literal:
            pieces.append(("text", "".join(literal)))

        if sum(1 for kind, _ in pieces if kind == "spec") > len(rest):
            raise NotImplementedError("printf format has more specifiers than arguments")

        arg_i = 0
        for idx, (kind, val) in enumerate(pieces):
            if kind == "text":
                if trailing_newline and idx == len(pieces) - 1:
                    self.emit("print", val)  # text + newline in one instruction
                    trailing_newline = False
                else:
                    self.emit("printn", val)
                continue
            arg = rest[arg_i]
            arg_i += 1
            if val in "diu":
                addr = self.gen_expr(arg)
                self.emit_load(addr, self.addr_size(addr), RA)
                self.emit("outreg", RA)  # prints the register's full numeric value, any width
            elif val == "c":
                self.gen_print_char(arg)
            else:
                self.gen_print_str(arg)
        if trailing_newline:
            self.emit("print", "")

    def gen_print_char(self, arg):
        addr = self.gen_expr(arg)
        tmp = self.alloc_temp(1)
        self.emit_load(addr, self.addr_size(addr), RA)
        self.emit_store(RA, tmp, 1)
        self.emit("outc", tmp)

    def gen_print_str(self, arg, newline=False):
        if isinstance(arg, c_ast.Constant) and arg.type == "string":
            text = bytes(arg.value[1:-1], "utf-8").decode("unicode_escape")
            if text or newline:
                self.emit("print" if newline else "printn", text)
            return
        if self.expr_ptr_elem(arg) != 1:
            raise NotImplementedError("%s / puts need a char array, char pointer or string literal")
        rom = self.expr_is_rom(arg)
        ptr = self.gen_expr(arg)
        tmp = self.alloc_temp(1)
        loop, done = Label("strloop"), Label("strdone")
        self.emit_load(ptr, INT_SIZE, RA)
        self.define_label(loop)
        self.emit("romread8" if rom else "loadregi8", RA, RB)
        self.emit("ifnotreg", RB, done)
        self.emit_store(RB, tmp, 1)
        self.emit("outc", tmp)
        self.emit("addregv", RA, 1, RA)
        self.emit("setpc", loop)
        self.define_label(done)
        if newline:
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
        flag = self.const_value(args[0])
        val = self.const_value(args[1])
        self.emit("isflagsupported", flag, val, 12)
        dest = self.alloc_temp(1)
        self.emit_store(12, dest, 1)
        return dest

    def gen_rand(self, call):
        # rand(min, max) -- max is EXCLUDED, matching the VM's randreg
        # semantics. min/max are register operands now (loaded at runtime
        # via RA/RB), so either argument can be a variable or expression,
        # not just a constant. If max <= min at runtime, the VM leaves dst
        # unchanged (a no-op), same convention as divreg/modreg.
        args = call.args.exprs if call.args else []
        if len(args) == 0:  # C-style rand(): 0..RAND_MAX (32767)
            self.emit_set_const(RA, 0)
            self.emit_set_const(RB, 32768)
        elif len(args) != 2:
            raise NotImplementedError("rand() or rand(min, max) -- max is exclusive")
        else:
            min_r = self.resolve_operand(args[0])
            max_r = self.resolve_operand(args[1])
            self.load_resolved(min_r, RA)
            self.load_resolved(max_r, RB)
        self.emit("randreg", RA, RB, RA)
        dest = self.alloc_temp(INT_SIZE)
        self.emit_store(RA, dest, INT_SIZE)
        return dest

    def gen_srand(self, call):
        args = call.args.exprs if call.args else []
        if len(args) != 1:
            raise NotImplementedError("srand(seed) takes exactly 1 argument")
        seed_r = self.resolve_operand(args[0])
        self.load_resolved(seed_r, RA)
        self.emit("srandreg", RA)

    def gen_gettime(self):
        self.emit("gettime", 12)
        dest = self.alloc_temp(INT_SIZE)
        self.emit_store(12, dest, INT_SIZE)
        return dest

    def gen_numtostr(self, call):
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("numtostr(value, buf) takes exactly 2 arguments")
        val_node, buf_node = args
        if not isinstance(buf_node, c_ast.ID) or buf_node.name not in self.array_elem_bytes:
            raise NotImplementedError("numtostr()'s second argument must be a char[] buffer")
        if self.array_elem_bytes[buf_node.name] != 1:
            raise NotImplementedError("numtostr()'s buffer must be a char[]")
        buf_addr = self.var_addr(buf_node.name)
        val_r = self.resolve_operand(val_node)
        self.load_resolved(val_r, RA)
        self.emit("numtostr", RA, buf_addr, 12)
        dest = self.alloc_temp(1)
        self.emit_store(12, dest, 1)
        return dest

    # -- text drawing (drawchar/drawstring) ------------------------------------
    # Scratch registers 8-13 are used here (never RA/RB) since these calls are
    # always self-contained statements/expressions: every input register is
    # loaded immediately before emitting "drawstring", and the two output
    # registers (14, 15) are discarded right after -- no value needs to
    # survive across separate emit() calls the way arithmetic temps do.
    def gen_draw_common(self, x_node, y_node, addr_kind, addr_val, length, src, color=1):
        x_r = self.resolve_operand(x_node)
        y_r = self.resolve_operand(y_node)
        self.load_resolved(x_r, 8)
        self.load_resolved(y_r, 9)
        self.emit_set_const(10, color)
        if addr_kind == "const":
            self.emit_set_const(11, addr_val)
        else:  # "load" -- addr_val is a RAM address whose runtime VALUE is the actual address/ROM offset
            self.emit_load(addr_val, INT_SIZE, 11)
        self.emit_set_const(12, length)
        self.emit("drawstring", 8, 9, 10, 11, 12, src, 14, 15)

    def gen_drawchar(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 3:
            raise NotImplementedError("drawchar(c, x, y) takes exactly 3 arguments")
        c_node, x_node, y_node = args
        c_r = self.resolve_operand(c_node)
        self.load_resolved(c_r, RA)
        char_addr = self.alloc_temp(1)
        self.emit_store(RA, char_addr, 1)
        self.gen_draw_common(x_node, y_node, "const", char_addr, 1, 0)

    def gen_drawstring(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 3:
            raise NotImplementedError("drawstring(str, x, y) takes exactly 3 arguments")
        str_node, x_node, y_node = args
        if isinstance(str_node, c_ast.Constant) and str_node.type == "string":
            # String literal -- lives in ROM (rom_data), src=1. gen_expr() on a
            # string constant allocates the const array and returns a RAM temp
            # whose runtime VALUE is the ROM byte offset (patched in finalize()).
            chars = self.string_bytes(str_node)
            rom_offset_temp = self.gen_expr(str_node)
            self.gen_draw_common(x_node, y_node, "load", rom_offset_temp, len(chars), 1)
        elif isinstance(str_node, c_ast.ID) and str_node.name in self.array_elem_bytes:
            # Plain char[] variable -- lives in RAM, src=0. Its address is a
            # compile-time constant, so it's used directly (no load needed).
            name = str_node.name
            if self.array_elem_bytes[name] != 1:
                raise NotImplementedError("drawstring() requires a char[] array")
            base_addr = self.var_addr(name)
            total_bytes = self.var_sizes[name]
            self.gen_draw_common(x_node, y_node, "const", base_addr, total_bytes, 0)
        else:
            raise NotImplementedError("drawstring() requires a string literal or a char[] variable")

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
        params = self.fn_params(fn)
        if len(args) != len(params):
            raise NotImplementedError(f"'{name}' expects {len(params)} argument(s), got {len(args)}")

        instance = self.next_instance
        self.next_instance += 1
        prefix = f"{name}#{instance}."

        # Evaluate arguments *before* shadowing names, so `foo(x)` uses the
        # caller's x even if the callee also has a parameter named x.
        for a, p in zip(args, params):
            if self.param_pointee(p) is not None:
                self.check_not_rom(a)
        arg_vals = [self.gen_expr(a) for a in args]

        shadowed = {}  # name -> previous address/size/type (or NO_PREV sentinel) to restore after inlining
        NO_PREV = object()
        for p, val in zip(params, arg_vals):
            param_size = self.param_size(p)
            addr = self.alloc_var(prefix + p.name, param_size)
            pointee = self.param_pointee(p)
            if pointee is not None:
                self.ptr_elem[addr] = pointee
            self.emit_copy(val, addr)
            shadowed[p.name] = (
                self.vars.get(p.name, NO_PREV),
                self.var_sizes.get(p.name, NO_PREV),
                self.var_types.get(p.name, NO_PREV),
            )
            self.vars[p.name] = addr
            self.var_sizes[p.name] = param_size
            self.var_types[p.name] = p.type

        ret_addr = self.alloc_var(prefix + "__ret", self.function_return_size(fn))
        end_label = Label(f"end_{prefix}")

        self.inlining_stack.append(name)
        self.return_stack.append((ret_addr, end_label))
        saved_loops = (self.break_stack, self.continue_stack)
        self.break_stack, self.continue_stack = [], []
        self.gen_stmt(fn.body)
        self.break_stack, self.continue_stack = saved_loops
        self.return_stack.pop()
        self.inlining_stack.pop()
        self.define_label(end_label)

        for pname, (prev, prev_size, prev_type) in shadowed.items():
            if prev is NO_PREV:
                self.vars.pop(pname, None)
                self.var_sizes.pop(pname, None)
                self.var_types.pop(pname, None)
            else:
                self.vars[pname] = prev
                self.var_sizes[pname] = prev_size
                self.var_types[pname] = prev_type

        return ret_addr

    # -- declarations --------------------------------------------------------------
    def register_enum(self, enum):
        if enum.values is None:
            return
        value = -1
        for item in enum.values.enumerators:
            value = self.const_value(item.value, "enum value") if item.value is not None else value + 1
            self.constants[item.name] = value & M32

    def register_typedef(self, node):
        if node.name in TYPE_BYTES or node.name in TYPE_ALIASES:
            return  # the compiler's own built-in typedefs
        ty = node.type
        if isinstance(ty, c_ast.TypeDecl) and isinstance(ty.type, c_ast.Enum):
            self.register_enum(ty.type)
            self.typedefs[node.name] = INT_SIZE
            return
        if isinstance(ty, c_ast.TypeDecl) and isinstance(ty.type, c_ast.Struct):
            self.register_struct(node.name, ty.type)
            self.typedefs[node.name] = self.structs[node.name]["size"]
            return
        if isinstance(ty, c_ast.TypeDecl) and isinstance(ty.type, c_ast.IdentifierType):
            if any(name in self.struct_defs for name in ty.type.names):
                for name in ty.type.names:
                    if name in self.struct_defs:
                        self.typedefs[node.name] = self.structs.get(name, {}).get("size", self.struct_size(self.struct_defs[name]))
                        self.struct_defs[node.name] = self.struct_defs[name]
                        break
                return
        if not isinstance(ty, c_ast.TypeDecl):
            raise NotImplementedError("only typedefs of integer, enum and struct types are supported")
        self.typedefs[node.name] = self.scalar_size(ty)

    def array_count(self, t, init):
        if t.dim is not None:
            count = self.const_value(t.dim, "array size")
            if count <= 0:
                raise NotImplementedError("array size must be positive")
            return count
        if isinstance(init, c_ast.InitList):
            return len(init.exprs)
        if isinstance(init, c_ast.Constant) and init.type == "string":
            return len(self.string_bytes(init)) + 1
        raise NotImplementedError("array size can't be inferred without an initializer")

    def const_struct_bytes(self, init, struct_node):
        if not isinstance(init, c_ast.InitList):
            raise NotImplementedError("struct initializers must use {...}")
        out = bytearray()
        values = list(init.exprs)
        for decl in struct_node.decls or []:
            if decl.name is None:
                continue
            if values:
                value = values.pop(0)
            else:
                value = c_ast.Constant(type="int", value="0")
            if isinstance(decl.type, c_ast.TypeDecl) and isinstance(decl.type.type, c_ast.Struct):
                size = self.type_size(decl.type)
                if not isinstance(value, c_ast.InitList):
                    raise NotImplementedError("nested struct initializers must be enclosed in {...}")
                raw = self.const_struct_bytes(value, decl.type.type)
                out.extend(raw)
                continue
            field_size = self.struct_field_size(decl)
            raw = value.to_bytes(field_size, "big") if isinstance(value, int) else self.const_scalar_bytes(value, decl.type, field_size)
            out.extend(raw)
        return bytes(out)

    def const_scalar_bytes(self, expr, type_node, forced_size=None):
        value = self.const_value(expr, "const initializer")
        size = forced_size if forced_size is not None else self.type_size(type_node)
        if size == 0:
            return b""
        if value < 0:
            value &= (1 << (size * 8)) - 1
        return value.to_bytes(size, "big")

    def const_array_element_bytes(self, init, type_node):
        if isinstance(type_node, c_ast.ArrayDecl):
            if not isinstance(init, c_ast.InitList):
                if isinstance(type_node.type, c_ast.TypeDecl):
                    return self.const_scalar_bytes(init, type_node.type)
                return self.const_scalar_bytes(init, type_node.type)
            out = bytearray()
            for item in init.exprs:
                out.extend(self.const_array_element_bytes(item, type_node.type))
            return bytes(out)
        if isinstance(type_node, c_ast.TypeDecl) and isinstance(type_node.type, c_ast.Struct):
            return self.const_struct_bytes(init, type_node.type)
        if isinstance(type_node, c_ast.TypeDecl) and isinstance(type_node.type, c_ast.IdentifierType):
            for name in type_node.type.names:
                if name in self.struct_defs:
                    return self.const_struct_bytes(init, self.struct_defs[name])
        if isinstance(init, c_ast.InitList):
            out = bytearray()
            for item in init.exprs:
                out.extend(self.const_array_element_bytes(item, type_node))
            return bytes(out)
        return self.const_scalar_bytes(init, type_node)

    def flatten_const_values(self, init, type_node):
        if isinstance(type_node, c_ast.ArrayDecl):
            if isinstance(init, c_ast.InitList):
                values = []
                for item in init.exprs:
                    if isinstance(item, c_ast.InitList):
                        raw = self.const_array_element_bytes(item, type_node.type)
                        values.append(int.from_bytes(raw, "big"))
                    else:
                        values.append(self.const_value(item, "const array value"))
                return values
            return [self.const_value(init, "const array value")]
        if isinstance(type_node, c_ast.TypeDecl) and isinstance(type_node.type, c_ast.Struct):
            raw = self.const_struct_bytes(init, type_node.type)
            return [int.from_bytes(raw, "big")]
        if isinstance(type_node, c_ast.TypeDecl) and isinstance(type_node.type, c_ast.IdentifierType):
            names = tuple(type_node.type.names)
            for name in names:
                if name in self.struct_defs:
                    if isinstance(init, c_ast.InitList) and init.exprs and all(isinstance(item, c_ast.InitList) for item in init.exprs):
                        values = []
                        for item in init.exprs:
                            raw = self.const_struct_bytes(item, self.struct_defs[name])
                            values.append(int.from_bytes(raw, "big"))
                        return values
                    raw = self.const_struct_bytes(init, self.struct_defs[name])
                    return [int.from_bytes(raw, "big")]
        if isinstance(init, c_ast.InitList):
            values = []
            for item in init.exprs:
                values.extend(self.flatten_const_values(item, type_node))
            return values
        return [self.const_value(init, "const array value")]

    def gen_array_init(self, node, addr, elem, count):
        init = node.init
        if isinstance(init, c_ast.Constant) and init.type == "string":
            if elem != 1:
                raise NotImplementedError("only char arrays can be initialized from a string literal")
            data = self.string_bytes(init) + [0]
            if len(data) > count:
                raise NotImplementedError(f"string is too long for array '{node.name}'")
            if not self.at_global_scope:
                data += [0] * (count - len(data))
            if all(b < 128 for b in data):
                self.emit("write", addr, bytes(data).decode("ascii"))  # one instruction for the whole string
                return
            items = [("const", b) for b in data]
        elif isinstance(init, c_ast.InitList):
            items = [("expr", e) for e in init.exprs]
        else:
            raise NotImplementedError("arrays must be initialized with a {...} list or a string literal")
        if len(items) > count:
            raise NotImplementedError(f"too many initializers for array '{node.name}'")
        for i in range(count):
            elem_addr = addr + i * elem
            self.addr_sizes[elem_addr] = elem
            if i < len(items):
                kind, item = items[i]
                value = item if kind == "const" else self.try_const(item)
                if value is None:
                    self.emit_copy(self.gen_expr(item), elem_addr)
                    continue
                self.emit_store_const(elem_addr, elem, value)
            elif not self.at_global_scope:  # globals start zeroed; locals must be cleared like C does
                self.emit_store_const(elem_addr, elem, 0)

    def register_struct(self, name, struct_node):
        if name is None:
            return
        self.struct_defs[name] = struct_node
        if name in self.structs:
            return
        if struct_node.decls is None:
            self.structs[name] = {"size": 0, "fields": {}}
            return
        fields = {}
        offset = 0
        for decl in struct_node.decls:
            if decl.name is None:
                continue
            size = self.struct_field_size(decl)
            fields[decl.name] = {"offset": offset, "size": size}
            offset += size
        self.structs[name] = {"size": offset, "fields": fields}

    def gen_decl(self, node):
        t = node.type
        enum = t.type if (isinstance(t, c_ast.TypeDecl) and isinstance(t.type, c_ast.Enum)) else (
            t if isinstance(t, c_ast.Enum) else None)
        if enum is not None:
            self.register_enum(enum)
        if isinstance(t, c_ast.TypeDecl) and isinstance(t.type, c_ast.Struct):
            if t.type.name is not None:
                self.register_struct(t.type.name, t.type)
        elif isinstance(t, c_ast.Struct) and t.name is not None:
            self.register_struct(t.name, t)
        if node.name is None or isinstance(t, c_ast.FuncDecl):
            return  # bare enum definition or a function prototype
        if "extern" in (node.storage or []) and node.init is None:
            return  # forward declaration; definition appears later in another translation unit
        if "static" in (node.storage or []) and self.inlining_stack:
            # The VM frontend is intentionally lightweight; static locals are
            # treated as ordinary persistent variables in the current translation
            # unit so MicroCity's file-local counters/seed state still compiles.
            pass

        if isinstance(t, c_ast.ArrayDecl) and self.is_const_decl(node) and node.init is not None:
            elem_bytes = self.type_size(t.type)
            if isinstance(t.type, c_ast.PtrDecl):
                if not isinstance(node.init, c_ast.InitList):
                    raise NotImplementedError("const pointer arrays need a {...} initializer")
                count = self.const_value(t.dim, "array size") if t.dim is not None else len(node.init.exprs)
                if len(node.init.exprs) > count:
                    raise NotImplementedError(f"const array '{node.name}' has too many initializers")
                values = [0] * count
                relocations = []
                for index, value in enumerate(node.init.exprs):
                    target = value
                    if isinstance(target, c_ast.UnaryOp) and target.op == "&":
                        target = target.expr
                    if isinstance(target, c_ast.ID):
                        relocations.append((index * elem_bytes, target.name))
                    elif self.try_const(value) != 0:
                        raise NotImplementedError(
                            f"const pointer array '{node.name}' entries must refer to ROM arrays or be null")
                self.add_const_array(node.name, elem_bytes, values, t, relocations)
                return
            if isinstance(node.init, c_ast.Constant) and node.init.type == "string":
                if elem_bytes != TYPE_BYTES["char"]:
                    raise NotImplementedError("string initializers require const char arrays")
                values = self.string_bytes(node.init) + [0]
            elif isinstance(node.init, c_ast.InitList):
                values = self.flatten_const_values(node.init, t.type)
            else:
                raise NotImplementedError("const arrays need a {...} list or string initializer")
            count = self.const_value(t.dim, "array size") if t.dim is not None else len(values)
            if len(values) > count:
                raise NotImplementedError(f"const array '{node.name}' has too many initializers")
            values.extend([0] * (count - len(values)))
            self.add_const_array(node.name, elem_bytes, values, t)
            return

        if isinstance(t, c_ast.TypeDecl) and self.is_const_decl(node) and node.init is not None:
            value = self.try_const(node.init)
            if value is not None:  # const scalar with a constant value: a compile-time constant
                self.constants[node.name] = value
                return

        self.var_types[node.name] = t
        meta = self.alloc_meta.get(node.name)
        elem_for_array = None
        ptr_pointee = None
        if meta is not None:
            # alloc<N> still specifies N in BITS for source compatibility;
            # round up to whole bytes since sub-byte packing is gone.
            elem_size = max(1, (meta["bits"] + 7) // 8)
            if meta["kind"] == "scalar":
                size = elem_size
            else:
                size = elem_size * meta["count"]
                elem_for_array = elem_size
        elif isinstance(t, c_ast.ArrayDecl):
            elem_for_array = self.type_size(t.type)
            size = elem_for_array * self.array_count(t, node.init)
        elif isinstance(t, c_ast.PtrDecl):
            size = INT_SIZE
            ptr_pointee = self.ptr_pointee_size(t)
        else:
            size = self.type_size(t)

        addr = self.alloc_var(node.name, size)
        if elem_for_array is not None:
            self.array_elem_bytes[node.name] = elem_for_array
            self.array_by_addr[addr] = elem_for_array
        if ptr_pointee is not None:
            self.ptr_elem[addr] = ptr_pointee
        if node.init is None:
            return
        after_alloc = self.next_addr  # temporaries used by the initializer are freed afterwards
        if elem_for_array is not None:
            self.gen_array_init(node, addr, elem_for_array, size // elem_for_array)
            self.next_addr = after_alloc
            return
        if ptr_pointee is not None:
            self.check_not_rom(node.init)
        self.emit_copy(self.gen_expr(node.init), addr)
        self.next_addr = after_alloc

    def gen_loop_test(self, cond, end_label):
        value = 1 if cond is None else self.try_const(cond)
        if value is None:
            bool_addr = self.gen_cond(cond)
            self.emit_load(bool_addr, 1, RA)
            self.emit("ifnotreg", RA, end_label)
        elif value == 0:
            self.emit("setpc", end_label)

    # -- statements --------------------------------------------------------------
    def gen_stmt(self, node):
        """Compiles one statement, then frees the RAM its temporaries and block
        locals used (they're dead once the statement ends), so RAM use is the
        peak of any single statement rather than the sum of all of them.
        Declarations keep their variables until the enclosing block ends."""
        if node is None:
            return
        if isinstance(node, (c_ast.Decl, c_ast.DeclList, c_ast.Typedef, c_ast.EmptyStatement)):
            self._gen_stmt(node)
            return
        mark = self.next_addr
        self._gen_stmt(node)
        self.next_addr = mark

    def _gen_stmt(self, node):
        if node is None:
            return
        if getattr(node, "coord", None):
            self.cur_coord = node.coord

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
            self.gen_decl(node)
            return

        if isinstance(node, c_ast.Typedef):
            self.register_typedef(node)
            return

        if isinstance(node, c_ast.EmptyStatement):
            return

        if isinstance(node, c_ast.DeclList):
            for d in node.decls:
                self.gen_stmt(d)
            return

        if isinstance(node, c_ast.For):
            saved_vars, saved_sizes = dict(self.vars), dict(self.var_sizes)  # C99: loop vars are loop-scoped
            if node.init:
                self.gen_stmt(node.init)
            label_start = Label("forloop")
            label_cont = Label("forcontinue")
            label_end = Label("endforloop")
            self.define_label(label_start)
            self.gen_loop_test(node.cond, label_end)
            self.break_stack.append(label_end)
            self.continue_stack.append(label_cont)
            self.gen_stmt(node.stmt)
            self.break_stack.pop()
            self.continue_stack.pop()
            self.define_label(label_cont)
            if node.next:
                self.gen_stmt(node.next)
            self.emit("setpc", label_start)
            self.define_label(label_end)
            self.vars, self.var_sizes = saved_vars, saved_sizes
            return

        if isinstance(node, c_ast.ExprList):
            for item in node.exprs:
                self.gen_stmt(item)
            return

        if isinstance(node, c_ast.UnaryOp) and node.op in ("p++", "++", "p--", "--"):
            self.gen_incdec(node, want_value=False)
            return

        if isinstance(node, (c_ast.Assignment, c_ast.UnaryOp, c_ast.Cast, c_ast.TernaryOp,
                             c_ast.BinaryOp, c_ast.ID, c_ast.Constant)):
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
                self.emit("flag", self.const_value(args[0]), self.const_value(args[1]))
            elif name == "setpixel":
                self.gen_setpixel(node)
            elif name == "sleep":
                args = node.args.exprs if node.args else []
                if len(args) != 1 or self.try_const(args[0]) is None:
                    raise NotImplementedError("sleep(ms) takes exactly 1 constant argument")
                self.emit("sleep", self.const_value(args[0]))
            elif name == "refreshscreen":
                self.emit("refreshscreen")
            elif name == "puts":
                args = node.args.exprs if node.args else []
                if len(args) != 1:
                    raise NotImplementedError("puts(s) takes exactly one argument")
                self.gen_print_str(args[0], newline=True)
            elif name == "putchar":
                args = node.args.exprs if node.args else []
                if len(args) != 1:
                    raise NotImplementedError("putchar(c) takes exactly one argument")
                self.gen_print_char(args[0])
            elif name == "srand":
                self.gen_srand(node)
            elif name == "drawchar":
                self.gen_drawchar(node)
            elif name == "drawstring":
                self.gen_drawstring(node)
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
            cond_value = self.try_const(node.cond)
            if cond_value is not None:  # constant condition: compile only the branch that can run
                self.gen_stmt(node.iftrue if cond_value else node.iffalse)
                return
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
            self.gen_loop_test(node.cond, label_end)
            self.break_stack.append(label_end)
            self.continue_stack.append(label_start)
            self.gen_stmt(node.stmt)
            self.break_stack.pop()
            self.continue_stack.pop()
            self.emit("setpc", label_start)
            self.define_label(label_end)
            return

        if isinstance(node, c_ast.DoWhile):
            label_start = Label("dowhile")
            label_cont = Label("docontinue")
            label_end = Label("enddowhile")
            self.define_label(label_start)
            self.break_stack.append(label_end)
            self.continue_stack.append(label_cont)
            self.gen_stmt(node.stmt)
            self.break_stack.pop()
            self.continue_stack.pop()
            self.define_label(label_cont)
            value = self.try_const(node.cond)
            if value is None:
                bool_addr = self.gen_cond(node.cond)
                self.emit_load(bool_addr, 1, RA)
                self.emit("ifreg", RA, label_start)
            elif value:
                self.emit("setpc", label_start)
            self.define_label(label_end)
            return

        if isinstance(node, c_ast.Break):
            if not self.break_stack:
                raise NotImplementedError("'break' outside a loop or switch")
            self.emit("setpc", self.break_stack[-1])
            return

        if isinstance(node, c_ast.Continue):
            if not self.continue_stack:
                raise NotImplementedError("'continue' outside a loop")
            self.emit("setpc", self.continue_stack[-1])
            return

        if isinstance(node, c_ast.Switch):
            saved_vars, saved_sizes = dict(self.vars), dict(self.var_sizes)
            cond = self.gen_expr(node.cond)
            items = (node.stmt.block_items or []) if isinstance(node.stmt, c_ast.Compound) else [node.stmt]
            end_label = Label("endswitch")
            default_label = None
            labels = []
            self.emit_load(cond, self.addr_size(cond), RA)
            for item in items:
                if isinstance(item, c_ast.Case):
                    lbl = Label("case")
                    self.emit_set_const(RB, self.const_value(item.expr, "case label"))
                    self.emit("eqreg", RA, RB, RB)
                    self.emit("ifreg", RB, lbl)
                elif isinstance(item, c_ast.Default):
                    lbl = default_label = Label("default")
                else:
                    raise NotImplementedError("statements before the first case label in a switch are not supported")
                labels.append(lbl)
            self.emit("setpc", default_label or end_label)
            self.break_stack.append(end_label)
            for item, lbl in zip(items, labels):
                self.define_label(lbl)
                for stmt in item.stmts or []:
                    self.gen_stmt(stmt)
            self.break_stack.pop()
            self.define_label(end_label)
            self.vars, self.var_sizes = saved_vars, saved_sizes
            return

        if isinstance(node, c_ast.Return):
            if self.return_stack:
                ret_addr, end_label = self.return_stack[-1]
                if node.expr is not None:
                    rhs = self.gen_expr(node.expr)
                    self.emit_copy(rhs, ret_addr)
                self.emit("setpc", end_label)
            else:
                self.emit("exit")  # `return` inside main() ends the program
            return

        raise NotImplementedError(f"unsupported statement '{type(node).__name__}'")

    # -- entry point ---------------------------------------------------------
    def compile(self, source, filename="<input>", include_dirs=(), defines=None):
        source = Preprocessor(include_dirs, defines).process(source, filename)
        clean_source, self.alloc_meta = self.rewrite_alloc_syntax(source)
        clean_source = self.rewrite_mem_syntax(clean_source)
        clean_source = self.rewrite_cpp_tag_typedefs(clean_source)
        builtin_names = [n for n in TYPE_BYTES if n != "char"] + list(TYPE_ALIASES)
        builtin_typedefs = "\n".join(f"typedef unsigned char {name};" for name in builtin_names)
        clean_source = builtin_typedefs + "\n" + clean_source
        ast = pycparser.CParser().parse(clean_source, filename=filename)

        for n in ast.ext:
            if isinstance(n, c_ast.Typedef):
                self.register_typedef(n)

        func_defs = [n for n in ast.ext if isinstance(n, c_ast.FuncDef)]
        main_fn = next((f for f in func_defs if f.decl.name == "main"), None)
        other_fns = [f for f in func_defs if f.decl.name != "main"]
        if main_fn is None:
            raise NotImplementedError("no main() function found")

        self.function_return_size(main_fn)

        for fn in other_fns:
            self.function_asts[fn.decl.name] = fn

        self.gen_prologue()
        self.at_global_scope = True  # globals start zeroed, so zero-fill is skipped
        for n in ast.ext:
            if isinstance(n, c_ast.Decl):
                self.gen_stmt(n)
        self.at_global_scope = False
        self.gen_stmt(main_fn.body)
        self.emit("exit")

        # Now that we know the program's total memory usage, patch the
        # prologue's placeholder "required bytes" value. Graphics/keyboard no
        # longer share the addressable memory space (they have their own
        # dedicated VM-side buffers), so only the program's own variables count.
        required_bytes = self.max_addr
        pos = self.required_bytes_patch_pos
        self.buf[pos:pos + 4] = required_bytes.to_bytes(4, "big")

        return self.finalize()


USAGE = "Usage: python compiler.py input.c|input_dir [input.c|input_dir ...] [-o output.ulc] [-I dir]... [-D NAME[=value]]..."


def _collect_sources(paths):
    seen = set()
    sources = []
    for path in paths:
        if os.path.isfile(path):
            entries = [path]
        elif os.path.isdir(path):
            entries = sorted(
                os.path.join(path, name)
                for name in os.listdir(path)
                if name.lower().endswith((".c", ".cpp"))
            )
            if not entries:
                raise FileNotFoundError(f"{path}: no .c/.cpp sources found")
        else:
            raise FileNotFoundError(path)
        for entry in entries:
            real = os.path.abspath(entry)
            if real not in seen:
                seen.add(real)
                sources.append(real)
    if not sources:
        raise FileNotFoundError("no source files were specified")
    return sources


def _compile_sources(sources, include_dirs, defines):
    pp = Preprocessor(include_dirs, defines)
    parts = []
    for src in sources:
        with open(src, "r", encoding="utf-8-sig") as f:
            text = f.read()
        parts.append(pp.process(text, src))
    return "\n".join(parts)


def main():
    args = sys.argv[1:]
    input_paths = []
    out_path = None
    include_dirs = [os.path.join(os.path.dirname(os.path.abspath(__file__)), "include")]
    defines = {}
    i = 0
    while i < len(args):
        a = args[i]
        if a[:2] in ("-o", "-I", "-D"):
            flag, value = a[:2], a[2:]
            if not value:
                i += 1
                if i >= len(args):
                    print(USAGE)
                    sys.exit(1)
                value = args[i]
            if flag == "-o":
                out_path = value
            elif flag == "-I":
                include_dirs.append(value)
            else:
                name, sep, val = value.partition("=")
                defines[name] = val if sep else "1"
        else:
            input_paths.append(a)
        i += 1
    if not input_paths:
        print(USAGE)
        sys.exit(1)
    try:
        sources = _collect_sources(input_paths)
        label = sources[0]
        source = _compile_sources(sources, include_dirs, defines)
    except (FileNotFoundError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    if out_path is None:
        if len(input_paths) == 1 and os.path.isdir(input_paths[0]):
            out_path = os.path.join(input_paths[0], os.path.basename(os.path.normpath(input_paths[0])) + ".ulc")
        elif len(sources) == 1:
            out_path = os.path.splitext(sources[0])[0] + ".ulc"
        else:
            out_path = os.path.join(os.getcwd(), "combined.ulc")

    compiler = Compiler()
    try:
        bytecode = compiler.compile(source, label, include_dirs, defines)
    except (PreprocessorError, NotImplementedError, pycparser.c_parser.ParseError) as e:
        where = f"{compiler.cur_coord}: " if compiler.cur_coord else ""
        print(f"{where}error: {e}", file=sys.stderr)
        sys.exit(1)

    with open(out_path, "wb") as f:
        f.write(bytecode)

    print(f"Compiled {', '.join(input_paths)} -> {out_path} ({len(bytecode)} bytes)")


if __name__ == "__main__":
    main()