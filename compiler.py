'''
uniClang -> unilang bytecode (.ulc) compiler by AzizBgBoss
https://github.com/AzizBgBoss/unilang

Source files are plain C (.c); compiled bytecode uses .ulc.
Requires pycparser:  pip install pycparser

See README.md for full documentation of the supported C subset and the .ulc bytecode format.

C-closeness summary (details in README.md):
  - a real preprocessor (preprocessor.py): #include, #define/#undef, #if/#ifdef/
    #elif/#else/#endif, #pragma once, -I and -D command line options
  - standard type names (signed/unsigned integers, uint8_t..uint32_t, size_t,
    float/double, _Bool, void), user typedefs, enums, const scalars
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
  - Integer arithmetic uses scratch registers r14/r15; floating-point
    operations use address registers r13-r15. Registers are never allocated
    to persist across statements, since this compiler's model (inline
    everything, no real stack) doesn't need that yet.
  - Pointer dereferencing and array-element access with a runtime index now
    compile to `loadregi*/storeregi*` (address held in a register), replacing
    the old `setmem` double-indirection opcode.
  - `romread(pos, size)`'s `size` argument is now a BYTE count (1, 2, 4, or 8),
    not a bit count -- same for __mem_load/__mem_store's size argument.
  - `alloc <N> name;` still takes N in bits for source-compatibility, but now
    rounds up to ceil(N/8) bytes -- `alloc 4 x;` and `alloc 8 x;` both now
    allocate a single byte.
'''

import os
import re
import struct
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
    "floatop":         (0x21, [1, 1, 1, 1, 1]),       # op, format, reg_left_addr, reg_right_addr, reg_dest_addr
    "floatcast":       (0x22, [1, 1, 1, 1]),          # src_format, dst_format, reg_src_addr, reg_dst_addr
    "romread64":       (0x23, [1, 1, 1]),             # reg_addr, reg_hi, reg_lo
    "fillrect":        (0x24, [1, 1, 1, 1, 1]),       # regx, regy, regw, regh, regc
    "drawline":        (0x25, [1, 1, 1, 1, 1]),       # regx0, regy0, regx1, regy1, regc
    "blit":            (0x26, [4]),                   # desc_addr (11 words in RAM, see ulvm_blit)
    "setpalette":      (0x27, [1, 1]),                # regindex, regcolor (RGB555), palette graphics modes only
    "save":            (0x28, [1, 1, 1]),              # regaddr, reglen, regresult
    "load":            (0x29, [1, 1, 1]),              # regaddr, reglen, regresult
    "beep":            (0x2A, [1, 1]),                 # regfreq, regms
    "audio_command":   (0x2B, [1, 1, 1, 1]),           # command, rega, regb, regresult
    "audio_load":      (0x2C, [1, 1, 1, 1, 1, 1]),     # kind, index, regaddr, reglen, source, regresult
    "net_open":        (0x90, [1, 1, 1, 1, 1, 1]),     # regkind, reglocal, regremote, reghostaddr, regflags, regresult
    "net_status":      (0x91, [1, 1]),
    "net_send":        (0x92, [1, 1, 1, 1]),
    "net_recv":        (0x93, [1, 1, 1, 1]),
    "net_accept":      (0x94, [1, 1]),
    "net_close":       (0x95, [1, 1]),
    "net_option":      (0x96, [1, 1, 1, 1]),
    "net_avail":       (0x97, [1, 1]),
    "net_localip":     (0x98, [1, 1]),
    "net_peer":        (0x99, [1, 1, 1, 1]),
    "net_lasterror":   (0x9A, [1]),
    "net_http":        (0x9B, [1, 1, 1, 1, 1, 1, 1]),
    "net_http_status": (0x9C, [1, 1]),
    "net_discovery":   (0x9D, [1, 1, 1, 1, 1]),
    "net_discovery_count": (0x9E, [1]),
    "net_discovery_get": (0x9F, [1, 1, 1, 1, 1, 1]),
    "net_localport": (0xA0, [1, 1]),
    "memcpy":          (0x2D, [1, 1, 1, 1]),           # regdst, regsrc, reglen, source (0=RAM,1=ROM)
    "memset":          (0x2E, [1, 1, 1]),              # regdst, regvalue, reglen
    "fillscreen":      (0x2F, [1]),                    # regcolor
    "fillcircle":      (0x31, [1, 1, 1, 1]),           # regx, regy, regradius, regcolor
    "drawrect":        (0x32, [1, 1, 1, 1, 1]),        # regx, regy, regw, regh, regcolor
    "jmpreg":          (0x37, [1]),                    # regtarget
    "callreg":         (0x38, [1]),                    # regtarget
    "pushr":           (0x39, [1]),                    # regsrc
    "popr":            (0x3A, [1]),                    # regdst
    "outfmt":          (0x3B, [4, 1, 1, 1, 1, 1]),     # addr, numeric format, conversion, width, precision, flags
    "minreg":          (0x33, [1, 1, 1, 1]),           # rleft, rright, rdst, signed
    "maxreg":          (0x34, [1, 1, 1, 1]),           # rleft, rright, rdst, signed
    "absreg":          (0x35, [1, 1]),                 # rsrc, rdst
    "mul64":           (0x36, [1, 1, 1, 1, 1]),        # rleft, rright, rhigh, rlow, signed

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
    "sdivreg":         (0x6D, [1, 1, 1]),
    "smodreg":         (0x6E, [1, 1, 1]),
    "sshrreg":         (0x6F, [1, 1, 1]),
    "outregsigned":    (0x70, [1]),
    "sltreg":          (0x71, [1, 1, 1]),             # signed compares
    "sgtreg":          (0x72, [1, 1, 1]),
    "slereg":          (0x73, [1, 1, 1]),
    "sgereg":          (0x74, [1, 1, 1]),
    "andregv":         (0x75, [1, 4, 1]),             # r1, const, dst
    "orregv":          (0x76, [1, 4, 1]),
    "xorregv":         (0x77, [1, 4, 1]),
    "shlregv":         (0x78, [1, 1, 1]),             # r1, imm8, dst
    "shrregv":         (0x79, [1, 1, 1]),
    "sshrregv":        (0x7A, [1, 1, 1]),
    "cmpregv":         (0x7B, [1, 4, 1, 1]),          # r1, const, dst, cond
    "jcmp":            (0x7C, [1, 1, 1, 4]),          # r1, r2, cond, pos
    "jcmpv":           (0x7D, [1, 4, 1, 4]),          # r1, const, cond, pos

    "exit":            (0xFF, []),
}

# comparison -> fused/immediate compare condition code (+4 selects the signed variant)
COND_CODES = {"==": 0, "!=": 1, "<": 2, ">": 3, "<=": 4, ">=": 5}
COND_INVERT = {0: 1, 1: 0, 2: 5, 3: 4, 4: 3, 5: 2, 6: 9, 7: 8, 8: 7, 9: 6}

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
# Signed integers use two's-complement storage and explicit signed operations;
# float/double values use IEEE-754 binary32/binary64 bit layouts.
TYPE_ALIASES = {
    "uint8_t": (1, False), "uint16_t": (2, False), "uint32_t": (4, False),
    "int8_t": (1, True), "int16_t": (2, True), "int32_t": (4, True),
    "size_t": (4, False), "uintptr_t": (4, False),
}

M32 = 0xFFFFFFFF

# integer numeric formats (see floatcast) -> storage bytes; signed ones
INT_FMT_SIZE = {1: 1, 2: 1, 3: 2, 4: 2, 5: 4, 6: 4}
SIGNED_FMTS = (1, 3, 5)


class _NotConst(Exception):
    """Raised internally when an expression isn't a compile-time constant."""


class Label:
    """A jump target resolved once its position in the bytecode is known."""
    def __init__(self, name="L"):
        self.name = name
        self.addr = None


class Slot:
    """One RAM allocation (variable, parameter, return slot or temporary).

    `start`/`end` are indices into Compiler.code: the slot exists from the
    instruction emitted right after it was allocated until the point where
    the compiler freed its address again (end=None: never freed). Every
    instruction that can touch the slot lies inside that range, because
    temporaries never outlive the statement that created them and block
    locals never outlive their block. The optimizer relies on this."""
    __slots__ = ("id", "addr", "size", "start", "end", "name", "permanent")

    def __init__(self, sid, addr, size, start, name):
        self.id = sid
        self.addr = addr
        self.size = size
        self.start = start
        self.end = None
        self.name = name
        self.permanent = False


class Addr(int):
    """A RAM address that remembers which Slot (and offset inside it) it
    designates. Adding/subtracting a plain integer keeps the tag, so every
    address that reaches an instruction operand can be traced back to its
    variable. That is what lets the optimizer move variables to other
    addresses or into registers safely."""
    # (no __slots__: not supported on int subclasses)

    def __new__(cls, value, slot, off=0):
        obj = int.__new__(cls, value)
        obj.slot = slot
        obj.off = off
        return obj

    def __add__(self, other):
        if isinstance(other, int) and not isinstance(other, (Addr, bool)):
            return Addr(int(self) + other, self.slot, self.off + other)
        return int(self) + int(other)

    __radd__ = __add__

    def __sub__(self, other):
        if isinstance(other, int) and not isinstance(other, (Addr, bool)):
            return Addr(int(self) - other, self.slot, self.off - other)
        return int(self) - int(other)

    def __rsub__(self, other):
        return int(other) - int(self)

    def __repr__(self):
        return f"@{int(self)}({self.slot.name}+{self.off})"


class RomRef:
    """Placeholder for a ROM byte offset that is only known once all code
    has been laid out (const arrays and string literals live after the code)."""
    __slots__ = ("name",)

    def __init__(self, name):
        self.name = name


class Ins:
    """One instruction (or a label marker when op == 'LABEL')."""
    __slots__ = ("op", "args")

    def __init__(self, op, args):
        self.op = op
        self.args = args

    def __repr__(self):
        return f"{self.op} {', '.join(repr(a) if not isinstance(a, Label) else a.name for a in self.args)}"


