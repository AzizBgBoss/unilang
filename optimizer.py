'''
Post-pass optimizer for compiler.py's instruction list.

The code generator is deliberately simple: every sub-expression goes through
a RAM temporary, and no value is kept in a register across two
sub-expressions. This module cleans that up after the whole program has been
generated, working on the list of `Ins` objects (before encoding).

Passes, in order:
  1. jump cleanup        jumps to the next instruction, jumps over jumps,
                         jump chains, unreachable code, unused labels
  2. store forwarding    a load from an address whose value is still in a
                         register becomes a register move (or nothing)
  3. dead stores         stores into variables nothing ever reads are dropped
  4. register promotion  variables/temporaries whose address never escapes
                         live in r0..r12 instead of RAM
  5. register cleanup    copy propagation, constant operands folded into the
                         *regv/cmpregv/jcmpv forms, retargeting the producer
                         of a moved value, dead-instruction removal
  6. RAM layout          variables that are still in RAM get packed again,
                         skipping everything that became unused

What makes 3, 4 and 6 safe is that every RAM address operand the compiler
emits is an `Addr` tagged with the variable (`Slot`) it belongs to, and each
slot knows the range of instructions it lives in. A slot "escapes" when its
address is used as a value (pointer, array base, float operand address,
printv/blit/... operand); escaped slots are only ever relocated, never
promoted or dropped. Programs that address RAM by raw number
(MEMORY[...], __mem_*, integer-to-pointer casts) skip 3, 4 and 6.

Set UNILANG_OPT_DEBUG=1 to print what each pass did.
'''

import os
import sys

# The compiler module is usually running as __main__, so its classes are
# taken from the Compiler instance's own module in optimize_program() rather
# than imported (importing would create a second, incompatible copy).
Addr = Ins = Label = RomRef = None
COND_INVERT = OPS = None

DEBUG = bool(os.environ.get("UNILANG_OPT_DEBUG"))
# comma-separated pass names to skip, for bisecting: jumps,forward,dse,promote,regs,reloc
SKIP = set(filter(None, os.environ.get("UNILANG_OPT_SKIP", "").split(",")))
LIMIT = int(os.environ.get("UNILANG_OPT_LIMIT", "-1"))   # bisecting: max forwardings

# Operand roles, one character per operand:
#   r read register     w write register     W conditional write (also a read)
#   i immediate         m direct load addr   s direct store addr
#   M other RAM address operand (the slot escapes)    L label    t text
ROLES = {
    "flag": "ii", "isflagsupported": "iiw", "setbyte": "Mi", "write": "Mt",
    "outc": "M", "out": "M", "input": "iM", "print": "t", "printn": "t",
    "printv": "iM", "printvn": "iM", "setpc": "L", "sleep": "i",
    "refreshscreen": "", "romread8": "rw", "romread16": "rw", "romread32": "rw",
    "setpixel": "rrr", "getpixel": "rrw", "getkeyboard": "w", "gettime": "w",
    "getmemsize": "w", "getflag": "iw", "randreg": "rrW", "srandreg": "r",
    "drawstring": "rrrrriww", "numtostr": "rMw", "floatop": "iirrr",
    "floatcast": "iirr", "romread64": "rww", "fillrect": "rrrrr",
    "drawline": "rrrrr", "blit": "M", "setpalette": "rr",
    "fillscreen": "r", "fillcircle": "rrrr", "drawrect": "rrrrr",
    "jmpreg": "r", "callreg": "r",
    "pushr": "r", "popr": "w",
    "outfmt": "Miiiii",
    "save": "rrw", "load": "rrw", "beep": "rr",
    "audio_command": "irrw", "audio_load": "rrrriw", "memcpy": "rrri", "memset": "rrr",
    "minreg": "rrwi", "maxreg": "rrwi", "absreg": "rw", "mul64": "rrwwi",
    "setreg": "wi", "movreg": "wr",
    "loadreg8": "wm", "loadreg16": "wm", "loadreg32": "wm",
    "storereg8": "rs", "storereg16": "rs", "storereg32": "rs",
    "addreg": "rrw", "subreg": "rrw", "mulreg": "rrw", "divreg": "rrW", "modreg": "rrW",
    "addregv": "riw", "subregv": "riw", "mulregv": "riw", "compreg": "rrw",
    "ifreg": "rL", "ifnotreg": "rL", "notreg": "rw",
    "orreg": "rrw", "andreg": "rrw", "norreg": "rrw", "nandreg": "rrw",
    "xorreg": "rrw", "xnorreg": "rrw", "shlreg": "rrw", "shrreg": "rrw",
    "loadregi8": "rw", "loadregi16": "rw", "loadregi32": "rw",
    "storeregi8": "rr", "storeregi16": "rr", "storeregi32": "rr",
    "eqreg": "rrw", "neqreg": "rrw", "ltreg": "rrw", "gtreg": "rrw",
    "lereg": "rrw", "gereg": "rrw", "outreg": "r",
    "sdivreg": "rrW", "smodreg": "rrW", "sshrreg": "rrw", "outregsigned": "r",
    "sltreg": "rrw", "sgtreg": "rrw", "slereg": "rrw", "sgereg": "rrw",
    "andregv": "riw", "orregv": "riw", "xorregv": "riw",
    "shlregv": "riw", "shrregv": "riw", "sshrregv": "riw",
    "cmpregv": "riwi", "jcmp": "rriL", "jcmpv": "riiL", "exit": "",
}

# instructions that can be deleted when nothing reads what they write
PURE = {
    "setreg", "movreg", "loadreg8", "loadreg16", "loadreg32",
    "loadregi8", "loadregi16", "loadregi32", "romread8", "romread16", "romread32", "romread64",
    "addreg", "subreg", "mulreg", "divreg", "modreg", "addregv", "subregv", "mulregv",
    "compreg", "notreg", "orreg", "andreg", "norreg", "nandreg", "xorreg", "xnorreg",
    "shlreg", "shrreg", "eqreg", "neqreg", "ltreg", "gtreg", "lereg", "gereg",
    "sdivreg", "smodreg", "sshrreg", "sltreg", "sgtreg", "slereg", "sgereg",
    "andregv", "orregv", "xorregv", "shlregv", "shrregv", "sshrregv", "cmpregv",
    "getmemsize", "getflag",
}
# instructions that write RAM somewhere other than a tagged direct address
MEM_CLOBBER = {"storeregi8", "storeregi16", "storeregi32", "floatop", "floatcast",
               "numtostr", "input", "write", "setbyte", "load"}
LOADS = {"loadreg8": 1, "loadreg16": 2, "loadreg32": 4}
STORES = {"storereg8": 1, "storereg16": 2, "storereg32": 4}
COND_JUMPS = {"ifreg", "ifnotreg", "jcmp", "jcmpv"}
JUMPS = COND_JUMPS | {"setpc"}
ENDS_BLOCK = JUMPS | {"exit"}
NO_FALLTHROUGH = {"setpc", "exit"}

# register-register op -> register-immediate form for a constant right operand
IMM_FORM = {"addreg": "addregv", "subreg": "subregv", "mulreg": "mulregv",
            "andreg": "andregv", "orreg": "orregv", "xorreg": "xorregv",
            "shlreg": "shlregv", "shrreg": "shrregv", "sshrreg": "sshrregv"}
COMMUTATIVE = {"addreg", "mulreg", "andreg", "orreg", "xorreg"}
CMP_COND = {"eqreg": 0, "neqreg": 1, "ltreg": 2, "gtreg": 3, "lereg": 4, "gereg": 5,
            "sltreg": 6, "sgtreg": 7, "slereg": 8, "sgereg": 9}
# condition with its operands swapped: a < b  <=>  b > a
COND_SWAP = {0: 0, 1: 1, 2: 3, 3: 2, 4: 5, 5: 4, 6: 7, 7: 6, 8: 9, 9: 8}

ALLOCATABLE = range(0, 13)   # r13-r15 are the compiler's scratch registers
MASK = {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}


def log(msg):
    if DEBUG:
        print(f"[opt] {msg}", file=sys.stderr)


def reg_operands(ins):
    """(index, role) of every register operand."""
    roles = ROLES[ins.op]
    return [(k, roles[k]) for k in range(len(roles)) if roles[k] in "rwW"]


def uses_defs(ins):
    uses, defs = [], []
    roles = ROLES[ins.op]
    for k, role in enumerate(roles):
        if role == "r":
            uses.append(ins.args[k])
        elif role == "w":
            defs.append(ins.args[k])
        elif role == "W":
            uses.append(ins.args[k])
            defs.append(ins.args[k])
    return uses, defs


def is_const(v):
    return isinstance(v, int) and not isinstance(v, (Addr, bool))


def bit_len(v):
    return max(1, int(v).bit_length()) if is_const(v) else 32