class Compiler:
    NETWORK_BUILTINS = {
        "netopen": ("net_open", 5, (0, 1, 2, 3, 4)),
        "netstatus": ("net_status", 1, (0,)),
        "netsend": ("net_send", 3, (0, 1, 2)),
        "netrecv": ("net_recv", 3, (0, 1, 2)),
        "netaccept": ("net_accept", 1, (0,)),
        "netclose": ("net_close", 1, (0,)),
        "netoption": ("net_option", 3, (0, 1, 2)),
        "netavail": ("net_avail", 1, (0,)),
        "netlocalip": ("net_localip", 1, (0,)),
        "netpeer": ("net_peer", 3, (0, 1, 2)),
        "netlasterror": ("net_lasterror", 0, ()),
        "nethttp": ("net_http", 6, (0, 1, 2, 3, 4, 5)),
        "nethttpstatus": ("net_http_status", 1, (0,)),
        "netdiscovery": ("net_discovery", 4, (0, 1, 2, 3)),
        "netdiscoverycount": ("net_discovery_count", 0, ()),
        "netdiscoveryget": ("net_discovery_get", 5, (0, 1, 2, 3, 4)),
        "netlocalport": ("net_localport", 1, (0,)),
    }

    def __init__(self, optimize=True):
        self.optimize = optimize
        self.code = []             # list of Ins; encoded to bytes by finalize()
        self.slots = []            # every Slot ever allocated, in allocation order
        self.live_slots = []       # currently allocated slots (a stack, by address)
        self._next_addr = VARS_START
        self.raw_memory = False    # program addresses RAM by number (MEMORY[...], int->pointer casts)
        self.vars = {}              # name -> byte address (current scope view)
        self.var_sizes = {}         # name -> byte width (current scope view)
        self.addr_sizes = {}        # allocated address -> byte width
        self.addr_formats = {}      # allocated scalar address -> numeric format
        self.alloc_meta = {}        # name -> {"kind": "scalar"|"array", "bits":..., "count":...}
        self.next_temp = 0
        self.function_asts = {}     # name -> FuncDef node
        self.inlining_stack = []    # names currently being inlined (recursion guard)
        self.return_stack = []      # (ret_addr, end_label) for the innermost inlined call
        self.next_instance = 0
        self.uses_indirect_jump = False

        self.runtime_memsize_addr = None   # set by gen_prologue(); holds getmemsize()'s result
        self.uses_graphics = False         # set True by flag()/setpixel()/getkey()
        self.required_bytes_ins = None  # prologue setreg patched once we know final memory usage
        self.array_elem_bytes = {}   # array variable name -> element width in bytes

        self.const_arrays = {}       # name -> {base_addr, elem_bytes, count}
        self.const_array_types = {}  # name -> declared ArrayDecl type, for nested ROM indexing
        self.rom_data = []           # {"name", "data", "relocations"}

        self.typedefs = {}           # user typedef name -> byte size
        self.typedef_formats = {}    # user scalar typedef name -> numeric format
        self.structs = {}            # struct tag or typedef name -> {"size": n, "fields": {name: {"offset": o, "size": s}}}
        self.struct_defs = {}        # struct tag or typedef name -> original Struct AST node
        self.constants = {
            "ULVM_FLAG_NETWORK": 15,
            "NET_TCP": 1, "NET_UDP": 2, "NET_HTTP": 3,
            "NET_CLOSED": 0, "NET_LISTENING": 1, "NET_CONNECTING": 2,
            "NET_CONNECTED": 3, "NET_READY": 4,
            "NET_OPEN_LISTEN": 1, "NET_OPEN_BROADCAST": 2,
            "NET_HTTP_GET": 1, "NET_HTTP_POST": 2,
            "NET_DISCOVERY_LISTEN": 1, "NET_DISCOVERY_ANNOUNCE": 2,
            "NET_DISCOVERY_PORT": 45678,
            "NET_ERR_NONE": 0, "NET_ERR_INVALID_HANDLE": 1,
            "NET_ERR_BAD_KIND": 2, "NET_ERR_NO_BUFFER": 3,
            "NET_ERR_TIMEOUT": 4, "NET_ERR_IO": 5,
            "NET_ERR_UNSUPPORTED": 6, "NET_ERR_LIMIT": 7,
            "NET_OPT_TIMEOUT_MS": 1, "NET_OPT_TCP_NODELAY": 2,
            "NET_OPT_FRAME_SIZE": 3, "NET_OPT_HTTP_HEADERS": 4,
            "NET_OPT_UDP_BROADCAST": 5,
        }          # shared builtin constants plus enum constants and const scalars
        self.ptr_elem = {}           # address of a pointer variable -> pointee byte size
        self.array_by_addr = {}      # base address of a RAM array -> element byte size
        self.var_types = {}          # variable name -> declared type AST node
        self.break_stack = []        # jump targets for `break`
        self.continue_stack = []     # jump targets for `continue`
        self.user_labels = []        # function-scoped C labels while compiling inlined calls
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
    @property
    def next_addr(self):
        return self._next_addr

    @next_addr.setter
    def next_addr(self, value):
        value = int(value)
        if value < self._next_addr:
            # everything allocated at or above `value` is freed here
            while self.live_slots and self.live_slots[-1].addr >= value:
                self.live_slots.pop().end = len(self.code)
        self._next_addr = value

    def alloc_var(self, name, size=INT_SIZE, fmt=None):
        slot = Slot(len(self.slots), self._next_addr, size, len(self.code), name)
        self.slots.append(slot)
        self.live_slots.append(slot)
        addr = Addr(self._next_addr, slot)
        self.constants.pop(name, None)  # a variable shadows a same-named constant
        self.next_addr += size
        if self.next_addr > self.max_addr:
            self.max_addr = self.next_addr
        # this address may have held something else before it was freed
        self.ptr_elem.pop(addr, None)
        self.array_by_addr.pop(addr, None)
        self.addr_formats.pop(addr, None)
        self.vars[name] = addr
        self.var_sizes[name] = size
        self.addr_sizes[addr] = size
        if fmt is not None:
            self.addr_formats[addr] = fmt
        return addr

    def alloc_temp(self, size=INT_SIZE, fmt=None):
        name = f"__t{self.next_temp}"
        self.next_temp += 1
        return self.alloc_var(name, size, fmt)

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

    def int_conversion(self, src, src_fmt, dest_size, dest_fmt):
        """True when converting src (format src_fmt) to an integer of
        dest_size bytes needs no floatcast: both sides are plain integers
        whose storage width matches their format."""
        return (src_fmt in INT_FMT_SIZE and dest_fmt in INT_FMT_SIZE
                and INT_FMT_SIZE[src_fmt] == self.addr_size(src)
                and INT_FMT_SIZE[dest_fmt] == dest_size)

    def emit_load_int(self, src, src_fmt, dest_size, reg):
        """Loads an integer so that storing `reg` with dest_size bytes gives
        the C conversion: truncation needs nothing (storereg truncates), and
        widening a signed value sign-extends it."""
        size = self.addr_size(src)
        self.emit_load(src, size, reg)
        if src_fmt in SIGNED_FMTS and dest_size > size:
            shift = 32 - 8 * size
            self.emit("shlregv", reg, shift, reg)
            self.emit("sshrregv", reg, shift, reg)

    def emit_copy(self, src, dest):
        src_fmt = self.addr_formats.get(src, self.format_for_size(self.addr_size(src)))
        dest_fmt = self.addr_formats.get(dest, self.format_for_size(self.addr_size(dest)))
        if src_fmt != dest_fmt and self.int_conversion(src, src_fmt, self.addr_size(dest), dest_fmt):
            # e.g. uint8_t c = b & 15: a load and a (truncating) store
            self.emit_load_int(src, src_fmt, self.addr_size(dest), RA)
            self.emit_store(RA, dest, self.addr_size(dest))
            return
        if src_fmt != dest_fmt or self.addr_size(src) == 8 or self.addr_size(dest) == 8:
            self.emit_set_const(RA, src)
            self.emit_set_const(RB, dest)
            self.emit("floatcast", src_fmt, dest_fmt, RA, RB)
            return
        # loadreg zero-extends into a full 32-bit register and storereg
        # truncates back down, so narrowing/widening between different-sized
        # variables just falls out of an ordinary load+store -- no special
        # casing needed (unlike the old bit-field right-justification model).
        self.emit_load(src, self.addr_size(src), RA)
        self.emit_store(RA, dest, self.addr_size(dest))

    @staticmethod
    def format_for_size(size):
        return {1: 2, 2: 4, 4: 6, 8: 8}.get(size, 6)

    def type_format(self, type_node):
        if self.is_void_type(type_node):
            return 6
        if isinstance(type_node, c_ast.PtrDecl):
            return 6
        if isinstance(type_node, c_ast.ArrayDecl):
            return self.type_format(type_node.type)
        if isinstance(type_node, c_ast.IdentifierType):
            type_node = c_ast.TypeDecl(declname=None, quals=[], align=None, type=type_node)
        if isinstance(type_node, c_ast.TypeDecl):
            if isinstance(type_node.type, c_ast.IdentifierType):
                words = list(type_node.type.names)
                if "float" in words:
                    return 7
                if "double" in words:
                    return 8
                for word in words:
                    if word in TYPE_ALIASES:
                        size, signed = TYPE_ALIASES[word]
                        return self.integer_format(size, signed)
                    if word in self.typedef_formats:
                        return self.typedef_formats[word]
                if "char" in words:
                    return 1 if "unsigned" not in words and "signed" in words else 2
                size = self.scalar_size(type_node)
                return self.integer_format(size, self.scalar_type_signed(type_node))
            if isinstance(type_node.type, c_ast.Enum):
                return 5
        return self.format_for_size(self.type_size(type_node)) if type_node is not None else 6

    @staticmethod
    def integer_format(size, signed):
        return {(1, True): 1, (1, False): 2, (2, True): 3,
                (2, False): 4, (4, True): 5, (4, False): 6}.get(
                    (size, signed), 6)

    def expr_format(self, node):
        float_size = self.expr_float_size(node)
        if float_size:
            return 8 if float_size == 8 else 7
        if isinstance(node, c_ast.BinaryOp) and node.op in (
                "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"):
            left_type, right_type = self.expr_type(node.left), self.expr_type(node.right)
            unsigned = ((left_type is not None and not self.scalar_type_signed(left_type))
                        or (right_type is not None and not self.scalar_type_signed(right_type)))
            return 6 if unsigned else 5
        if isinstance(node, c_ast.UnaryOp) and node.op in ("-", "~"):
            operand_type = self.expr_type(node.expr)
            if operand_type is not None and self.type_size(operand_type) >= INT_SIZE:
                return 5 if self.scalar_type_signed(operand_type) else 6
            return 5
        if isinstance(node, c_ast.BinaryOp) and node.op in (
                "==", "!=", "<", ">", "<=", ">=", "&&", "||"):
            return 2
        if isinstance(node, c_ast.UnaryOp) and node.op == "!":
            return 2
        typ = self.expr_type(node)
        return self.type_format(typ) if typ is not None else 6

    def expr_storage_size(self, node):
        fmt = self.expr_format(node)
        if fmt == 8:
            return 8
        if fmt in (5, 6, 7):
            return 4
        typ = self.expr_type(node)
        return self.type_size(typ) if typ is not None else INT_SIZE

    def expr_float_size(self, node):
        if isinstance(node, c_ast.Constant):
            return 8 if node.type == "double" else 4 if node.type == "float" else 0
        if isinstance(node, c_ast.Cast):
            target = node.to_type.type
            target_size = INT_SIZE if isinstance(target, c_ast.PtrDecl) else self.type_size(target)
            target_fmt = self.type_format(target)
            return target_size if target_fmt in (7, 8) else 0
        if isinstance(node, c_ast.UnaryOp):
            return self.expr_float_size(node.expr)
        if isinstance(node, c_ast.BinaryOp) and node.op not in (
                "==", "!=", "<", ">", "<=", ">=", "&&", "||"):
            return max(self.expr_float_size(node.left), self.expr_float_size(node.right))
        if isinstance(node, c_ast.TernaryOp):
            return max(self.expr_float_size(node.iftrue), self.expr_float_size(node.iffalse))
        typ = self.expr_type(node)
        if typ is not None:
            fmt = self.type_format(typ)
            return 8 if fmt == 8 else 4 if fmt == 7 else 0
        return 0

    def emit_floatcast(self, src, src_fmt, dest, dest_fmt):
        self.emit_set_const(RA, src)
        self.emit_set_const(RB, dest)
        self.emit("floatcast", src_fmt, dest_fmt, RA, RB)

    def gen_float_value(self, node, target_fmt=None):
        src = self.gen_expr(node)
        src_fmt = self.addr_formats.get(src, self.expr_format(node))
        fmt = target_fmt or src_fmt
        if fmt == src_fmt:
            return src
        dest = self.alloc_temp(4 if fmt == 7 else 8, fmt)
        self.emit_floatcast(src, src_fmt, dest, fmt)
        return dest

    def gen_float_binary(self, node):
        fmt = 8 if max(self.expr_float_size(node.left), self.expr_float_size(node.right)) == 8 else 7
        left = self.gen_float_value(node.left, fmt)
        right = self.gen_float_value(node.right, fmt)
        dest = self.alloc_temp(4 if fmt == 7 else 8, fmt)
        self.emit_set_const(RA, left)
        self.emit_set_const(RB, right)
        self.emit_set_const(RC, dest)
        op = {"+": 1, "-": 2, "*": 3, "/": 4}.get(node.op)
        if op is None:
            raise NotImplementedError(f"operator '{node.op}' is not supported for floating-point values")
        self.emit("floatop", op, fmt, RA, RB, RC)
        return dest

    def gen_float_compare(self, left_node, right_node, op):
        fmt = 8 if max(self.expr_float_size(left_node), self.expr_float_size(right_node)) == 8 else 7
        left = self.gen_float_value(left_node, fmt)
        right = self.gen_float_value(right_node, fmt)
        dest = self.alloc_temp(1, 2)
        self.emit_set_const(RA, left)
        self.emit_set_const(RB, right)
        self.emit_set_const(RC, dest)
        opcode = {"==": 6, "!=": 7, "<": 8, ">": 9, "<=": 10, ">=": 11}[op]
        self.emit("floatop", opcode, fmt, RA, RB, RC)
        return dest

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
        if self.expr_float_size(node):
            return self.gen_float_compare(node, c_ast.Constant(type="double" if self.expr_float_size(node) == 8 else "float", value="0.0"), "!=")
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

    @staticmethod
    def is_aggregate_node(node):
        return isinstance(node, (c_ast.Struct, c_ast.Union))

    @staticmethod
    def is_aggregate_type(type_node):
        return (isinstance(type_node, c_ast.TypeDecl)
                and Compiler.is_aggregate_node(type_node.type))

    def canonical_aggregate(self, node):
        if self.is_aggregate_node(node) and node.name is not None:
            return self.struct_defs.get(node.name, node)
        return node

    def struct_size(self, struct_node):
        if struct_node is None:
            raise NotImplementedError("incomplete aggregate type is not supported")
        name = struct_node.name
        if name in self.structs and self.structs[name]["fields"]:
            return self.structs[name]["size"]
        if struct_node.decls is None:
            raise NotImplementedError("incomplete aggregate type is not supported")
        offset = 0
        fields = {}
        for decl in struct_node.decls:
            if decl.name is None:
                continue
            size = self.struct_field_size(decl)
            field_offset = 0 if isinstance(struct_node, c_ast.Union) else offset
            fields[decl.name] = {"offset": field_offset, "size": size}
            offset = max(offset, size) if isinstance(struct_node, c_ast.Union) else offset + size
        if name is not None:
            self.structs[name] = {"size": offset, "fields": fields}
        return offset

    def struct_field_info(self, struct_type, field_name):
        if not self.is_aggregate_node(struct_type):
            raise NotImplementedError("expected a struct or union type")
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
                    if not isinstance(struct_type, c_ast.Union):
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
        if self.is_aggregate_node(t):
            return self.struct_size(t)
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
        if words == ["float"]:
            return 4
        if words == ["double"]:
            return 8
        if "float" in words or "double" in words:
            if words == ["long", "double"]:
                return 8
            raise NotImplementedError(f"unsupported floating-point type '{' '.join(words)}'")
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
            if self.is_aggregate_node(type_node.type):
                return self.struct_size(type_node.type)
            return self.scalar_size(type_node)
        if self.is_aggregate_node(type_node):
            return self.struct_size(type_node)
        raise NotImplementedError(f"unsupported type node '{type(type_node).__name__}'")

    def ptr_pointee_size(self, ptr_node):
        target = ptr_node.type
        if isinstance(target, c_ast.PtrDecl):
            return INT_SIZE  # pointer to pointer: the pointee is itself a 4-byte address
        if isinstance(target, c_ast.TypeDecl):
            if self.is_void_type(target):
                return 1  # void * is byte-addressed
            if self.is_aggregate_node(target.type):
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
            if self.is_aggregate_node(type_node.type):
                return self.canonical_aggregate(type_node.type)
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
                if self.is_aggregate_node(target_type.type):
                    struct = self.canonical_aggregate(target_type.type)
                    for decl in struct.decls or []:
                        if decl.name == field_name:
                            return decl.type
            if isinstance(base_type, c_ast.ArrayDecl):
                elem = base_type.type
                if isinstance(elem, c_ast.TypeDecl) and self.is_aggregate_node(elem.type):
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
            float_size = max(self.expr_float_size(expr.left), self.expr_float_size(expr.right))
            if float_size:
                return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                     type=c_ast.IdentifierType(names=["double" if float_size == 8 else "float"]))
            return self.expr_type(expr.left) or self.expr_type(expr.right)
        if isinstance(expr, c_ast.TernaryOp):
            float_size = max(self.expr_float_size(expr.iftrue), self.expr_float_size(expr.iffalse))
            if float_size:
                return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                     type=c_ast.IdentifierType(names=["double" if float_size == 8 else "float"]))
            return self.expr_type(expr.iftrue) or self.expr_type(expr.iffalse)
        if isinstance(expr, c_ast.FuncCall) and isinstance(expr.name, c_ast.ID):
            if expr.name.name in ("sin", "cos", "sqrt", "atan2"):
                return c_ast.TypeDecl(declname=None, quals=[], align=None,
                                     type=c_ast.IdentifierType(names=["double"]))
            fn = self.function_asts.get(expr.name.name)
            if fn is not None:
                return fn.decl.type.type
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
        if len(words) == 1 and words[0] in self.typedef_formats:
            return self.typedef_formats[words[0]] in (1, 3, 5)
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
            shift = 32 - size * 8   # sign-extend: two shifts, no scratch register
            self.emit("shlregv", reg, shift, reg)
            self.emit("sshrregv", reg, shift, reg)

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
                if isinstance(target, c_ast.TypeDecl) and self.is_aggregate_node(target.type):
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
            if isinstance(base_type, c_ast.TypeDecl) and self.is_aggregate_node(base_type.type):
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
            # pycparser keeps the base expression in `.name` (not `.expr`)
            return self.sizeof_value(node)
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
        # ROM offset, resolved in finalize() once the code size is known
        self.emit("setreg", RA, RomRef(name))
        self.emit_store(RA, base, INT_SIZE)
        self.const_arrays[name] = {"base": base, "elem_bytes": elem_bytes, "count": len(values)}
        if type_node is not None:
            self.const_array_types[name] = type_node
        self.array_elem_bytes[name] = elem_bytes
        self.rom_data.append({
            "name": name,
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
        _, sizes = OPS[op]
        if len(operands) != len(sizes):
            raise ValueError(f"{op} expects {len(sizes)} operands, got {len(operands)}")
        ins = Ins(op, list(operands))
        self.code.append(ins)
        return ins

    def define_label(self, label):
        self.code.append(Ins("LABEL", [label]))

    @staticmethod
    def ins_size(ins):
        if ins.op == "LABEL":
            return 0
        size = 1
        for val, width in zip(ins.args, OPS[ins.op][1]):
            size += 2 + len(val.encode("utf-8")) if width == 0 else width
        return size

    def finalize(self):
        code = [ins for ins in self.code if ins is not None]
        pos = 0
        for ins in code:
            if ins.op == "LABEL":
                ins.args[0].addr = pos
            pos += self.ins_size(ins)
        rom_offsets = {}
        byte_offset = pos
        for item in self.rom_data:
            rom_offsets[item["name"]] = byte_offset
            byte_offset += len(item["data"])
        buf = bytearray()
        for ins in code:
            if ins.op == "LABEL":
                continue
            byte, sizes = OPS[ins.op]
            buf.append(byte)
            for val, width in zip(ins.args, sizes):
                if width == 0:
                    text = val.encode("utf-8")
                    buf += len(text).to_bytes(2, "big")
                    buf += text
                    continue
                if isinstance(val, Label):
                    if val.addr is None:
                        raise RuntimeError(f"label '{val.name}' used but never defined")
                    val = val.addr
                elif isinstance(val, RomRef):
                    val = rom_offsets[val.name]
                buf += (int(val) % (2 ** (width * 8))).to_bytes(width, "big")
        for item in self.rom_data:
            data = bytearray(item["data"])
            for offset, target in item["relocations"]:
                if isinstance(target, Label):
                    if target.addr is None:
                        raise ValueError(f"ROM jump table in '{item['name']}' refers to an undefined label")
                    address = target.addr
                else:
                    if target not in rom_offsets:
                        raise ValueError(f"ROM pointer in '{item['name']}' refers to unknown array '{target}'")
                    address = rom_offsets[target]
                data[offset:offset + INT_SIZE] = address.to_bytes(INT_SIZE, "big")
            buf.extend(data)
        return bytes(buf)

    # -- startup prologue: query real memory size, warn if too small ------------
    def gen_prologue(self):
        self.runtime_memsize_addr = self.alloc_temp()
        self.emit("getmemsize", RA)
        self.emit_store(RA, self.runtime_memsize_addr, INT_SIZE)

        required_addr = self.alloc_temp()
        # placeholder; patched once compilation (and RAM layout) is finished
        self.required_bytes_ins = self.emit("setreg", RA, 0)
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
            if isinstance(base, c_ast.ID) and node.type == "->":
                # p->field: the struct lives where the pointer points
                base_type = self.resolve_type_for_expr(base)
                ptr_addr = self.gen_expr(base)
                self.emit_load(ptr_addr, self.addr_size(ptr_addr), RA)
            elif isinstance(base, c_ast.ID):
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
                if not isinstance(target, c_ast.TypeDecl) or not self.is_aggregate_node(target.type):
                    raise NotImplementedError(f"field '{field}' requires a struct/union or pointer-to-aggregate base")
                base_type = target
            if not isinstance(base_type, c_ast.TypeDecl) or not self.is_aggregate_node(base_type.type):
                raise NotImplementedError(f"field '{field}' requires a struct/union or pointer-to-aggregate base")
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
        target_type = self.resolve_type_for_expr(target)
        fmt = self.type_format(target_type)
        if fmt in (7, 8):
            size = 4 if fmt == 7 else 8
            indirect = not isinstance(target, c_ast.ID)
            if indirect:
                elem = self.emit_addr_in_ra(target)
                if elem != size:
                    raise NotImplementedError("floating-point increment requires a float or double lvalue")
                pointer = self.alloc_temp(INT_SIZE, 6)
                self.emit_store(RA, pointer, INT_SIZE)
                current = self.alloc_temp(size, fmt)
                self.emit_set_const(RB, current)
                self.emit("floatcast", fmt, fmt, RA, RB)
            else:
                current = self.var_addr(target.name)

            old = None
            if want_value and post:
                old = self.alloc_temp(size, fmt)
                self.emit_copy(current, old)
            one = self.gen_expr(c_ast.Constant(
                type="float" if fmt == 7 else "double", value="1.0"))
            result = self.alloc_temp(size, fmt)
            self.emit_set_const(RA, current)
            self.emit_set_const(RB, one)
            self.emit_set_const(RC, result)
            self.emit("floatop", 1 if increment else 2, fmt, RA, RB, RC)
            if indirect:
                self.emit_load(pointer, INT_SIZE, RA)
                self.emit_set_const(RB, result)
                self.emit("floatcast", fmt, fmt, RB, RA)
            else:
                self.emit_copy(result, current)
            return old if post else result

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
        if op in ("+", "-") and self.expr_float_size(node.expr):
            operand = self.gen_expr(node.expr)
            if op == "+":
                return operand
            fmt = self.addr_formats.get(operand, self.expr_format(node.expr))
            dest = self.alloc_temp(4 if fmt == 7 else 8, fmt)
            self.emit_set_const(RA, operand)
            self.emit_set_const(RB, operand)
            self.emit_set_const(RC, dest)
            self.emit("floatop", 5, fmt, RA, RB, RC)
            return dest
        if op == "!" and self.expr_float_size(node.expr):
            zero_type = "double" if self.expr_float_size(node.expr) == 8 else "float"
            return self.gen_float_compare(
                node.expr, c_ast.Constant(type=zero_type, value="0.0"), "==")
        if op == "&":
            target = node.expr
            if isinstance(target, c_ast.ID):
                # Must produce a temp whose VALUE is the address (so
                # emit_copy/gen_expr consumers -- which always treat a
                # gen_expr result as "a location to load from" -- see a
                # location holding the address, not the address itself
                # misread as a location).
                addr_val = self.var_addr(target.name)
                out = self.alloc_temp(INT_SIZE, 6)
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
            if elem == 8:
                fmt = self.type_format(self.resolve_type_for_expr(node))
                result = self.alloc_temp(8, fmt)
                self.emit("movreg", RC, RA)
                self.emit_set_const(RA, result)
                self.emit("floatcast", fmt, fmt, RC, RA)
                return result
            self.emit(f"loadregi{self._reg_width(elem)}", RA, RB)
            result = self.alloc_temp(elem, self.type_format(self.resolve_type_for_expr(node)))
            self.emit_store(RB, result, elem)
            return result
        if op == "+":
            return self.gen_expr(node.expr)
        if op == "-":
            operand = self.resolve_operand(node.expr)
            self.emit_set_const(RA, 0)
            self.load_comparison_operand(node.expr, operand, RB)
            self.emit("subreg", RA, RB, RA)
            dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
            self.emit_store(RA, dest, INT_SIZE)
            return dest
        if op == "~":
            operand = self.resolve_operand(node.expr)
            self.load_comparison_operand(node.expr, operand, RA)
            self.emit("notreg", RA, RA)
            dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
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
            self.load_comparison_operand(node.right, right_r, RB)
            self.emit("subreg", RA, RB, RA)
            if left_elem != 1:
                self.emit_set_const(RB, left_elem)
                self.emit("divreg", RA, RB, RA)  # distance in elements, like C
            dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
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
        if isinstance(node, (c_ast.UnaryOp, c_ast.BinaryOp, c_ast.TernaryOp, c_ast.Cast)) and not self.expr_float_size(node):
            value = self.try_const(node)  # constant folding
            if value is not None:
                size = self.expr_storage_size(node)
                dest = self.alloc_temp(size, self.expr_format(node))
                self.emit_store_const(dest, size, value)
                return dest

        if isinstance(node, c_ast.Constant):
            if node.type == "string":
                name = f"__str{len(self.rom_data)}"
                return self.add_const_array(name, TYPE_BYTES["char"], self.string_bytes(node) + [0])
            if node.type in ("float", "double"):
                fmt = 7 if node.type == "float" else 8
                size = 4 if fmt == 7 else 8
                text = node.value.rstrip("fFlL")
                value = float.fromhex(text) if text.lower().startswith("0x") else float(text)
                raw = struct.pack(">f" if fmt == 7 else ">d", value)
                dest = self.alloc_temp(size, fmt)
                self.emit_set_const(RA, int.from_bytes(raw[:4], "big"))
                self.emit_store(RA, dest, 4)
                if size == 8:
                    self.emit_set_const(RA, int.from_bytes(raw[4:], "big"))
                    self.emit_store(RA, dest + 4, 4)
                return dest
            if node.type != "char" and not node.type.endswith("int"):
                raise NotImplementedError(f"unsupported constant type '{node.type}'")
            dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
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
            if field_size == 8:
                fmt = self.type_format(field_decl.type) if field_decl is not None else 8
                result = self.alloc_temp(8, fmt)
                self.emit("movreg", RC, RA)
                self.emit_set_const(RA, result)
                self.emit("floatcast", fmt, fmt, RC, RA)
                return result
            self.emit(f"loadregi{self._reg_width(field_size)}", RA, RB)
            result = self.alloc_temp(field_size, self.type_format(field_decl.type) if field_decl is not None else None)
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
                result = self.alloc_temp(elem_bytes, self.type_format(result_type) if result_type is not None else None)
                if elem_bytes == 8:
                    self.emit("romread64", RA, RB, RC)
                    self.emit_store(RB, result, 4)
                    self.emit_store(RC, result + 4, 4)
                    return result
                self.emit(f"romread{elem_bytes * 8}", RA, RB)
                self.emit_store(RB, result, elem_bytes)
                return result

            elem_bytes = self.emit_addr_in_ra(node)
            if elem_bytes == 8:
                fmt = self.type_format(self.resolve_type_for_expr(node))
                result = self.alloc_temp(8, fmt)
                self.emit("movreg", RC, RA)
                self.emit_set_const(RA, result)
                self.emit("floatcast", fmt, fmt, RC, RA)
                return result
            self.emit(f"loadregi{self._reg_width(elem_bytes)}", RA, RB)
            result = self.alloc_temp(elem_bytes, self.type_format(self.resolve_type_for_expr(node)))
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

            if isinstance(node.lvalue, c_ast.ID) and node.lvalue.name in self.vars:
                dest = self.var_addr(node.lvalue.name)
                if self.store_int_constant(node.rvalue, dest):
                    return dest

            rhs = self.gen_expr(node.rvalue)

            if isinstance(node.lvalue, c_ast.ID):
                dest = self.var_addr(node.lvalue.name)
                self.emit_copy(rhs, dest)
                return dest

            # array element, pointer subscript or *pointer: the address goes
            # in RA only after rhs is already evaluated into a temp
            elem_bytes = self.lvalue_elem_size(node.lvalue)
            dest_type = self.resolve_type_for_expr(node.lvalue)
            dest_fmt = self.type_format(dest_type)
            src_fmt = self.addr_formats.get(rhs, self.expr_format(node.rvalue))
            if src_fmt != dest_fmt and elem_bytes != 8 and self.int_conversion(
                    rhs, src_fmt, elem_bytes, dest_fmt):
                converted = self.alloc_temp(elem_bytes, dest_fmt)
                self.emit_copy(rhs, converted)
                self.emit_addr_in_ra(node.lvalue)
                self.emit_load(converted, elem_bytes, RB)
                self.emit(f"storeregi{self._reg_width(elem_bytes)}", RA, RB)
                return converted
            if src_fmt != dest_fmt or elem_bytes == 8:
                converted = self.alloc_temp(elem_bytes, dest_fmt)
                self.emit_copy(rhs, converted)
                self.emit_addr_in_ra(node.lvalue)
                self.emit("movreg", RC, RA)
                self.emit_set_const(RA, converted)
                self.emit("floatcast", dest_fmt, dest_fmt, RA, RC)
                return converted
            self.emit_addr_in_ra(node.lvalue)
            self.emit_load(rhs, self.addr_size(rhs), RB)
            self.emit(f"storeregi{self._reg_width(elem_bytes)}", RA, RB)
            return rhs

        if isinstance(node, c_ast.TernaryOp):
            result_type = self.expr_type(node)
            result_size = self.type_size(result_type) if result_type is not None else INT_SIZE
            dest = self.alloc_temp(result_size, self.expr_format(node))
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
            fmt = self.type_format(target)
            dest = self.alloc_temp(size, fmt)
            src_fmt = self.addr_formats.get(src, self.expr_format(node.expr))
            if src_fmt != fmt and self.int_conversion(src, src_fmt, size, fmt):
                self.emit_load_int(src, src_fmt, size, RA)
                self.emit_store(RA, dest, size)
            elif src_fmt != fmt or size == 8 or self.addr_size(src) == 8:
                self.emit_floatcast(src, src_fmt, dest, fmt)
            else:
                self.emit_load(src, self.addr_size(src), RA)
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

            if self.expr_float_size(node):
                return self.gen_float_binary(node)

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
                self.load_comparison_operand(node.left, left_r, RA)
                imm = self.try_const(node.right)
                if imm is not None and node.op in ("&", "|", "^"):
                    self.emit({"&": "andregv", "|": "orregv", "^": "xorregv"}[node.op], RA, imm, RA)
                    dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
                    self.emit_store(RA, dest, INT_SIZE)
                    return dest
                if imm is not None and node.op in ("<<", ">>") and 0 <= imm < 32:
                    if node.op == "<<":
                        opn = "shlregv"
                    elif self.comparison_is_signed(node.left, node.right,
                                                   INT_SIZE if left_r[0] == "const" else left_r[2], INT_SIZE):
                        opn = "sshrregv"
                    else:
                        opn = "shrregv"
                    self.emit(opn, RA, imm, RA)
                    dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
                    self.emit_store(RA, dest, INT_SIZE)
                    return dest
                self.load_resolved(right_r, RB)
                if node.op == ">>" and self.comparison_is_signed(
                        node.left, node.right,
                        INT_SIZE if left_r[0] == "const" else left_r[2],
                        INT_SIZE if right_r[0] == "const" else right_r[2]):
                    self.emit("sshrreg", RA, RB, RA)
                else:
                    self.emit(bitwise_ops[node.op], RA, RB, RA)
                dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
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
            left_r = self.resolve_operand(node.left)
            right_r = self.resolve_operand(node.right)
            left_size = INT_SIZE if left_r[0] == "const" else left_r[2]
            right_size = INT_SIZE if right_r[0] == "const" else right_r[2]
            signed_op = self.comparison_is_signed(
                node.left, node.right, left_size, right_size)
            right_const = self.try_const(node.right)
            left_const = self.try_const(node.left)
            pow2 = (right_const is not None and node.op in ("/", "%")
                    and 0 < right_const < (1 << 31) and right_const & (right_const - 1) == 0)
            left_type = self.expr_type(node.left)
            left_nonneg = left_r[0] == "const" or not signed_op or (
                left_size < INT_SIZE and left_type is not None
                and not self.scalar_type_signed(left_type))
            if pow2 and (left_nonneg or node.op == "/"):
                k = right_const.bit_length() - 1
                self.load_comparison_operand(node.left, left_r, RA)
                if node.op == "%":
                    self.emit("andregv", RA, right_const - 1, RA)
                elif left_nonneg:
                    if k:
                        self.emit("shrregv", RA, k, RA)
                elif k:
                    # signed x / 2^k rounds toward zero: add 2^k - 1 to
                    # negative values before the arithmetic shift
                    if k == 1:
                        self.emit("shrregv", RA, 31, RB)
                    else:
                        self.emit("sshrregv", RA, 31, RB)
                        self.emit("shrregv", RB, 32 - k, RB)
                    self.emit("addreg", RA, RB, RA)
                    self.emit("sshrregv", RA, k, RA)
            elif right_const is not None and node.op in const_ops and not (
                    signed_op and left_size < INT_SIZE):
                self.load_operand(node.left, RA)
                self.emit(const_ops[node.op], RA, right_const, RA)
            elif left_const is not None and node.op in ("+", "*") and not (
                    signed_op and right_size < INT_SIZE):
                # commutative -- swap so the constant still uses the cheaper *v opcode
                self.load_operand(node.right, RA)
                self.emit(const_ops[node.op], RA, left_const, RA)
            else:
                self.load_comparison_operand(node.left, left_r, RA)
                self.load_comparison_operand(node.right, right_r, RB)
                operation = "sdivreg" if node.op == "/" and signed_op else (
                    "smodreg" if node.op == "%" and signed_op else reg_ops[node.op])
                self.emit(operation, RA, RB, RA)
            dest = self.alloc_temp(INT_SIZE, self.expr_format(node))
            self.emit_store(RA, dest, INT_SIZE)
            return dest

        if isinstance(node, c_ast.FuncCall):
            name = node.name.name
            if name == "romread":
                args = node.args.exprs if node.args else []
                if len(args) != 2 or not isinstance(args[1], c_ast.Constant):
                    raise NotImplementedError("romread(pos, size) takes a ROM byte-position expression and a constant size (1, 2, 4, or 8 bytes)")
                pos = self.gen_expr(args[0])
                size = int(args[1].value, 0)
                self.emit_load(pos, self.addr_size(pos), RA)
                if size == 8:
                    self.emit("romread64", RA, RB, RC)
                    dest = self.alloc_temp(8, 8)
                    self.emit_store(RB, dest, 4)
                    self.emit_store(RC, dest + 4, 4)
                    return dest
                self.emit(f"romread{self._reg_width(size)}", RA, RB)
                dest = self.alloc_temp(size)
                self.emit_store(RB, dest, size)
                return dest
            if name == "pop":
                args = node.args.exprs if node.args else []
                if args:
                    raise NotImplementedError("pop() takes no arguments")
                self.emit("popr", RA)
                dest = self.alloc_temp(INT_SIZE)
                self.emit_store(RA, dest, INT_SIZE)
                return dest
            if name in ("sin", "cos", "sqrt", "atan2"):
                return self.gen_float_math(node, name)
            if name in ("min", "max", "abs"):
                return self.gen_min_max_abs(node, name)
            if name == "mul64":
                raise NotImplementedError("mul64(a, b, &high, &low) is a statement")
            if name in ("memcpy", "memset"):
                return self.gen_mem_builtin(node, name)
            if name == "getkey":
                return self.gen_getkey()
            if name == "getpixel":
                return self.gen_getpixel(node)
            if name == "isflagsupported":
                return self.gen_isflagsupported(node)
            if name in self.NETWORK_BUILTINS:
                return self.gen_network_builtin(node, name)
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
            if name in ("save", "load"):
                return self.gen_storage(node, name)
            if name == "audiostatus":
                return self.gen_audio_command(node, 7)
            if name == "audioload":
                return self.gen_audio_load(node)
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
            if name in ("flag", "setpixel", "exit", "srand", "drawchar", "drawstring", "fillrect", "fillscreen", "fillcircle", "drawrect", "drawline", "blit", "setpalette", "beep", "sfx", "music", "audiostop", "audiopause", "audioresume", "audiovolume", "push"):
                raise NotImplementedError(f"'{name}' does not return a value")
            return self.gen_call(node)

        raise NotImplementedError(f"unsupported expression node '{type(node).__name__}'")

    # -- conditions: returns a 1-byte boolean address ---------------------------
    def gen_branch(self, node, label, jump_if):
        """Jump to `label` when `node` is truthy (jump_if=True) or falsy (jump_if=False).
        Integer comparisons compile to one fused compare-and-jump; && || ! short-circuit."""
        if isinstance(node, c_ast.UnaryOp) and node.op == "!":
            self.gen_branch(node.expr, label, not jump_if)
            return
        if isinstance(node, c_ast.BinaryOp) and node.op in ("&&", "||"):
            both = node.op == "&&"
            if both != jump_if:       # (&& jump-if-false) or (|| jump-if-true): either side decides
                self.gen_branch(node.left, label, jump_if)
                self.gen_branch(node.right, label, jump_if)
            else:                     # (&& jump-if-true) or (|| jump-if-false): need both to agree
                skip = Label("skip")
                self.gen_branch(node.left, skip, not jump_if)
                self.gen_branch(node.right, label, jump_if)
                self.define_label(skip)
            return
        if (isinstance(node, c_ast.BinaryOp) and node.op in COND_CODES and self.try_const(node) is None
                and not (self.expr_float_size(node.left) or self.expr_float_size(node.right))):
            left_r = self.resolve_operand(node.left)
            right_r = self.resolve_operand(node.right)
            left_size = INT_SIZE if left_r[0] == "const" else left_r[2]
            right_size = INT_SIZE if right_r[0] == "const" else right_r[2]
            signed = node.op not in ("==", "!=") and self.comparison_is_signed(
                node.left, node.right, left_size, right_size)
            cond = COND_CODES[node.op] + (4 if signed else 0)
            if not jump_if:
                cond = COND_INVERT[cond]
            self.load_comparison_operand(node.left, left_r, RA)
            right_const = self.try_const(node.right)
            if right_const is not None:
                self.emit("jcmpv", RA, right_const, cond, label)
            else:
                self.load_comparison_operand(node.right, right_r, RB)
                self.emit("jcmp", RA, RB, cond, label)
            return
        bool_addr = self.gen_cond(node)
        self.emit_load(bool_addr, 1, RA)
        self.emit("ifreg" if jump_if else "ifnotreg", RA, label)

    def gen_cond(self, node):
        comparisons = ("==", "!=", "<", ">", "<=", ">=")
        opmap = {"==": "eqreg", "!=": "neqreg", "<": "ltreg", ">": "gtreg", "<=": "lereg", ">=": "gereg"}
        if isinstance(node, c_ast.BinaryOp) and node.op in comparisons:
            if self.expr_float_size(node.left) or self.expr_float_size(node.right):
                return self.gen_float_compare(node.left, node.right, node.op)
            left_r = self.resolve_operand(node.left)
            right_r = self.resolve_operand(node.right)
            left_size = INT_SIZE if left_r[0] == "const" else left_r[2]
            right_size = INT_SIZE if right_r[0] == "const" else right_r[2]
            signed = node.op not in ("==", "!=") and self.comparison_is_signed(
                node.left, node.right, left_size, right_size)
            self.load_comparison_operand(node.left, left_r, RA)
            right_const = self.try_const(node.right)
            if right_const is not None:
                self.emit("cmpregv", RA, right_const, RA, COND_CODES[node.op] + (4 if signed else 0))
            else:
                self.load_comparison_operand(node.right, right_r, RB)
                if signed:
                    self.emit({"<": "sltreg", ">": "sgtreg", "<=": "slereg", ">=": "sgereg"}[node.op], RA, RB, RA)
                else:
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

        pieces = []  # ("text", str) or ("spec", (conversion, width, precision, flags, length))
        literal = []
        i = 0
        while i < len(fmt):
            if fmt[i] != "%":
                literal.append(fmt[i])
                i += 1
                continue
            m = re.match(
                r"%(?:%|(?P<flags>[-+ #0]*)(?P<width>\d{0,2})"
                r"(?:\.(?P<precision>\d{0,2}))?(?P<length>hh|h|ll|l|z)?"
                r"(?P<conversion>[diuoxXfFeEgGcs]))",
                fmt[i:],
            )
            if not m:
                raise NotImplementedError(
                    f"unsupported printf format at '{fmt[i:i + 8]}' "
                    "(supported: integer/float conversions with literal width and precision, %c, %s, %%)")
            if m.group(0) == "%%":
                literal.append("%")
            else:
                if literal:
                    pieces.append(("text", "".join(literal)))
                    literal = []
                flags = m.group("flags") or ""
                width_text = m.group("width") or ""
                precision_text = m.group("precision")
                width = int(width_text) if width_text else 0
                precision = int(precision_text) if precision_text is not None and precision_text else (
                    0 if precision_text is not None else 255)
                length = m.group("length") or ""
                conversion = m.group("conversion")
                if width > 99 or (precision != 255 and precision > 99):
                    raise NotImplementedError("printf width and precision are limited to 99")
                if length == "ll":
                    raise NotImplementedError("64-bit printf conversions are not supported")
                if length in ("l", "z") and conversion in "diuoxX":
                    raise NotImplementedError("long and size_t printf conversions are not supported")
                if conversion == "c" and (
                        precision != 255 or any(flag != "-" for flag in flags) or length):
                    raise NotImplementedError("%c supports only an optional width and '-' flag")
                if conversion in "uoxX" and any(flag in flags for flag in "+ "):
                    raise NotImplementedError(f"%{conversion} does not accept '+' or space flags")
                if conversion in "diu" and "#" in flags:
                    raise NotImplementedError(f"%{conversion} does not accept the '#' flag")
                if conversion in "fFeEgG" and length in ("hh", "h"):
                    raise NotImplementedError("floating printf conversions do not accept h/hh modifiers")
                if conversion == "s" and (width or precision != 255 or flags):
                    raise NotImplementedError("formatted %s width, precision and flags are not supported")
                pieces.append(("spec", (conversion, width, precision, flags, length)))
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
            conversion, width, precision, flags, length = val
            if conversion in "diuoxXfFeEgG" or conversion == "c":
                addr = self.gen_expr(arg)
                numeric_format = self.expr_format(arg)
                if conversion == "c":
                    numeric_format = self.integer_format(min(self.addr_size(addr), 4), False)
                if length in ("hh", "h"):
                    size = 1 if length == "hh" else 2
                    signed = conversion in "di"
                    numeric_format = self.integer_format(size, signed)
                    if self.addr_size(addr) > size:
                        narrowed = self.alloc_temp(size, numeric_format)
                        self.emit_load(addr, self.addr_size(addr), RA)
                        self.emit_store(RA, narrowed, size)
                        addr = narrowed
                if conversion in "fFeEgG" and numeric_format not in (7, 8):
                    raise NotImplementedError(f"%{conversion} requires a float or double argument")
                if conversion not in "fFeEgG" and numeric_format in (7, 8):
                    raise NotImplementedError(f"%{conversion} requires an integer argument")
                flag_bits = sum(bit for flag, bit in (
                    ("-", 1), ("+", 2), (" ", 4), ("#", 8), ("0", 16))
                    if flag in flags)
                self.emit("outfmt", addr, numeric_format, ord(conversion), width, precision, flag_bits)
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
        val_addr = self.gen_expr(val_node)

        # only the scratch registers: r0-r12 stay free for variables
        self.emit_load(x_addr, self.addr_size(x_addr), RC)
        self.emit_load(y_addr, self.addr_size(y_addr), RA)
        self.emit_load(val_addr, self.addr_size(val_addr), RB)
        self.emit("setpixel", RC, RA, RB)

    def gen_rect_or_line(self, call, opname):
        # fillrect(x, y, w, h, c) / drawline(x0, y0, x1, y1, c): one native opcode each
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 5:
            raise NotImplementedError(f"{opname}() takes exactly 5 arguments")
        addrs = [self.gen_expr(a) for a in args]
        regs = (11, 12, RC, RA, RB)
        for reg, a in zip(regs, addrs):
            self.emit_load(a, self.addr_size(a), reg)
        self.emit(opname, *regs)

    def gen_shape(self, call, opname, argument_count):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != argument_count:
            raise NotImplementedError(f"{opname}() takes exactly {argument_count} arguments")
        addrs = [self.gen_expr(arg) for arg in args]
        regs = (11, 12, RC, RA, RB)[:argument_count]
        for reg, addr in zip(regs, addrs):
            self.emit_load(addr, self.addr_size(addr), reg)
        self.emit(opname, *regs)

    def gen_setpalette(self, call):
        # setpalette(index, rgb555): palette graphics modes (flag 0 = 3, 5, 8, 10)
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("setpalette() takes exactly 2 arguments")
        addrs = [self.gen_expr(a) for a in args]
        regs = (RA, RB)
        for reg, a in zip(regs, addrs):
            self.emit_load(a, self.addr_size(a), reg)
        self.emit("setpalette", *regs)

    def gen_blit(self, call):
        # blit(x, y, w, h, src, srcx, srcy, stride, flags, palette, transparent)
        # See ulvm_blit() in unilang.h. `src` is a const array (ROM sheet) or RAM array (flag 32);
        # `palette` is a RAM array (flag 8) or 0.
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 11:
            raise NotImplementedError("blit(x, y, w, h, src, srcx, srcy, stride, flags, palette, transparent) takes 11 arguments")
        desc = self.alloc_temp(44)
        for i, a in enumerate(args):
            if isinstance(a, c_ast.ID) and a.name in self.array_elem_bytes and a.name not in self.const_arrays \
                    and a.name in self.vars:
                self.emit_set_const(RA, self.var_addr(a.name))      # RAM array: its address
            else:
                addr = self.gen_expr(a)
                self.emit_load(addr, self.addr_size(addr), RA)
            self.emit_store(RA, desc + 4 * i, 4)
        self.emit("blit", desc)

    def gen_getpixel(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("getpixel(x, y) takes exactly 2 arguments")
        x_addr = self.gen_expr(args[0])
        y_addr = self.gen_expr(args[1])
        self.emit_load(x_addr, self.addr_size(x_addr), RA)
        self.emit_load(y_addr, self.addr_size(y_addr), RB)
        self.emit("getpixel", RA, RB, RA)
        dest = self.alloc_temp(1)
        self.emit_store(RA, dest, 1)
        return dest

    def gen_getkey(self):
        self.uses_graphics = True
        self.emit("getkeyboard", RA)
        dest = self.alloc_temp(1)
        self.emit_store(RA, dest, 1)
        return dest

    def gen_isflagsupported(self, call):
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError("isflagsupported(flag, val) takes exactly 2 arguments")
        if not isinstance(args[0], c_ast.Constant) or not isinstance(args[1], c_ast.Constant):
            raise NotImplementedError("isflagsupported(flag, val) requires constant arguments")
        flag = self.const_value(args[0])
        val = self.const_value(args[1])
        self.emit("isflagsupported", flag, val, RA)
        dest = self.alloc_temp(1)
        self.emit_store(RA, dest, 1)
        return dest

    def gen_network_builtin(self, call, name):
        # Network opcodes use register operands. Resolve every source first so
        # evaluating a later argument cannot clobber an earlier register.
        if name not in self.NETWORK_BUILTINS:
            raise NotImplementedError(f"unknown networking builtin '{name}'")
        opcode, argc, arg_regs = self.NETWORK_BUILTINS[name]
        args = call.args.exprs if call.args else []
        if len(args) != argc:
            raise NotImplementedError(f"{name}() takes exactly {argc} argument(s)")
        pointer_args = {
            "netopen": (3,), "netsend": (1,), "netrecv": (1,),
            "netlocalip": (0,), "netpeer": (1, 2),
            "nethttp": (2, 3, 4),
            "netdiscovery": (3,), "netdiscoveryget": (1, 2, 3, 4),
        }.get(name, ())
        for i, arg in enumerate(args):
            if self.expr_float_size(arg):
                raise NotImplementedError(f"{name}() does not accept floating-point arguments")
            arg_type = self.resolve_type_for_expr(arg) or self.expr_type(arg)
            arg_is_pointer = isinstance(arg, c_ast.UnaryOp) and arg.op == "&"
            arg_is_pointer = arg_is_pointer or (
                isinstance(arg, c_ast.ID) and
                (arg.name in self.array_elem_bytes or arg.name in self.const_arrays)
            )
            arg_is_pointer = arg_is_pointer or isinstance(arg_type, (c_ast.PtrDecl, c_ast.ArrayDecl))
            arg_is_pointer = arg_is_pointer or (
                isinstance(arg_type, c_ast.TypeDecl)
                and isinstance(arg_type.type, (c_ast.PtrDecl, c_ast.ArrayDecl))
            )
            if i in pointer_args:
                optional_pointer = ((name == "nethttp" and i == 4) or
                                    (name == "netopen" and i == 3) or
                                    (name == "netdiscovery" and i == 3))
                if optional_pointer and (
                    (isinstance(arg, c_ast.ID) and arg.name in ("NULL", "nullptr")) or
                    self.try_const(arg) == 0
                ):
                    continue
                if not arg_is_pointer:
                    raise NotImplementedError(f"{name}() argument {i + 1} must be a pointer or array")
                if self.expr_is_rom(arg):
                    raise NotImplementedError(f"{name}() buffer arguments must point to VM RAM")
                self.check_not_rom(arg)
            elif arg_is_pointer:
                raise NotImplementedError(f"{name}() argument {i + 1} must be an integer value")
        values = [self.gen_expr(arg) for arg in args]
        for value, reg in zip(values, arg_regs):
            self.emit_load(value, self.addr_size(value), reg)
        result_reg = 5
        operands = list(arg_regs) + [result_reg]
        if name == "netlasterror":
            operands = [result_reg]
        self.emit(opcode, *operands)
        dest = self.alloc_temp(INT_SIZE)
        self.emit_store(result_reg, dest, INT_SIZE)
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

    def gen_storage(self, call, opname):
        self.raw_memory = True
        args = call.args.exprs if call.args else []
        if len(args) != 2:
            raise NotImplementedError(f"{opname}(buffer, length) takes exactly 2 arguments")
        buffer = args[0]
        is_buffer = (isinstance(buffer, c_ast.UnaryOp) and buffer.op == "&") or \
            isinstance(buffer, c_ast.ArrayRef)
        if isinstance(buffer, c_ast.ID):
            typ = self.var_types.get(buffer.name)
            is_buffer = is_buffer or isinstance(typ, (c_ast.PtrDecl, c_ast.ArrayDecl))
        if not is_buffer:
            raise NotImplementedError(f"{opname}() requires a pointer, array, or &variable as its first argument")
        addr = self.gen_expr(buffer)
        length = self.gen_expr(args[1])
        self.emit_load(addr, self.addr_size(addr), RA)
        self.emit_load(length, self.addr_size(length), RB)
        self.emit(opname, RA, RB, RC)
        result = self.alloc_temp(1)
        self.emit_store(RC, result, 1)
        return result

    def gen_mem_builtin(self, call, opname):
        args = call.args.exprs if call.args else []
        if len(args) != 3:
            raise NotImplementedError(f"{opname}(destination, source/value, length) takes exactly 3 arguments")
        if self.expr_is_rom(args[0]):
            raise NotImplementedError(f"{opname}() destination must be writable RAM")
        addresses = [self.gen_expr(arg) for arg in args]
        regs = (RA, RB, RC)
        for reg, address in zip(regs, addresses):
            self.emit_load(address, self.addr_size(address), reg)
        if opname == "memcpy":
            source = 1 if self.expr_is_rom(args[1]) else 0
            self.emit("memcpy", RA, RB, RC, source)
        else:
            self.emit("memset", RA, RB, RC)
        return addresses[0]

    def gen_float_math(self, call, name):
        args = call.args.exprs if call.args else []
        expected = 2 if name == "atan2" else 1
        if len(args) != expected:
            raise NotImplementedError(f"{name}() takes exactly {expected} argument(s)")
        values = [self.gen_expr(arg) for arg in args]
        converted = []
        for value, arg in zip(values, args):
            fmt = self.addr_formats.get(value, self.expr_format(arg))
            dest = self.alloc_temp(8, 8)
            self.emit_set_const(RA, value)
            self.emit_set_const(RB, dest)
            self.emit("floatcast", fmt, 8, RA, RB)
            converted.append(dest)
        if len(converted) == 1:
            right = self.gen_expr(c_ast.Constant(type="double", value="0.0"))
        else:
            right = converted[1]
        result = self.alloc_temp(8, 8)
        self.emit_set_const(RA, converted[0])
        self.emit_set_const(RB, right)
        self.emit_set_const(RC, result)
        operation = {"sin": 12, "cos": 13, "sqrt": 14, "atan2": 15}[name]
        self.emit("floatop", operation, 8, RA, RB, RC)
        return result

    def gen_min_max_abs(self, call, name):
        args = call.args.exprs if call.args else []
        expected = 1 if name == "abs" else 2
        if len(args) != expected:
            raise NotImplementedError(f"{name}() takes exactly {expected} argument(s)")
        if any(self.expr_float_size(arg) for arg in args):
            raise NotImplementedError(f"{name}() supports integer arguments only")
        resolved = [self.resolve_operand(arg) for arg in args]
        signed = name == "abs"
        if name != "abs":
            left_size = INT_SIZE if resolved[0][0] == "const" else resolved[0][2]
            right_size = INT_SIZE if resolved[1][0] == "const" else resolved[1][2]
            signed = self.comparison_is_signed(args[0], args[1], left_size, right_size)
        self.load_comparison_operand(args[0], resolved[0], RA)
        result = self.alloc_temp(INT_SIZE, 5 if signed else 6)
        if name == "abs":
            self.emit("absreg", RA, RB)
        else:
            self.load_comparison_operand(args[1], resolved[1], RC)
            self.emit("minreg" if name == "min" else "maxreg",
                      RA, RC, RB, int(signed))
        self.emit_store(RB, result, INT_SIZE)
        return result

    def gen_mul64(self, call):
        args = call.args.exprs if call.args else []
        if len(args) != 4 or not (
                isinstance(args[2], c_ast.UnaryOp) and args[2].op == "&"
                and isinstance(args[3], c_ast.UnaryOp) and args[3].op == "&"):
            raise NotImplementedError("mul64(a, b, &high, &low) takes two inputs and two output addresses")
        inputs = args[:2]
        if any(self.expr_float_size(arg) for arg in inputs):
            raise NotImplementedError("mul64() requires integer inputs")
        high_ptr, low_ptr = self.gen_expr(args[2]), self.gen_expr(args[3])
        left, right = [self.resolve_operand(arg) for arg in inputs]
        left_size = INT_SIZE if left[0] == "const" else left[2]
        right_size = INT_SIZE if right[0] == "const" else right[2]
        signed = self.comparison_is_signed(inputs[0], inputs[1], left_size, right_size)
        self.load_comparison_operand(inputs[0], left, 8)
        self.load_comparison_operand(inputs[1], right, 11)
        self.emit_load(high_ptr, INT_SIZE, 9)
        self.emit_load(low_ptr, INT_SIZE, 10)
        self.emit("mul64", 8, 11, 12, 13, int(signed))
        self.emit("storeregi32", 9, 12)
        self.emit("storeregi32", 10, 13)

    def gen_audio_command(self, call, command):
        args = call.args.exprs if call.args else []
        required = 1 if command in (2, 6, 7) else 2 if command == 1 else 0
        if len(args) != required:
            labels = {1: "sfx(id, channel)", 2: "music(pattern)", 6: "audiovolume(volume)", 7: "audiostatus(channel)"}
            label = labels.get(command, "audio command")
            raise NotImplementedError(f"{label} takes exactly {required} argument(s)")
        regs = [RA, RB]
        for i, arg in enumerate(args):
            addr = self.gen_expr(arg)
            self.emit_load(addr, self.addr_size(addr), regs[i])
        if len(args) < 2:
            self.emit_set_const(RB, 0)
        self.emit("audio_command", command, RA, RB, RC)
        if command != 7:
            return None
        result = self.alloc_temp(INT_SIZE)
        self.emit_store(RC, result, INT_SIZE)
        return result

    def gen_audio_load(self, call):
        args = call.args.exprs if call.args else []
        if len(args) != 4:
            raise NotImplementedError("audioload(kind, index, data, length) takes exactly 4 arguments")
        kind_addr = self.gen_expr(args[0])
        index_addr = self.gen_expr(args[1])
        data_addr = self.gen_expr(args[2])
        length_addr = self.gen_expr(args[3])
        source = int(self.expr_is_rom(args[2]))
        if not source:
            self.raw_memory = True
        for reg, addr in zip((RA, RB, RC, 9), (kind_addr, index_addr, data_addr, length_addr)):
            self.emit_load(addr, self.addr_size(addr), reg)
        self.emit("audio_load", RA, RB, RC, 9, source, 10)
        result = self.alloc_temp(1)
        self.emit_store(10, result, 1)
        return result

    def gen_gettime(self):
        self.emit("gettime", RA)
        dest = self.alloc_temp(INT_SIZE)
        self.emit_store(RA, dest, INT_SIZE)
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
        self.emit("numtostr", RA, buf_addr, RB)
        dest = self.alloc_temp(1)
        self.emit_store(RB, dest, 1)
        return dest

    # -- text drawing (drawchar/drawstring) ------------------------------------
    # Registers 9-13 carry the inputs here (never RA/RB) since these calls are
    # always self-contained statements/expressions: every input register is
    # loaded immediately before emitting "drawstring", and the two output
    # registers (14, 15) are discarded right after -- no value needs to
    # survive across separate emit() calls the way arithmetic temps do.
    def gen_draw_common(self, x_node, y_node, addr_kind, addr_val, length, src, color_node=None):
        x_r = self.resolve_operand(x_node)
        y_r = self.resolve_operand(y_node)
        color_r = self.resolve_operand(color_node) if color_node is not None else ("const", 1)
        self.load_resolved(x_r, 9)
        self.load_resolved(y_r, 10)
        self.load_resolved(color_r, 11)
        if addr_kind == "const":
            self.emit_set_const(12, addr_val)
        else:  # "load" -- addr_val is a RAM address whose runtime VALUE is the actual address/ROM offset
            self.emit_load(addr_val, INT_SIZE, 12)
        self.emit_set_const(RC, length)
        self.emit("drawstring", 9, 10, 11, 12, RC, src, 14, 15)

    def gen_drawchar(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) not in (3, 4):
            raise NotImplementedError("drawchar(c, x, y[, color]) takes 3 or 4 arguments")
        c_node, x_node, y_node = args[:3]
        c_r = self.resolve_operand(c_node)
        self.load_resolved(c_r, RA)
        char_addr = self.alloc_temp(1)
        self.emit_store(RA, char_addr, 1)
        color_node = args[3] if len(args) == 4 else None
        self.gen_draw_common(x_node, y_node, "const", char_addr, 1, 0, color_node)

    def gen_drawstring(self, call):
        self.uses_graphics = True
        args = call.args.exprs if call.args else []
        if len(args) not in (3, 4):
            raise NotImplementedError("drawstring(str, x, y[, color]) takes 3 or 4 arguments")
        str_node, x_node, y_node = args[:3]
        color_node = args[3] if len(args) == 4 else None
        if isinstance(str_node, c_ast.Constant) and str_node.type == "string":
            # String literal -- lives in ROM (rom_data), src=1. gen_expr() on a
            # string constant allocates the const array and returns a RAM temp
            # whose runtime VALUE is the ROM byte offset (patched in finalize()).
            chars = self.string_bytes(str_node)
            rom_offset_temp = self.gen_expr(str_node)
            self.gen_draw_common(x_node, y_node, "load", rom_offset_temp, len(chars), 1, color_node)
        elif isinstance(str_node, c_ast.ID) and str_node.name in self.array_elem_bytes:
            # Plain char[] variable -- lives in RAM, src=0. Its address is a
            # compile-time constant, so it's used directly (no load needed).
            name = str_node.name
            if self.array_elem_bytes[name] != 1:
                raise NotImplementedError("drawstring() requires a char[] array")
            base_addr = self.var_addr(name)
            total_bytes = self.var_sizes[name]
            self.gen_draw_common(x_node, y_node, "const", base_addr, total_bytes, 0, color_node)
        else:
            raise NotImplementedError("drawstring() requires a string literal or a char[] variable")

    # -- functions (compiled by inlining at each call site) -----------------------
    def gen_call(self, node, allow_void=False):
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
        void_return = self.is_void_type(fn.decl.type.type)
        if void_return and not allow_void:
            raise NotImplementedError(f"void function '{name}' does not return a value")
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
            param_fmt = self.type_format(p.type)
            addr = self.alloc_var(prefix + p.name, param_size, param_fmt)
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

        ret_type = fn.decl.type.type
        ret_fmt = self.type_format(ret_type)
        ret_addr = None if void_return else self.alloc_var(
            prefix + "__ret", self.function_return_size(fn), ret_fmt)
        end_label = Label(f"end_{prefix}")
        function_labels = self.collect_user_labels(fn.body, prefix)

        self.inlining_stack.append(name)
        self.user_labels.append(function_labels)
        self.return_stack.append((ret_addr, end_label))
        saved_loops = (self.break_stack, self.continue_stack)
        self.break_stack, self.continue_stack = [], []
        self.gen_stmt(fn.body)
        self.break_stack, self.continue_stack = saved_loops
        self.return_stack.pop()
        self.user_labels.pop()
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

    @staticmethod
    def collect_user_labels(body, prefix):
        labels = {}

        def visit(node):
            if isinstance(node, c_ast.Label):
                if node.name in labels:
                    raise NotImplementedError(f"duplicate label '{node.name}'")
                labels[node.name] = Label(f"user_{prefix}_{node.name}")
            if isinstance(node, c_ast.FuncDef):
                return
            for _, child in node.children():
                visit(child)

        visit(body)
        return labels

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
        if self.is_aggregate_type(ty):
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
        self.typedef_formats[node.name] = self.type_format(ty)

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
            raise NotImplementedError("aggregate initializers must use {...}")
        values = list(init.exprs)
        if isinstance(struct_node, c_ast.Union):
            out = bytearray(self.struct_size(struct_node))
            if not values:
                return bytes(out)
            decl = next((field for field in (struct_node.decls or []) if field.name is not None), None)
            if decl is None:
                return bytes(out)
            value = values[0]
            raw = self.const_array_element_bytes(value, decl.type)
            out[:min(len(out), len(raw))] = raw[:len(out)]
            return bytes(out)

        out = bytearray()
        for decl in struct_node.decls or []:
            if decl.name is None:
                continue
            if values:
                value = values.pop(0)
            else:
                value = c_ast.Constant(type="int", value="0")
            if isinstance(decl.type, c_ast.TypeDecl) and self.is_aggregate_node(decl.type.type):
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
        size = forced_size if forced_size is not None else self.type_size(type_node)
        if size == 0:
            return b""
        fmt = self.type_format(type_node)
        if fmt in (7, 8):
            value = self.const_float_value(expr)
            return struct.pack(">f" if fmt == 7 else ">d", value)
        value = self.const_value(expr, "const initializer")
        if value < 0:
            value &= (1 << (size * 8)) - 1
        return value.to_bytes(size, "big")

    def const_float_value(self, node):
        if isinstance(node, c_ast.Constant) and node.type in ("float", "double"):
            text = node.value.rstrip("fFlL")
            return float.fromhex(text) if text.lower().startswith("0x") else float(text)
        if isinstance(node, c_ast.Constant) and node.type.endswith("int"):
            return float(self.const_value(node, "const initializer"))
        if isinstance(node, c_ast.UnaryOp) and node.op in ("+", "-"):
            value = self.const_float_value(node.expr)
            return value if node.op == "+" else -value
        if isinstance(node, c_ast.BinaryOp) and node.op in ("+", "-", "*", "/"):
            left = self.const_float_value(node.left)
            right = self.const_float_value(node.right)
            return {"+": lambda: left + right, "-": lambda: left - right,
                    "*": lambda: left * right, "/": lambda: left / right}[node.op]()
        if isinstance(node, c_ast.Cast):
            return float(self.const_value(node.expr, "const initializer"))
        raise NotImplementedError("floating-point array initializers must be constant expressions")

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
        if isinstance(type_node, c_ast.TypeDecl) and self.is_aggregate_node(type_node.type):
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
                        raw = self.const_scalar_bytes(item, type_node.type)
                        values.append(int.from_bytes(raw, "big"))
                return values
            raw = self.const_scalar_bytes(init, type_node.type)
            return [int.from_bytes(raw, "big")]
        if isinstance(type_node, c_ast.TypeDecl) and self.is_aggregate_node(type_node.type):
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
        if self.type_format(type_node) in (7, 8):
            return [int.from_bytes(self.const_scalar_bytes(init, type_node), "big")]
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
        if name in self.structs and self.structs[name]["fields"]:
            if struct_node.decls is not None:
                self.struct_defs[name] = struct_node
            return
        self.struct_defs[name] = struct_node
        if struct_node.decls is None:
            self.structs[name] = {"size": 0, "fields": {}}
            return
        fields = {}
        offset = 0
        for decl in struct_node.decls:
            if decl.name is None:
                continue
            size = self.struct_field_size(decl)
            field_offset = 0 if isinstance(struct_node, c_ast.Union) else offset
            fields[decl.name] = {"offset": field_offset, "size": size}
            offset = max(offset, size) if isinstance(struct_node, c_ast.Union) else offset + size
        self.structs[name] = {"size": offset, "fields": fields}

    def register_aggregate_definitions(self, node):
        if self.is_aggregate_node(node):
            for declaration in node.decls or []:
                self.register_aggregate_definitions(declaration)
            if node.name is not None and node.decls is not None:
                self.register_struct(node.name, node)
            return
        for _, child in node.children():
            self.register_aggregate_definitions(child)

    def gen_aggregate_init(self, init, addr, aggregate):
        if not isinstance(init, c_ast.InitList):
            raise NotImplementedError("aggregate initializers must use {...}")
        fields = [field for field in (aggregate.decls or []) if field.name is not None]
        values = list(init.exprs)
        if isinstance(aggregate, c_ast.Union):
            fields = fields[:1]
            values = values[:1]
        if len(values) > len(fields):
            raise NotImplementedError(
                f"too many initializer values for {type(aggregate).__name__.lower()} "
                f"'{aggregate.name or ''}' ({len(values)} values for {len(fields)} members)"
            )

        for field, value in zip(fields, values):
            field_addr = addr + self.struct_field_info(aggregate, field.name)["offset"]
            nested = self.resolve_struct_type(field.type)
            if nested is not None:
                self.gen_aggregate_init(value, field_addr, nested)
                continue
            if isinstance(field.type, c_ast.ArrayDecl):
                if not isinstance(value, c_ast.InitList):
                    raise NotImplementedError("array members in aggregate initializers require {...}")
                elem_size = self.type_size(field.type.type)
                for index, item in enumerate(value.exprs):
                    if field.type.dim is not None and index >= self.const_value(field.type.dim, "array size"):
                        raise NotImplementedError("too many values for aggregate array member")
                    element_addr = field_addr + index * elem_size
                    nested_elem = self.resolve_struct_type(field.type.type)
                    if nested_elem is not None:
                        self.gen_aggregate_init(item, element_addr, nested_elem)
                        continue
                    self.addr_sizes[element_addr] = elem_size
                    self.addr_formats[element_addr] = self.type_format(field.type.type)
                    if self.store_int_constant(item, element_addr):
                        continue
                    self.emit_copy(self.gen_expr(item), element_addr)
                continue

            if isinstance(value, c_ast.InitList):
                if len(value.exprs) != 1:
                    raise NotImplementedError("scalar aggregate member initializer needs one value")
                value = value.exprs[0]
            field_size = self.type_size(field.type)
            self.addr_sizes[field_addr] = field_size
            self.addr_formats[field_addr] = self.type_format(field.type)
            if self.store_int_constant(value, field_addr):
                continue
            self.emit_copy(self.gen_expr(value), field_addr)

    def gen_decl(self, node):
        t = node.type
        enum = t.type if (isinstance(t, c_ast.TypeDecl) and isinstance(t.type, c_ast.Enum)) else (
            t if isinstance(t, c_ast.Enum) else None)
        if enum is not None:
            self.register_enum(enum)
        if self.is_aggregate_type(t):
            if t.type.name is not None:
                self.register_struct(t.type.name, t.type)
        elif self.is_aggregate_node(t) and t.name is not None:
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
        if elem_for_array is None and ptr_pointee is None:
            self.addr_formats[addr] = self.type_format(t)
        if elem_for_array is not None:
            self.array_elem_bytes[node.name] = elem_for_array
            self.array_by_addr[addr] = elem_for_array
            element_fmt = self.type_format(t.type)
            for index in range(size // elem_for_array):
                self.addr_formats[addr + index * elem_for_array] = element_fmt
        if ptr_pointee is not None:
            self.ptr_elem[addr] = ptr_pointee
        if node.init is None:
            return
        after_alloc = self.next_addr  # temporaries used by the initializer are freed afterwards
        if elem_for_array is not None:
            self.gen_array_init(node, addr, elem_for_array, size // elem_for_array)
        else:
            if ptr_pointee is not None:
                self.check_not_rom(node.init)
            aggregate = self.resolve_struct_type(t)
            if aggregate is not None and isinstance(node.init, c_ast.InitList):
                if not self.at_global_scope:
                    self.emit_set_const(RA, addr)
                    self.emit_set_const(RB, 0)
                    self.emit_set_const(RC, size)
                    self.emit("memset", RA, RB, RC)
                self.gen_aggregate_init(node.init, addr, aggregate)
            elif not self.store_int_constant(node.init, addr):
                self.emit_copy(self.gen_expr(node.init), addr)
        self.free_temps_above(after_alloc)

    def store_int_constant(self, expr, dest):
        """`int i = 0;` / `x = 5;`: when an integer variable gets a
        compile-time constant, store it directly (setreg + store) instead of
        going through a temporary. Returns False when that doesn't apply."""
        fmt = self.addr_formats.get(dest)
        size = self.addr_size(dest)
        if fmt not in INT_FMT_SIZE or INT_FMT_SIZE[fmt] != size or self.expr_float_size(expr):
            return False
        value = self.try_const(expr)
        if value is None:
            return False
        self.emit_store_const(dest, size, value & ((1 << (8 * size)) - 1))
        return True

    def free_temps_above(self, mark):
        """Frees what an initializer allocated above `mark`. At global scope the
        memory itself is not handed out again: later globals rely on starting
        out zeroed, and a reused temporary would leave its value behind."""
        if self.at_global_scope:
            while self.live_slots and self.live_slots[-1].addr >= mark:
                self.live_slots.pop().end = len(self.code)
        else:
            self.next_addr = mark

    def gen_loop_test(self, cond, end_label):
        value = 1 if cond is None else self.try_const(cond)
        if value is None:
            self.gen_branch(cond, end_label, False)
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
            elif name == "push":
                args = node.args.exprs if node.args else []
                if len(args) != 1:
                    raise NotImplementedError("push(value) takes exactly one argument")
                value = self.gen_expr(args[0])
                self.emit_load(value, self.addr_size(value), RA)
                self.emit("pushr", RA)
            elif name == "setpixel":
                self.gen_setpixel(node)
            elif name in ("fillrect", "drawline"):
                self.gen_rect_or_line(node, name)
            elif name == "fillscreen":
                self.gen_shape(node, name, 1)
            elif name == "fillcircle":
                self.gen_shape(node, name, 4)
            elif name == "drawrect":
                self.gen_shape(node, name, 5)
            elif name == "blit":
                self.gen_blit(node)
            elif name == "setpalette":
                self.gen_setpalette(node)
            elif name == "mul64":
                self.gen_mul64(node)
            elif name == "beep":
                args = node.args.exprs if node.args else []
                if len(args) != 2:
                    raise NotImplementedError("beep(frequency, duration_ms) takes exactly 2 arguments")
                addrs = [self.gen_expr(arg) for arg in args]
                for reg, addr in zip((RA, RB), addrs):
                    self.emit_load(addr, self.addr_size(addr), reg)
                self.emit("beep", RA, RB)
            elif name == "sfx":
                self.gen_audio_command(node, 1)
            elif name == "music":
                self.gen_audio_command(node, 2)
            elif name == "audiostop":
                self.gen_audio_command(node, 3)
            elif name == "audiopause":
                self.gen_audio_command(node, 4)
            elif name == "audioresume":
                self.gen_audio_command(node, 5)
            elif name == "audiovolume":
                self.gen_audio_command(node, 6)
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
                if (name in self.function_asts
                        and self.is_void_type(self.function_asts[name].decl.type.type)):
                    self.gen_call(node, allow_void=True)
                else:
                    self.gen_expr(node)  # covers getkey, isflagsupported, user functions; value discarded
            return

        if isinstance(node, c_ast.If):
            cond_value = self.try_const(node.cond)
            if cond_value is not None:  # constant condition: compile only the branch that can run
                self.gen_stmt(node.iftrue if cond_value else node.iffalse)
                return
            label_else = Label("else")
            label_end = Label("endif")
            self.gen_branch(node.cond, label_else if node.iffalse else label_end, False)
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
            case_targets = {}
            for item in items:
                if isinstance(item, c_ast.Case):
                    lbl = Label("case")
                    value = self.const_value(item.expr, "case label")
                    if value in case_targets:
                        raise NotImplementedError(f"duplicate switch case value '{value}'")
                    case_targets[value] = lbl
                elif isinstance(item, c_ast.Default):
                    if default_label is not None:
                        raise NotImplementedError("switch has more than one default label")
                    lbl = default_label = Label("default")
                else:
                    raise NotImplementedError("statements before the first case label in a switch are not supported")
                labels.append(lbl)

            target_default = default_label or end_label
            case_values = list(case_targets)
            if case_values:
                min_case, max_case = min(case_values), max(case_values)
                span = max_case - min_case + 1
            else:
                span = 0
                min_case = max_case = 0
            dense = len(case_targets) >= 3 and span <= 64 and span <= len(case_targets) * 2
            self.emit_load(cond, self.addr_size(cond), RA)
            if dense:
                self.uses_indirect_jump = True
                signed = self.comparison_is_signed(
                    node.cond,
                    c_ast.Constant(type="int", value="0"),
                    self.addr_size(cond),
                    INT_SIZE,
                )
                out_of_range = Label("switch_default")
                signed_offset = 4 if signed else 0
                self.emit("jcmpv", RA, min_case, 2 + signed_offset, out_of_range)
                self.emit("jcmpv", RA, max_case, 3 + signed_offset, out_of_range)
                self.emit_set_const(RB, min_case)
                self.emit("subreg", RA, RB, RA)
                self.emit("mulregv", RA, 4, RA)
                table_name = f"__switch_table_{self.next_instance}"
                self.next_instance += 1
                self.emit("setreg", RB, RomRef(table_name))
                self.emit("addreg", RA, RB, RA)
                self.emit("romread32", RA, RB)
                self.emit("jmpreg", RB)
                self.define_label(out_of_range)
                self.emit("setpc", target_default)

                table_data = bytearray(span * INT_SIZE)
                table_relocations = []
                for case_value in range(min_case, max_case + 1):
                    target = case_targets.get(case_value, target_default)
                    table_relocations.append(((case_value - min_case) * INT_SIZE, target))
                self.rom_data.append({
                    "name": table_name,
                    "data": table_data,
                    "relocations": table_relocations,
                })
            else:
                for value, label in case_targets.items():
                    self.emit_set_const(RB, value)
                    self.emit("eqreg", RA, RB, RB)
                    self.emit("ifreg", RB, label)
                self.emit("setpc", target_default)
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
                if ret_addr is None:
                    if node.expr is not None:
                        raise NotImplementedError("void functions cannot return a value")
                elif node.expr is None:
                    raise NotImplementedError("non-void functions must return a value")
                else:
                    rhs = self.gen_expr(node.expr)
                    self.emit_copy(rhs, ret_addr)
                self.emit("setpc", end_label)
            else:
                if node.expr is not None:
                    self.gen_expr(node.expr)
                self.emit("exit")  # `return` inside main() ends the program
            return

        if isinstance(node, c_ast.Goto):
            if not self.user_labels or node.name not in self.user_labels[-1]:
                raise NotImplementedError(f"goto to unknown label '{node.name}'")
            self.emit("setpc", self.user_labels[-1][node.name])
            return

        if isinstance(node, c_ast.Label):
            if not self.user_labels or node.name not in self.user_labels[-1]:
                raise NotImplementedError(f"unknown label '{node.name}'")
            self.define_label(self.user_labels[-1][node.name])
            self.gen_stmt(node.stmt)
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
        for n in ast.ext:
            self.register_aggregate_definitions(n)

        func_defs = [n for n in ast.ext if isinstance(n, c_ast.FuncDef)]
        for node in ast.ext:
            declaration = node.decl if isinstance(node, c_ast.FuncDef) else node
            if not isinstance(declaration, c_ast.Decl) or not isinstance(declaration.type, c_ast.FuncDecl):
                continue
            params = declaration.type.args.params if declaration.type.args else []
            if any(isinstance(param, c_ast.EllipsisParam) for param in params):
                if declaration.name != "printf":
                    raise NotImplementedError(
                        f"variadic function '{declaration.name}' is not supported; "
                        "only the compiler's fixed-format printf builtin is available"
                    )
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
        self.user_labels.append(self.collect_user_labels(main_fn.body, "main"))
        self.gen_stmt(main_fn.body)
        self.user_labels.pop()
        self.emit("exit")

        # Now that we know the program's total memory usage, patch the
        # prologue's placeholder "required bytes" value. Graphics/keyboard no
        # longer share the addressable memory space (they have their own
        # dedicated VM-side buffers), so only the program's own variables count.
        self.next_addr = VARS_START  # close the slots still open (they live until the end)
        for slot in self.slots:
            if slot.end is None or slot.end >= len(self.code):
                slot.end = None
                slot.permanent = True
        required_bytes = self.max_addr
        if self.optimize and not self.uses_indirect_jump:
            from optimizer import optimize_program
            required_bytes = optimize_program(self)
        self.required_bytes_ins.args[1] = required_bytes

        return self.finalize()


USAGE = "Usage: python compiler.py input.c|input_dir [input.c|input_dir ...] [-o output.ulc] [-I dir]... [-D NAME[=value]]... [-O0]"


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
    optimize = True
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-O0", "-O1", "-O2"):
            optimize = a != "-O0"
        elif a[:2] in ("-o", "-I", "-D"):
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

    compiler = Compiler(optimize=optimize)
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