class Optimizer:
    def __init__(self, comp):
        self.c = comp
        self.code = comp.code
        self.slot_ok = not comp.raw_memory
        self.stats = {}

    def bump(self, key, n=1):
        self.stats[key] = self.stats.get(key, 0) + n

    # ------------------------------------------------------------------ helpers
    def live_indices(self):
        return [i for i, ins in enumerate(self.code) if ins is not None]

    def label_refs(self):
        refs = {}
        for ins in self.code:
            if ins is not None and ins.op != "LABEL":
                for a in ins.args:
                    if isinstance(a, Label):
                        refs[a] = refs.get(a, 0) + 1
        for item in self.c.rom_data:
            for _, target in item["relocations"]:
                if isinstance(target, Label):
                    refs[target] = refs.get(target, 0) + 1
        return refs

    def next_real(self, i):
        """Index of the first non-label instruction after i (or None)."""
        n = len(self.code)
        j = i + 1
        while j < n and (self.code[j] is None or self.code[j].op == "LABEL"):
            j += 1
        return j if j < n else None

    def labels_between(self, i, j):
        return {self.code[k].args[0] for k in range(i + 1, j)
                if self.code[k] is not None and self.code[k].op == "LABEL"}

    # --------------------------------------------------------- 1. jump cleanup
    def jump_cleanup(self):
        changed = True
        rounds = 0
        while changed and rounds < 20:
            changed = False
            rounds += 1
            code = self.code
            label_at = {}
            for i, ins in enumerate(code):
                if ins is not None and ins.op == "LABEL":
                    label_at[ins.args[0]] = i
            # thread jump chains:  jump L ... L: setpc M   ->  jump M
            for i, ins in enumerate(code):
                if ins is None or ins.op not in JUMPS:
                    continue
                target = ins.args[-1]
                seen = set()
                while target not in seen:
                    seen.add(target)
                    j = self.next_real(label_at[target])
                    if j is None or code[j].op != "setpc":
                        break
                    target = code[j].args[0]
                if target is not ins.args[-1]:
                    ins.args[-1] = target
                    changed = True
                    self.bump("jumps threaded")
            for i, ins in enumerate(code):
                if ins is None or ins.op not in JUMPS:
                    continue
                j = self.next_real(i)
                following = self.labels_between(i, j if j is not None else len(code))
                # jump to the very next instruction
                if ins.args[-1] in following:
                    code[i] = None
                    changed = True
                    self.bump("jumps to next removed")
                    continue
                # if (c) goto L1; goto L2; L1:   ->   if (!c) goto L2; L1:
                if ins.op in COND_JUMPS and j is not None and code[j].op == "setpc":
                    k = self.next_real(j)
                    after = self.labels_between(j, k if k is not None else len(code))
                    if ins.args[-1] in after and not self.labels_between(i, j):
                        self.invert(ins)
                        ins.args[-1] = code[j].args[0]
                        code[j] = None
                        changed = True
                        self.bump("branches inverted")
            # unreachable code after setpc/exit, up to the next label
            dead = False
            for i, ins in enumerate(code):
                if ins is None:
                    continue
                if ins.op == "LABEL":
                    dead = False
                elif dead:
                    code[i] = None
                    changed = True
                    self.bump("unreachable removed")
                elif ins.op in NO_FALLTHROUGH:
                    dead = True
            refs = self.label_refs()
            for i, ins in enumerate(code):
                if ins is not None and ins.op == "LABEL" and ins.args[0] not in refs:
                    code[i] = None
                    changed = True

    @staticmethod
    def invert(ins):
        if ins.op == "ifreg":
            ins.op = "ifnotreg"
        elif ins.op == "ifnotreg":
            ins.op = "ifreg"
        else:  # jcmp / jcmpv: condition is operand 2
            ins.args[2] = COND_INVERT[ins.args[2]]

    # ------------------------------------------------------- slot bookkeeping
    def analyze_slots(self):
        """Per slot: direct loads, direct stores, escape, and whether every
        access is a plain full-width access at offset 0."""
        info = {}
        for s in self.c.slots:
            info[s.id] = {"slot": s, "loads": [], "stores": [], "escaped": False,
                          "regular": s.size in (1, 2, 4), "refs": 0}
        n = len(self.code)
        for i, ins in enumerate(self.code):
            if ins is None or ins.op == "LABEL":
                continue
            roles = ROLES[ins.op]
            for k, a in enumerate(ins.args):
                if not isinstance(a, Addr):
                    if roles[k] in "msM":
                        # a RAM address the compiler did not tag
                        log(f"untagged RAM address {a!r} in {ins!r}")
                        self.slot_ok = False
                    continue
                s = a.slot
                d = info[s.id]
                d["refs"] += 1
                end = s.end if s.end is not None else n
                if not (s.start <= i < end):
                    # used outside its lifetime: the bookkeeping is wrong
                    # somewhere, so don't trust any slot-based optimization
                    log(f"slot {s.name}@{s.addr} used at {i} outside [{s.start},{end})")
                    self.slot_ok = False
                role = roles[k]
                if role in "ms":
                    width = LOADS.get(ins.op) or STORES.get(ins.op)
                    if a.off < 0 or a.off + width > s.size:
                        log(f"slot {s.name} accessed out of bounds at {i}")
                        self.slot_ok = False
                    if a.off != 0 or width != s.size:
                        d["regular"] = False
                    (d["loads"] if role == "m" else d["stores"]).append(i)
                else:
                    d["escaped"] = True
        self.info = info
        return info

    # ----------------------------------------------------- 2. store forwarding
    def forward_stores(self):
        """Within each basic block, remember which register holds the value
        of which RAM location, and turn reloads into register moves."""
        info = self.info if self.slot_ok else None
        code = self.code

        cur = [0]

        def escaped(addr):
            """Could an indirect write at the current instruction hit this
            remembered location? Yes if its variable escaped, and also if
            the variable is no longer alive: the address may belong to a
            different (escaped) variable by now."""
            if info is None or info[addr.slot.id]["escaped"]:
                return True
            s = addr.slot
            return s.start > cur[0] or (s.end is not None and s.end <= cur[0])

        reg_mem = {}     # reg -> (addr_int, width, exact, Addr)
        reg_const = {}   # reg -> int
        reg_bits = {}    # reg -> max significant bits
        mem_const = {}   # (addr_int, width) -> (value, Addr)

        def reset():
            reg_mem.clear(); reg_const.clear(); reg_bits.clear(); mem_const.clear()

        def kill_reg(r):
            reg_mem.pop(r, None); reg_const.pop(r, None); reg_bits.pop(r, None)

        def kill_mem(lo, hi):
            for r in [r for r, (a, w, _, _) in reg_mem.items() if a < hi and lo < a + w]:
                del reg_mem[r]
            for key in [k for k in mem_const if k[0] < hi and lo < k[0] + k[1]]:
                del mem_const[key]

        def clobber_escaped():
            for r in [r for r, v in reg_mem.items() if escaped(v[3])]:
                del reg_mem[r]
            for key in [k for k, v in mem_const.items() if escaped(v[1])]:
                del mem_const[key]

        for i, ins in enumerate(code):
            if ins is None:
                continue
            cur[0] = i
            op = ins.op
            if op == "LABEL":
                reset()
                continue
            if op in LOADS and isinstance(ins.args[1], Addr):
                r, a = ins.args
                w = LOADS[op]
                src = None
                for s, (ma, mw, exact, _) in reg_mem.items():
                    if ma == int(a) and mw == w:
                        src = (s, exact)
                        if exact:
                            break
                if src is not None and LIMIT >= 0:
                    if self.stats.get("_fw", 0) >= LIMIT:
                        src = None
                    else:
                        self.bump("_fw")
                        if self.stats["_fw"] == LIMIT:
                            log(f"last forwarding at {i}: {ins!r} from r{src[0]}")
                if src is not None:
                    s, exact = src
                    if exact:
                        if s == r:
                            code[i] = None
                        else:
                            code[i] = Ins("movreg", [r, s])
                        self.bump("loads forwarded")
                    else:
                        code[i] = Ins("andregv", [s, MASK[w], r])
                        self.bump("loads forwarded (masked)")
                    bits = min(reg_bits.get(s, 32), 8 * w)
                    kill_reg(r)
                    reg_mem[r] = (int(a), w, True, a)
                    reg_bits[r] = bits
                    continue
                if (int(a), w) in mem_const and LIMIT >= 0:
                    if self.stats.get("_fw", 0) >= LIMIT:
                        mem_const.pop((int(a), w))
                    else:
                        self.bump("_fw")
                        if self.stats["_fw"] == LIMIT:
                            log(f"last forwarding (const) at {i}: {ins!r}")
                if (int(a), w) in mem_const:
                    val = mem_const[(int(a), w)][0]
                    code[i] = Ins("setreg", [r, val])
                    self.bump("loads replaced by constants")
                    kill_reg(r)
                    reg_const[r] = val
                    reg_bits[r] = bit_len(val)
                    reg_mem[r] = (int(a), w, True, a)
                    continue
                kill_reg(r)
                reg_mem[r] = (int(a), w, True, a)
                reg_bits[r] = 8 * w
                continue
            if op in STORES and isinstance(ins.args[1], Addr):
                r, a = ins.args
                w = STORES[op]
                kill_mem(int(a), int(a) + w)
                exact = reg_bits.get(r, 32) <= 8 * w
                reg_mem[r] = (int(a), w, exact, a)
                if r in reg_const and is_const(reg_const[r]):
                    mem_const[(int(a), w)] = (reg_const[r] & MASK[w], a)
                continue
            if op in STORES or op in LOADS:
                # untagged address (raw memory program): could alias anything
                if op in STORES:
                    reg_mem.clear(); mem_const.clear()
                kill_reg(ins.args[0])
                continue
            if op in MEM_CLOBBER:
                if info is not None:
                    clobber_escaped()      # only escaped variables can be written this way
                else:
                    reg_mem.clear(); mem_const.clear()
            _, defs = uses_defs(ins)
            if op == "movreg":
                d, s = ins.args
                if d != s:
                    kill_reg(d)
                    if s in reg_mem:
                        reg_mem[d] = reg_mem[s]
                    if s in reg_const:
                        reg_const[d] = reg_const[s]
                    if s in reg_bits:
                        reg_bits[d] = reg_bits[s]
            else:
                for d in defs:
                    kill_reg(d)
                if op == "setreg" and is_const(ins.args[1]):
                    reg_const[ins.args[0]] = ins.args[1]
                    reg_bits[ins.args[0]] = bit_len(ins.args[1])
                elif defs:
                    b = result_bits(ins, reg_bits)
                    if b < 32:
                        reg_bits[defs[0]] = b
            if op in ENDS_BLOCK and op in NO_FALLTHROUGH:
                reset()

    # ---------------------------------------------------------- 3. dead stores
    def remove_dead_stores(self):
        removed = 0
        for d in self.info.values():
            if d["escaped"] or d["loads"] or not d["stores"]:
                continue
            for i in d["stores"]:
                if self.code[i] is not None:
                    self.code[i] = None
                    removed += 1
            d["stores"] = []
        self.bump("dead stores removed", removed)

    # ------------------------------------------------ 3b. constant variables
    def propagate_constant_slots(self):
        """A variable that is stored exactly once, with a known constant,
        before all of its loads, is that constant: inlined parameters such as
        drawBitmap(x, y, 0, 16, 16, 1) are the typical case. The store must be
        the only way into the rest of the variable's lifetime: every label in
        that range may only be jumped to from inside it."""
        code = self.code
        n = len(code)
        label_at, sources = {}, {}
        for i, ins in enumerate(code):
            if ins is None:
                continue
            if ins.op == "LABEL":
                label_at[ins.args[0]] = i
            elif ins.op in JUMPS:
                sources.setdefault(ins.args[-1], []).append(i)
        label_positions = sorted(label_at.values())
        import bisect
        done = 0
        for d in self.info.values():
            if d["escaped"] or not d["regular"] or len(d["stores"]) != 1 or not d["loads"]:
                continue
            p = d["stores"][0]
            if any(i < p for i in d["loads"]):
                continue
            s = d["slot"]
            end = n if s.end is None else s.end
            reg = code[p].args[0]
            val = None
            for k in range(p - 1, -1, -1):     # constant in `reg` at the store?
                ins = code[k]
                if ins is None:
                    continue
                if ins.op == "LABEL" or ins.op in ENDS_BLOCK:
                    break
                _, defs = uses_defs(ins)
                if reg in defs:
                    if ins.op == "setreg" and is_const(ins.args[1]):
                        val = ins.args[1] & MASK[s.size]
                    break
            if val is None:
                continue
            ok = True
            for li in label_positions[bisect.bisect_right(label_positions, p):
                                      bisect.bisect_left(label_positions, end)]:
                if any(not (p < j < end) for j in sources.get(code[li].args[0], [])):
                    ok = False
                    break
            if not ok:
                continue
            for i in d["loads"]:
                code[i] = Ins("setreg", [code[i].args[0], val])
            code[p] = None
            d["loads"], d["stores"] = [], []
            done += 1
        self.bump("constant variables propagated", done)

    # ---------------------------------------------------- 4. register promotion
    def loop_depth(self):
        n = len(self.code)
        label_at = {}
        for i, ins in enumerate(self.code):
            if ins is not None and ins.op == "LABEL":
                label_at[ins.args[0]] = i
        diff = [0] * (n + 1)
        for j, ins in enumerate(self.code):
            if ins is not None and ins.op in JUMPS:
                t = label_at[ins.args[-1]]
                if t <= j:
                    diff[t] += 1
                    diff[j + 1] -= 1
        depth, cur = [0] * n, 0
        for i in range(n):
            cur += diff[i]
            depth[i] = cur
        return depth

    def promote(self):
        code = self.code
        n = len(code)
        depth = self.loop_depth()
        # prefix counts of explicit register mentions, per register
        mention = {r: [0] * (n + 1) for r in ALLOCATABLE}
        for i, ins in enumerate(code):
            row = set()
            if ins is not None and ins.op != "LABEL":
                for k, _ in reg_operands(ins):
                    if ins.args[k] in mention:
                        row.add(ins.args[k])
            for r in ALLOCATABLE:
                mention[r][i + 1] = mention[r][i] + (1 if r in row else 0)

        cands = []
        for d in self.info.values():
            s = d["slot"]
            if d["escaped"] or not d["regular"] or not (d["loads"] or d["stores"]):
                continue
            lo = 0 if s.permanent else s.start
            hi = n if s.end is None else s.end
            weight = sum(8 ** min(depth[i], 6) for i in d["loads"] + d["stores"])
            cands.append((weight, lo, hi, d))
        cands.sort(key=lambda c: (-c[0], c[1]))
        assigned = {r: [] for r in ALLOCATABLE}   # r -> list of (lo, hi)
        promoted = 0
        for weight, lo, hi, d in cands:
            chosen = None
            for r in ALLOCATABLE:
                if mention[r][hi] - mention[r][lo]:
                    continue
                if any(a < hi and lo < b for a, b in assigned[r]):
                    continue
                chosen = r
                break
            if chosen is None:
                if DEBUG and weight >= 64:
                    log(f"  no register for {d['slot'].name} weight {weight} [{lo},{hi})")
                continue
            assigned[chosen].append((lo, hi))
            d["reg"] = chosen
            if DEBUG and weight >= 64:
                log(f"  r{chosen} <- {d['slot'].name} weight {weight} [{lo},{hi})")
            promoted += 1
            size = d["slot"].size
            for i in d["loads"]:
                ins = code[i]
                code[i] = Ins("movreg", [ins.args[0], chosen])
            for i in d["stores"]:
                ins = code[i]
                if size == 4:
                    code[i] = Ins("movreg", [chosen, ins.args[0]])
                else:
                    code[i] = Ins("andregv", [ins.args[0], MASK[size], chosen])
        self.bump("variables promoted to registers", promoted)

    # --------------------------------------------------- 5. register cleanup
    def build_blocks(self):
        """Basic blocks as lists of instruction indices, plus successors."""
        code = self.code
        blocks, cur = [], []
        block_of_label = {}
        for i, ins in enumerate(code):
            if ins is None:
                continue
            if ins.op == "LABEL":
                if cur:
                    blocks.append(cur)
                    cur = []
                block_of_label[ins.args[0]] = len(blocks)
                continue
            cur.append(i)
            if ins.op in ENDS_BLOCK:
                blocks.append(cur)
                cur = []
        if cur:
            blocks.append(cur)
        # labels at the very end with no block after them
        succ = []
        for b, idxs in enumerate(blocks):
            last = code[idxs[-1]]
            s = []
            if last.op in JUMPS:
                t = block_of_label.get(last.args[-1])
                if t is not None and t < len(blocks):
                    s.append(t)
            if last.op not in NO_FALLTHROUGH and b + 1 < len(blocks):
                s.append(b + 1)
            succ.append(s)
        return blocks, succ

    def liveness(self):
        """live_after[i] = bitmask of registers live right after instruction i."""
        code = self.code
        blocks, succ = self.build_blocks()
        nb = len(blocks)
        ud = {}
        gen, kill = [0] * nb, [0] * nb
        for b, idxs in enumerate(blocks):
            g = k = 0
            for i in reversed(idxs):
                uses, defs = uses_defs(code[i])
                strong = 0
                roles = ROLES[code[i].op]
                for kk, role in enumerate(roles):
                    if role == "w":
                        strong |= 1 << code[i].args[kk]
                um = 0
                for r in uses:
                    um |= 1 << r
                g = (g & ~strong) | um
                k |= strong
                ud[i] = (um, strong)
            gen[b], kill[b] = g, k
        live_in = [0] * nb
        live_out = [0] * nb
        changed = True
        while changed:
            changed = False
            for b in range(nb - 1, -1, -1):
                out = 0
                for s in succ[b]:
                    out |= live_in[s]
                inn = gen[b] | (out & ~kill[b])
                if out != live_out[b] or inn != live_in[b]:
                    live_out[b], live_in[b] = out, inn
                    changed = True
        live_after = {}
        for b, idxs in enumerate(blocks):
            live = live_out[b]
            for i in reversed(idxs):
                live_after[i] = live
                um, strong = ud[i]
                live = (live & ~strong) | um
        return blocks, live_after

    def register_cleanup(self):
        for _ in range(30):
            changed = self.copy_and_const_propagate()
            changed |= self.retarget_and_dce()
            if not changed:
                break

    def copy_and_const_propagate(self):
        """Forward pass per block: replace reads of a copied register by the
        original, fold known constants into immediate forms, drop masks that
        cannot change anything."""
        code = self.code
        changed = False
        # Facts survive a conditional jump on the fall-through path, so the
        # unit here is an extended block: from a label to the next label.
        blocks, cur = [], []
        for i, ins in enumerate(code):
            if ins is None:
                continue
            if ins.op == "LABEL":
                if cur:
                    blocks.append(cur)
                cur = []
                continue
            cur.append(i)
            if ins.op in NO_FALLTHROUGH:
                blocks.append(cur)
                cur = []
        if cur:
            blocks.append(cur)
        for idxs in blocks:
            copy_of = {}     # d -> s   (d currently equals s)
            const = {}       # r -> int
            bits = {}        # r -> max significant bits
            for i in idxs:
                ins = code[i]
                if ins is None:
                    continue
                roles = ROLES[ins.op]
                # 1. rewrite reads
                for k, role in enumerate(roles):
                    if role == "r" and ins.args[k] in copy_of:
                        ins.args[k] = copy_of[ins.args[k]]
                        changed = True
                        self.bump("copies propagated")
                # 2. constant folding into immediate forms
                new = fold_constants(ins, const)
                if new is not None:
                    code[i] = ins = new
                    roles = ROLES[ins.op]
                    changed = True
                    self.bump("constants folded into instructions")
                folded = evaluate(ins, const)
                if folded is not None:
                    code[i] = ins = folded
                    roles = ROLES[ins.op]
                    changed = True
                    self.bump("constant expressions folded")
                if ins.op == "andregv" and is_const(ins.args[1]):
                    m = ins.args[1] & 0xFFFFFFFF
                    if m == (1 << m.bit_length()) - 1 and bits.get(ins.args[0], 32) <= m.bit_length():
                        code[i] = ins = Ins("movreg", [ins.args[2], ins.args[0]])
                        roles = ROLES[ins.op]
                        changed = True
                        self.bump("masks removed")
                if ins.op == "movreg" and ins.args[0] == ins.args[1]:
                    code[i] = None
                    changed = True
                    continue
                # 3. update facts
                _, defs = uses_defs(ins)
                for d in defs:
                    copy_of.pop(d, None)
                    for x in [x for x, s in copy_of.items() if s == d]:
                        del copy_of[x]
                    const.pop(d, None)
                if ins.op == "movreg":
                    d, s = ins.args
                    copy_of[d] = copy_of.get(s, s)
                    if s in const:
                        const[d] = const[s]
                    if s in bits:
                        bits[d] = bits[s]
                    else:
                        bits.pop(d, None)
                elif ins.op == "setreg" and is_const(ins.args[1]):
                    const[ins.args[0]] = ins.args[1] & 0xFFFFFFFF
                    bits[ins.args[0]] = bit_len(ins.args[1] & 0xFFFFFFFF)
                else:
                    for d in defs:
                        bits.pop(d, None)
                    if len(defs) == 1:
                        b = result_bits(ins, bits)
                        if b < 32:
                            bits[defs[0]] = b
        return changed

    def retarget_and_dce(self):
        code = self.code
        blocks, live_after = self.liveness()
        changed = False
        # dead pure instructions
        for idxs in blocks:
            for i in idxs:
                ins = code[i]
                if ins is None or ins.op not in PURE:
                    continue
                _, defs = uses_defs(ins)
                if not any(live_after[i] >> d & 1 for d in defs):
                    code[i] = None
                    changed = True
                    self.bump("dead instructions removed")
        # producer retargeting:  OP ..., t   ...   movreg d, t   (t dead after)
        #                     -> OP ..., d
        for idxs in blocks:
            idxs = [i for i in idxs if code[i] is not None]
            touched = set()   # liveness of these is stale after a retarget
            for pos, j in enumerate(idxs):
                mv = code[j]
                if mv is None or mv.op != "movreg":
                    continue
                d, t = mv.args
                if d == t or d in touched or t in touched or live_after[j] >> t & 1:
                    continue
                for back in range(pos - 1, -1, -1):
                    i = idxs[back]
                    ins = code[i]
                    if ins is None:
                        continue
                    uses, defs = uses_defs(ins)
                    roles = ROLES[ins.op]
                    if t in defs:
                        wpos = [k for k, role in enumerate(roles) if role in "wW"]
                        if (len(wpos) == 1 and roles[wpos[0]] == "w"
                                and ins.args[wpos[0]] == t):
                            ins.args[wpos[0]] = d
                            code[j] = None
                            touched.update((d, t))
                            changed = True
                            self.bump("producers retargeted")
                        break
                    if t in uses or d in uses or d in defs:
                        break
        return changed

    # --------------------------------------------------------- 6. RAM layout
    def relocate(self):
        code = self.code
        used = set()
        for ins in code:
            if ins is not None and ins.op != "LABEL":
                for a in ins.args:
                    if isinstance(a, Addr):
                        used.add(a.slot.id)
        new_addr = {}
        top = 0
        for s in self.c.slots:
            if s.permanent and s.id in used:
                new_addr[s.id] = top
                top += s.size
        peak = top
        stack = []   # (slot, end_addr)
        for s in self.c.slots:
            if s.permanent or s.id not in used:
                continue
            while stack and stack[-1][0].end is not None and stack[-1][0].end <= s.start:
                stack.pop()
            base = stack[-1][1] if stack else top
            new_addr[s.id] = base
            stack.append((s, base + s.size))
            peak = max(peak, base + s.size)
        for ins in code:
            if ins is None or ins.op == "LABEL":
                continue
            for k, a in enumerate(ins.args):
                if isinstance(a, Addr):
                    ins.args[k] = Addr(new_addr[a.slot.id] + a.off, a.slot, a.off)
        self.bump("RAM slots kept", len(used))
        return peak

    # ------------------------------------------------------- 7. loop rotation
    def rotate_loops(self, max_header=6):
        """while/for loops test at the top and jump back at the bottom:
              L: H; if !cond goto E; body; goto L; E:
        Repeating the (short, jump-free) header at the bottom saves the
        unconditional jump on every iteration:
              L: H; if !cond goto E; B: body; H; if cond goto B; E:
        Runs after the RAM layout because it inserts instructions."""
        code = [ins for ins in self.code if ins is not None]
        pos = {ins.args[0]: i for i, ins in enumerate(code) if ins.op == "LABEL"}
        out = []
        rotated = 0
        for k, ins in enumerate(code):
            if (ins.op == "setpc" and ins.args[0] in pos and pos[ins.args[0]] < k
                    and k + 1 < len(code) and code[k + 1].op == "LABEL"):
                start = pos[ins.args[0]]
                end_label = code[k + 1].args[0]
                header = []
                j = start + 1
                while j < k and code[j].op not in JUMPS and code[j].op not in ("LABEL", "exit"):
                    header.append(code[j])
                    j += 1
                test = code[j] if j < k else None
                if (test is not None and test.op in COND_JUMPS and test.args[-1] is end_label
                        and len(header) <= max_header
                        and not any(h.op not in PURE for h in header)):
                    body = Label("rotated")
                    copy = [Ins(h.op, list(h.args)) for h in header]
                    back = Ins(test.op, list(test.args))
                    self.invert(back)
                    back.args[-1] = body
                    out.extend(copy)
                    out.append(back)
                    # place the body label right after the original test
                    inserts = getattr(self, "_inserts", {})
                    inserts[id(test)] = body
                    self._inserts = inserts
                    rotated += 1
                    continue
            out.append(ins)
        inserts = getattr(self, "_inserts", {})
        final = []
        for ins in out:
            final.append(ins)
            if id(ins) in inserts:
                final.append(Ins("LABEL", [inserts[id(ins)]]))
        self.code[:] = final
        self.bump("loops rotated", rotated)

    # ------------------------------------------------------------------ driver
    def promote_and_propagate(self, consts_first, step):
        self.analyze_slots()
        if consts_first:
            step("constvars", self.propagate_constant_slots)
            self.register_cleanup()
            self.analyze_slots()
            step("dse", self.remove_dead_stores)
            step("promote", self.promote)
        else:
            step("promote", self.promote)
            self.register_cleanup()
            self.analyze_slots()
            step("constvars", self.propagate_constant_slots)
        self.register_cleanup()

    def snapshot(self):
        return ([None if ins is None else Ins(ins.op, list(ins.args)) for ins in self.code],
                dict(self.stats))

    def restore(self, snap):
        self.code[:] = [None if ins is None else Ins(ins.op, list(ins.args)) for ins in snap[0]]

    def weighted_cost(self):
        """Instruction count with each instruction weighted by 8^loop depth:
        a rough stand-in for how many instructions the program executes."""
        depth = self.loop_depth()
        return sum(8 ** min(depth[i], 6) for i, ins in enumerate(self.code)
                   if ins is not None and ins.op != "LABEL")

    def run(self):
        before = sum(1 for ins in self.code if ins is not None and ins.op != "LABEL")

        def step(name, fn):
            if name not in SKIP:
                fn()

        step("jumps", self.jump_cleanup)
        self.analyze_slots()
        step("forward", self.forward_stores)
        if self.slot_ok:
            self.analyze_slots()
            step("dse", self.remove_dead_stores)
        step("regs", self.register_cleanup)
        if self.slot_ok:
            self.analyze_slots()
            step("dse", self.remove_dead_stores)
            # Constant variables can either be propagated (a setreg per use,
            # often folded into an immediate operand) or get a register like
            # any other variable. Which is better depends on register
            # pressure, so try both orders and keep the cheaper program.
            snapshot = self.snapshot()
            variants = []
            for consts_first in (True, False):
                self.restore(snapshot)
                self.stats = dict(snapshot[1])
                self.promote_and_propagate(consts_first, step)
                variants.append((self.weighted_cost(), consts_first, self.snapshot()))
            best = min(variants, key=lambda v: v[0])
            log(f"weighted cost consts-first {variants[0][0]}, promote-first {variants[1][0]}")
            self.restore(best[2])
            self.stats = dict(best[2][1])
        step("regs", self.register_cleanup)
        step("jumps", self.jump_cleanup)
        step("regs", self.register_cleanup)
        required = self.c.max_addr
        if self.slot_ok and "reloc" not in SKIP:
            self.analyze_slots()
            required = self.relocate()
        # (slot lifetimes are instruction indices; nothing below needs them)
        step("rotate", self.rotate_loops)
        after = sum(1 for ins in self.code if ins is not None and ins.op != "LABEL")
        log(f"instructions {before} -> {after}, RAM {self.c.max_addr} -> {required} bytes"
            + ("" if self.slot_ok else " (slot passes disabled)"))
        for k, v in sorted(self.stats.items()):
            log(f"  {k}: {v}")
        return required


def result_bits(ins, bits):
    """Upper bound on the number of significant bits an instruction produces."""
    op, a = ins.op, ins.args
    if op in ("loadreg8", "loadregi8", "romread8", "getkeyboard", "numtostr"):
        return 8
    if op in ("loadreg16", "loadregi16", "romread16", "getpixel"):
        return 16
    if op in CMP_COND or op in ("cmpregv", "isflagsupported"):
        return 1
    if op == "andregv" and is_const(a[1]):
        return min(bits.get(a[0], 32), bit_len(a[1] & 0xFFFFFFFF))
    if op == "andreg":
        return min(bits.get(a[0], 32), bits.get(a[1], 32))
    if op in ("orreg", "xorreg"):
        return max(bits.get(a[0], 32), bits.get(a[1], 32))
    if op in ("orregv", "xorregv") and is_const(a[1]):
        return max(bits.get(a[0], 32), bit_len(a[1] & 0xFFFFFFFF))
    if op == "shrregv" and is_const(a[1]) and 0 < a[1] < 32:
        return max(1, bits.get(a[0], 32) - a[1])
    if op == "getflag":
        return 4
    return 32


def fold_constants(ins, const):
    """Rewrite a register-register instruction whose operand is a known
    constant into its immediate form. Returns the new Ins or None."""
    op, a = ins.op, ins.args
    if op in IMM_FORM:
        x, y, d = a
        if y in const:
            v = const[y]
            if op in ("shlreg", "shrreg", "sshrreg"):
                if not 0 <= v < 32:
                    return None
            return Ins(IMM_FORM[op], [x, v, d])
        if x in const and op in COMMUTATIVE:
            return Ins(IMM_FORM[op], [y, const[x], d])
        return None
    if op in CMP_COND:
        x, y, d = a
        if y in const:
            return Ins("cmpregv", [x, const[y], d, CMP_COND[op]])
        if x in const:
            return Ins("cmpregv", [y, const[x], d, COND_SWAP[CMP_COND[op]]])
        return None
    if op == "jcmp":
        x, y, cond, lab = a
        if y in const:
            return Ins("jcmpv", [x, const[y], cond, lab])
        if x in const:
            return Ins("jcmpv", [y, const[x], COND_SWAP[cond], lab])
        return None
    if op in ("ifreg", "ifnotreg") and a[0] in const:
        taken = (const[a[0]] != 0) == (op == "ifreg")
        return Ins("setpc", [a[1]]) if taken else Ins("movreg", [a[0], a[0]])
    if op == "jcmpv" and a[0] in const and is_const(a[1]):
        if cond_holds(a[2], const[a[0]], a[1] & 0xFFFFFFFF):
            return Ins("setpc", [a[3]])
        return Ins("movreg", [a[0], a[0]])
    if op == "movreg" and a[1] in const and a[0] != a[1]:
        return Ins("setreg", [a[0], const[a[1]]])
    if op in ("addregv", "subregv") and is_const(a[1]) and a[1] & 0xFFFFFFFF == 0:
        return Ins("movreg", [a[2], a[0]])
    return None


def _s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def evaluate(ins, const):
    """Replaces a pure instruction whose inputs are all known constants by a
    setreg of its result, and a few identities by a move. None otherwise."""
    op, a = ins.op, ins.args
    roles = ROLES.get(op, "")
    if op not in PURE or op in ("setreg", "movreg") or "W" in roles:
        return None
    if not all(is_const(x) for k, x in enumerate(a) if roles[k] == "i"):
        return None
    if roles in ("riw", "riwi"):
        x, imm, dst = a[0], a[1] & 0xFFFFFFFF, a[2]
        if x not in const:
            identity = ((op in ("addregv", "subregv", "orregv", "xorregv", "shlregv",
                                "shrregv", "sshrregv") and imm == 0)
                        or (op == "mulregv" and imm == 1)
                        or (op == "andregv" and imm == 0xFFFFFFFF))
            if identity:
                return Ins("movreg", [dst, x])
            if (op == "mulregv" or op == "andregv") and imm == 0:
                return Ins("setreg", [dst, 0])
            return None
        v = const[x]
        if op == "addregv": r = v + imm
        elif op == "subregv": r = v - imm
        elif op == "mulregv": r = v * imm
        elif op == "andregv": r = v & imm
        elif op == "orregv": r = v | imm
        elif op == "xorregv": r = v ^ imm
        elif op == "shlregv": r = v << imm if imm < 32 else 0
        elif op == "shrregv": r = v >> imm if imm < 32 else 0
        elif op == "sshrregv": r = _s32(v) >> min(imm, 31)
        elif op == "cmpregv": r = int(cond_holds(a[3], v, imm))
        else: return None
        return Ins("setreg", [dst, r & 0xFFFFFFFF])
    if roles == "rrw":
        x, y, dst = a
        if x not in const or y not in const:
            return None
        u, v = const[x], const[y]
        table = {
            "addreg": lambda: u + v, "subreg": lambda: u - v, "mulreg": lambda: u * v,
            "andreg": lambda: u & v, "orreg": lambda: u | v, "xorreg": lambda: u ^ v,
            "norreg": lambda: ~(u | v), "nandreg": lambda: ~(u & v), "xnorreg": lambda: ~(u ^ v),
            "shlreg": lambda: u << v if v < 32 else 0, "shrreg": lambda: u >> v if v < 32 else 0,
            "sshrreg": lambda: _s32(u) >> min(v, 31),
            "compreg": lambda: 0 if u == v else (1 if u > v else 2),
        }
        if op in CMP_COND:
            r = int(cond_holds(CMP_COND[op], u, v))
        elif op in table:
            r = table[op]()
        else:
            return None
        return Ins("setreg", [dst, r & 0xFFFFFFFF])
    if op == "notreg" and a[0] in const:
        return Ins("setreg", [a[1], ~const[a[0]] & 0xFFFFFFFF])
    return None


def cond_holds(cond, a, b):
    sa = a - (1 << 32) if a & 0x80000000 else a
    sb = b - (1 << 32) if b & 0x80000000 else b
    return [a == b, a != b, a < b, a > b, a <= b, a >= b,
            sa < sb, sa > sb, sa <= sb, sa >= sb][cond]


def optimize_program(comp):
    """Optimizes comp.code in place and returns the RAM size the program needs."""
    global Addr, Ins, Label, RomRef, COND_INVERT, OPS
    mod = sys.modules[type(comp).__module__]
    Addr, Ins, Label, RomRef = mod.Addr, mod.Ins, mod.Label, mod.RomRef
    COND_INVERT, OPS = mod.COND_INVERT, mod.OPS
    missing = set(OPS) - set(ROLES)
    assert not missing, f"optimizer has no operand roles for {missing}"
    return Optimizer(comp).run()
