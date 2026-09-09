'''
unilang bytecode VM by AzizBgBoss
https://github.com/AzizBgBoss/unilang

This VM executes compiled unilang bytecode (produced by a separate converter/assembler or written by hand if you hate yourself enough).

Register/byte-only ISA: memory is plain byte-addressable RAM (no bit shifting,
no per-instruction "size" operand). All arithmetic/logic/comparison happens in
16 general-purpose 32-bit registers (r0-r15). Memory is only touched to move a
single byte (setbyte/out/outc/write/input/printv) or to load/store a register
as 8/16/32 bits (loadreg8/16/32, storereg8/16/32). There is no generic
variable-width "mem" op anymore -- pick the loadreg/storereg width you need.

Operand encoding conventions (all big-endian):
  addr  -> 4 bytes (unsigned int, byte offset into RAM)
  reg   -> 1 byte  (register index, 0-15)
  val   -> 4 bytes (unsigned int)
  byte  -> 1 byte
  flag  -> 1 byte
  count -> 4 bytes
  pos   -> 4 bytes (bytecode offset, used for jumps/subroutines)
  text  -> 2 byte length prefix, followed by that many raw bytes

- flag 0 (graphics) (this will consume the last n bits of memory):
0 - disable graphics
1 - enable monochrome graphics (64x64) (1 x 64 x 64 = 4096 bits = 512 bytes)
2 - enable 4-shade grayscale graphics (64x64) (4 x 64 x 64 = 16384 bits = 2048 bytes)
3 - enable 8-bit color graphics (64x64) (8 x 64 x 64 = 32768 bits = 4096 bytes)
4 - enable monochrome graphics (128x64) (1 x 128 x 64 = 8192 bits = 1024 bytes)
5 - enable 4-shade grayscale graphics (128x64) (4 x 128 x 64 = 32768 bits = 4096 bytes)
6 - enable 8-bit color graphics (128x64) (8 x 128 x 64 = 65536 bits = 8192 bytes)

- flag 1 (keyboard) (this will consume the last n bits of memory (after graphics if it's used)) (Only gets supported once graphics are supported and on):
0 - disable keyboard
1 - NES style keyboard (Up + Down + Left + Right + A + B + select + start) (1 x 8 = 8 bits)
On keyboard: (Up + Down + Left + Right + C + V + Backspace + Enter)

0x00 (nop)               none                                 - do nothing (padding/alignment)
0x01 (flag)              flag(1), val(1)                      - set the value of a flag with a certain index to a 4-bit value
0x02 (isflagsupported)   flag(1), val(1), reg(1)              - check if a flag with a certain index is supported, result into reg
0x03 (setbyte)           addr(4), byte(1)                     - set a single memory byte
0x04 (write)             addr(4), text(0)                     - write text as raw bytes starting at addr
0x05 (outc)              addr(4)                              - output the byte at addr as a character
0x06 (out)               addr(4)                              - output the byte at addr as a number
0x07 (input)             chars(4), addr(4)                    - read chars and store them as bytes starting at addr
0x08 (print)             text(0)                              - print text
0x09 (printn)            text(0)                              - print text without a newline
0x0A (printv)            chars(4), addr(4)                    - print chars bytes from memory as characters
0x0B (printvn)           chars(4), addr(4)                    - same as printv without a newline
0x0C (cur)               none                                 - print the current position in the bytecode
0x0D (memory)            none                                 - print the current state of memory (hex dump)
0x0E (flags)             none                                 - print the current state of flags
0x0F (setpc)             pos(4)                               - set the program counter to a fixed bytecode position
0x10 (subroutine)        pos(4)                               - call a subroutine at a fixed bytecode position
0x11 (return)            none                                 - return from a subroutine
0x12 (sleep)             ms(4)                                - sleep for a certain amount of milliseconds
0x13 (refreshscreen)     none                                 - redraw the current frame and clear the framebuffer
0x14 (romread8)          reg_addr(1), reg_dst(1)              - read 1 byte from the bytecode's own ROM at the byte offset held in reg[reg_addr], into reg_dst
0x15 (romread16)         reg_addr(1), reg_dst(1)              - read 2 bytes from ROM into reg_dst
0x16 (romread32)         reg_addr(1), reg_dst(1)              - read 4 bytes from ROM into reg_dst
0x17 (setpixel)          regx(1), regy(1), regc(1)            - draw one pixel using x/y/color values from registers
0x18 (getpixel)          regx(1), regy(1), regdst(1)          - read one pixel's value into a register
0x19 (getkeyboard)       reg(1)                               - read keyboard state into a register
0x1A (gettime)           reg(1)                               - get the current time (ms) into a register
0x1B (getmemsize)        reg(1)                               - get the size of memory in bytes into a register
0x1C (getflag)           flag(1), reg(1)                      - read the current value of a flag into a register
0x1D (randreg)           min(4), max(4), reg(1)               - set a register to a random value between min and max (exclusive)

Registers: 16 general-purpose 32-bit registers, r0-r15 (index 0-15 as a single byte).
0x40 (setreg)      reg(1), val(4)              - reg = val
0x41 (movreg)      dst(1), src(1)              - dst = src
0x42 (loadreg8)    reg(1), addr(4)             - reg = mem byte at addr (zero-extended)
0x43 (loadreg16)   reg(1), addr(4)             - reg = mem 2 bytes at addr
0x44 (loadreg32)   reg(1), addr(4)             - reg = mem 4 bytes at addr
0x45 (storereg8)   reg(1), addr(4)             - mem byte at addr = reg & 0xFF
0x46 (storereg16)  reg(1), addr(4)             - mem 2 bytes at addr = reg & 0xFFFF
0x47 (storereg32)  reg(1), addr(4)             - mem 4 bytes at addr = reg
0x48 (addreg)      r1(1), r2(1), dst(1)        - dst = r1 + r2
0x49 (subreg)      r1(1), r2(1), dst(1)        - dst = r1 - r2
0x4A (mulreg)      r1(1), r2(1), dst(1)        - dst = r1 * r2
0x4B (divreg)      r1(1), r2(1), dst(1)        - dst = r1 // r2 (no-op if r2 == 0)
0x4C (modreg)      r1(1), r2(1), dst(1)        - dst = r1 % r2 (no-op if r2 == 0)
0x4D (addregv)     r1(1), val(4), dst(1)       - dst = r1 + val
0x4E (subregv)     r1(1), val(4), dst(1)       - dst = r1 - val
0x4F (mulregv)     r1(1), val(4), dst(1)       - dst = r1 * val
0x50 (compreg)     r1(1), r2(1), dst(1)        - dst = 0 if equal, 1 if r1 > r2, 2 if r1 < r2
0x51 (ifreg)       reg(1), pos(4)              - if reg != 0, jump to pos
0x52 (ifnotreg)    reg(1), pos(4)              - if reg == 0, jump to pos
0x53 (ifsubreg)    reg(1), pos(4)              - if reg != 0, call subroutine at pos
0x54 (ifnotsubreg) reg(1), pos(4)              - if reg == 0, call subroutine at pos
0x55 (notreg)      r1(1), dst(1)               - dst = ~r1 (32-bit)
0x56 (orreg)       r1(1), r2(1), dst(1)        - dst = r1 | r2
0x57 (andreg)      r1(1), r2(1), dst(1)        - dst = r1 & r2
0x58 (norreg)      r1(1), r2(1), dst(1)        - dst = ~(r1 | r2) (32-bit)
0x59 (nandreg)     r1(1), r2(1), dst(1)        - dst = ~(r1 & r2) (32-bit)
0x5A (xorreg)      r1(1), r2(1), dst(1)        - dst = r1 ^ r2
0x5B (xnorreg)     r1(1), r2(1), dst(1)        - dst = ~(r1 ^ r2) (32-bit)
0x5C (shlreg)      r1(1), r2(1), dst(1)        - dst = (r1 << r2) & 0xFFFFFFFF
0x5D (shrreg)      r1(1), r2(1), dst(1)        - dst = r1 >> r2

Indirect memory access: the address comes from a register at runtime, not a
bytecode literal. This is what pointer dereferencing and array-element access
(where the index is a variable, not a constant) compile down to.
0x60 (loadregi8)   reg_addr(1), reg_dst(1)     - reg_dst = mem byte at address reg[reg_addr]
0x61 (loadregi16)  reg_addr(1), reg_dst(1)     - reg_dst = mem 2 bytes at address reg[reg_addr]
0x62 (loadregi32)  reg_addr(1), reg_dst(1)     - reg_dst = mem 4 bytes at address reg[reg_addr]
0x63 (storeregi8)  reg_addr(1), reg_val(1)     - mem byte at address reg[reg_addr] = reg[reg_val] & 0xFF
0x64 (storeregi16) reg_addr(1), reg_val(1)     - mem 2 bytes at address reg[reg_addr] = reg[reg_val] & 0xFFFF
0x65 (storeregi32) reg_addr(1), reg_val(1)     - mem 4 bytes at address reg[reg_addr] = reg[reg_val]

Direct boolean comparisons: dst = 1 if the comparison holds, 0 otherwise.
0x66 (eqreg)       r1(1), r2(1), dst(1)        - dst = r1 == r2
0x67 (neqreg)      r1(1), r2(1), dst(1)        - dst = r1 != r2
0x68 (ltreg)       r1(1), r2(1), dst(1)        - dst = r1 < r2
0x69 (gtreg)       r1(1), r2(1), dst(1)        - dst = r1 > r2
0x6A (lereg)       r1(1), r2(1), dst(1)        - dst = r1 <= r2
0x6B (gereg)       r1(1), r2(1), dst(1)        - dst = r1 >= r2
0x6C (outreg)      reg(1)                      - print a register's full numeric value (not limited to a byte, unlike `out`)

0xFF (exit)              none                                 - exit the program
'''

import random
import sys
import time

# commandname: {"byte": the opcode byte, "operands": number of operands, "sizes": byte width of each operand (0 = length-prefixed text)}
op_codes = {
    "nop":              {"byte": 0x00, "operands": 0, "sizes": []},
    "flag":             {"byte": 0x01, "operands": 2, "sizes": [1, 1]},              # flag, val
    "isflagsupported":  {"byte": 0x02, "operands": 3, "sizes": [1, 1, 1]},           # flag, val, reg
    "setbyte":          {"byte": 0x03, "operands": 2, "sizes": [4, 1]},              # addr, byte
    "write":            {"byte": 0x04, "operands": 2, "sizes": [4, 0]},              # addr, text
    "outc":             {"byte": 0x05, "operands": 1, "sizes": [4]},                 # addr
    "out":              {"byte": 0x06, "operands": 1, "sizes": [4]},                 # addr
    "input":            {"byte": 0x07, "operands": 2, "sizes": [4, 4]},              # chars, addr
    "print":            {"byte": 0x08, "operands": 1, "sizes": [0]},                 # text
    "printn":           {"byte": 0x09, "operands": 1, "sizes": [0]},                 # text
    "printv":           {"byte": 0x0A, "operands": 2, "sizes": [4, 4]},              # chars, addr
    "printvn":          {"byte": 0x0B, "operands": 2, "sizes": [4, 4]},              # chars, addr
    "cur":              {"byte": 0x0C, "operands": 0, "sizes": []},
    "memory":           {"byte": 0x0D, "operands": 0, "sizes": []},
    "flags":            {"byte": 0x0E, "operands": 0, "sizes": []},
    "setpc":            {"byte": 0x0F, "operands": 1, "sizes": [4]},                 # pos
    "subroutine":       {"byte": 0x10, "operands": 1, "sizes": [4]},                 # pos
    "return":           {"byte": 0x11, "operands": 0, "sizes": []},
    "sleep":            {"byte": 0x12, "operands": 1, "sizes": [4]},                 # ms
    "refreshscreen":    {"byte": 0x13, "operands": 0, "sizes": []},
    "romread8":         {"byte": 0x14, "operands": 2, "sizes": [1, 1]},              # reg_addr, reg_dst
    "romread16":        {"byte": 0x15, "operands": 2, "sizes": [1, 1]},              # reg_addr, reg_dst
    "romread32":        {"byte": 0x16, "operands": 2, "sizes": [1, 1]},              # reg_addr, reg_dst
    "setpixel":         {"byte": 0x17, "operands": 3, "sizes": [1, 1, 1]},           # regx, regy, regc
    "getpixel":         {"byte": 0x18, "operands": 3, "sizes": [1, 1, 1]},           # regx, regy, regdst
    "getkeyboard":      {"byte": 0x19, "operands": 1, "sizes": [1]},                 # reg
    "gettime":          {"byte": 0x1A, "operands": 1, "sizes": [1]},                 # reg
    "getmemsize":       {"byte": 0x1B, "operands": 1, "sizes": [1]},                 # reg
    "getflag":          {"byte": 0x1C, "operands": 2, "sizes": [1, 1]},              # flag, reg
    "randreg":          {"byte": 0x1D, "operands": 3, "sizes": [4, 4, 1]},           # min, max, reg

    # --- registers: 16 x 32-bit general-purpose registers (reg index is 1 byte, 0-15).
    "setreg":           {"byte": 0x40, "operands": 2, "sizes": [1, 4]},              # reg, val
    "movreg":           {"byte": 0x41, "operands": 2, "sizes": [1, 1]},              # dst_reg, src_reg
    "loadreg8":         {"byte": 0x42, "operands": 2, "sizes": [1, 4]},              # reg, addr
    "loadreg16":        {"byte": 0x43, "operands": 2, "sizes": [1, 4]},              # reg, addr
    "loadreg32":        {"byte": 0x44, "operands": 2, "sizes": [1, 4]},              # reg, addr
    "storereg8":        {"byte": 0x45, "operands": 2, "sizes": [1, 4]},              # reg, addr
    "storereg16":       {"byte": 0x46, "operands": 2, "sizes": [1, 4]},              # reg, addr
    "storereg32":       {"byte": 0x47, "operands": 2, "sizes": [1, 4]},              # reg, addr
    "addreg":           {"byte": 0x48, "operands": 3, "sizes": [1, 1, 1]},           # reg1, reg2, dst
    "subreg":           {"byte": 0x49, "operands": 3, "sizes": [1, 1, 1]},
    "mulreg":           {"byte": 0x4A, "operands": 3, "sizes": [1, 1, 1]},
    "divreg":           {"byte": 0x4B, "operands": 3, "sizes": [1, 1, 1]},
    "modreg":           {"byte": 0x4C, "operands": 3, "sizes": [1, 1, 1]},
    "addregv":          {"byte": 0x4D, "operands": 3, "sizes": [1, 4, 1]},           # reg1, const, dst
    "subregv":          {"byte": 0x4E, "operands": 3, "sizes": [1, 4, 1]},
    "mulregv":          {"byte": 0x4F, "operands": 3, "sizes": [1, 4, 1]},
    "compreg":          {"byte": 0x50, "operands": 3, "sizes": [1, 1, 1]},           # reg1, reg2, dst
    "ifreg":            {"byte": 0x51, "operands": 2, "sizes": [1, 4]},              # reg, pos
    "ifnotreg":         {"byte": 0x52, "operands": 2, "sizes": [1, 4]},              # reg, pos
    "ifsubreg":         {"byte": 0x53, "operands": 2, "sizes": [1, 4]},              # reg, pos
    "ifnotsubreg":      {"byte": 0x54, "operands": 2, "sizes": [1, 4]},              # reg, pos
    "notreg":           {"byte": 0x55, "operands": 2, "sizes": [1, 1]},              # r1, dst
    "orreg":            {"byte": 0x56, "operands": 3, "sizes": [1, 1, 1]},
    "andreg":           {"byte": 0x57, "operands": 3, "sizes": [1, 1, 1]},
    "norreg":           {"byte": 0x58, "operands": 3, "sizes": [1, 1, 1]},
    "nandreg":          {"byte": 0x59, "operands": 3, "sizes": [1, 1, 1]},
    "xorreg":           {"byte": 0x5A, "operands": 3, "sizes": [1, 1, 1]},
    "xnorreg":          {"byte": 0x5B, "operands": 3, "sizes": [1, 1, 1]},
    "shlreg":           {"byte": 0x5C, "operands": 3, "sizes": [1, 1, 1]},
    "shrreg":           {"byte": 0x5D, "operands": 3, "sizes": [1, 1, 1]},

    # --- indirect: address comes from a register at runtime (not a bytecode
    # literal) -- this is what pointers/array-element access compile down to.
    "loadregi8":        {"byte": 0x60, "operands": 2, "sizes": [1, 1]},              # reg_addr, reg_dst
    "loadregi16":       {"byte": 0x61, "operands": 2, "sizes": [1, 1]},
    "loadregi32":       {"byte": 0x62, "operands": 2, "sizes": [1, 1]},
    "storeregi8":       {"byte": 0x63, "operands": 2, "sizes": [1, 1]},              # reg_addr, reg_val
    "storeregi16":      {"byte": 0x64, "operands": 2, "sizes": [1, 1]},
    "storeregi32":      {"byte": 0x65, "operands": 2, "sizes": [1, 1]},

    # --- direct boolean comparisons: dst = 1 if true, 0 if false.
    "eqreg":            {"byte": 0x66, "operands": 3, "sizes": [1, 1, 1]},
    "neqreg":           {"byte": 0x67, "operands": 3, "sizes": [1, 1, 1]},
    "ltreg":            {"byte": 0x68, "operands": 3, "sizes": [1, 1, 1]},
    "gtreg":            {"byte": 0x69, "operands": 3, "sizes": [1, 1, 1]},
    "lereg":            {"byte": 0x6A, "operands": 3, "sizes": [1, 1, 1]},
    "gereg":            {"byte": 0x6B, "operands": 3, "sizes": [1, 1, 1]},
    "outreg":           {"byte": 0x6C, "operands": 1, "sizes": [1]},                 # reg -- print its full numeric value

    "exit":             {"byte": 0xFF, "operands": 0, "sizes": []},
}

# reverse lookup: opcode byte -> command name
opcode_by_byte = {v["byte"]: k for k, v in op_codes.items()}

memsize = 1024 * 8 * 16
supportedFlags = [[0] * 16 for _ in range(16)]

supportedFlags[0][0] = 1 # disabling graphics is ofc supported
supportedFlags[1][0] = 1 # disabling keyboard is ofc supported

# pygame is only imported the first time graphics/keyboard (flag 0/1) is
# actually queried or used, not unconditionally at startup — programs that
# never touch graphics shouldn't need pygame at all. If pygame IS needed but
# isn't available, or fails to initialize (no display server, etc.), that's
# treated as fatal: a program that asked for graphics and silently ran
# headless anyway is worse than one that stops with a clear error.
pygame = None
_pygame_checked = False

def convert_size(n):
    if n < 8 * 1024:
        return f"{n // 8} KB"
    if n < 8 * 1024 * 1024:
        return f"{n // (8 * 1024)} MB"
    if n < 8 * 1024 * 1024 * 1024:
        return f"{n // (8 * 1024 * 1024)} GB"
    return f"{n} bits"

def ensure_pygame():
    global pygame, _pygame_checked
    if _pygame_checked:
        return
    _pygame_checked = True
    try:
        import pygame as _pygame_module
    except Exception as e:
        print(f"Fatal: this program requires pygame for graphics/keyboard support, but it could not be imported ({e}).")
        sys.exit(1)
    pygame = _pygame_module
    supportedFlags[0][1] = 1 # 64x64@1 -- graphics has its own dedicated framebuffer now, doesn't touch mem

print(f"Starting unilang bytecode VM with {memsize} bits of memory ({convert_size(memsize)})...\n")

# --- bit-level helpers: only used for `flags` (4-bit fields) and `framebuffer`
# (genuinely 1-bit-per-pixel). Main RAM (`mem`) is byte-addressable, see below.
def get_bit(mem, addr):
    return (mem[addr // 8] >> (addr % 8)) & 1

def set_bit(mem, addr, val):
    if val:
        mem[addr // 8] |= (1 << (addr % 8))
    else:
        mem[addr // 8] &= ~(1 << (addr % 8))

def set_bits(mem, addr, val, size):
    val %= 2 ** size
    for i in range(size):
        set_bit(mem, addr + i, val & (1 << (size - 1 - i)))

def get_bits(mem, addr, size):
    val = 0
    for i in range(size):
        val |= get_bit(mem, addr + i) << (size - i - 1)
    return val

# --- main RAM: byte-addressable, no bit shifting, no size operand. `size` is
# only ever 1/2/4 bytes and only used internally by load/storereg8/16/32.
def set_val(mem, addr, val, size):
    val %= 1 << (size * 8)
    for i in range(size):
        shift = (size - i - 1) * 8
        mem[(addr + i)] = (val >> shift) & 0xFF

def get_val(mem, addr, size):
    val = 0
    for i in range(size):
        val = (val << 8) | mem[(addr + i)]
    return val

def get_flag_value(flag_index):
    return get_bits(flags, flag_index * 4, 4)

def read_uint(bytecode, pos, nbytes):
    val = 0
    for i in range(nbytes):
        val = (val << 8) | bytecode[pos + i]
    return val, pos + nbytes

def read_operands(bytecode, pos, sizes):
    # reads each operand per the opcode's "sizes" list (0 means length-prefixed text)
    vals = []
    for width in sizes:
        if width == 0:
            length, pos = read_uint(bytecode, pos, 2)
            text = bytecode[pos:pos + length].decode("utf-8", errors="replace")
            pos += length
            vals.append(text)
        else:
            val, pos = read_uint(bytecode, pos, width)
            vals.append(val)
    return vals, pos

def rom_read(bytecode, addr, size):
    # Reads `size` bytes starting at byte offset `addr` in the program's own
    # bytecode (the ".ulc" file). Reading past the end returns 0 rather than
    # erroring, so a program that miscalculates a ROM offset degrades to
    # blank data rather than crashing the VM.
    val = 0
    for i in range(size):
        pos = addr + i
        b = bytecode[pos] if 0 <= pos < len(bytecode) else 0
        val = (val << 8) | b
    return val

# check for -f flag in command line arguments (now expects a compiled bytecode file)
if len(sys.argv) > 1 and sys.argv[1] == "-f":
    with open(sys.argv[2], "rb") as f:
        bytecode = f.read()
else:
    print("Usage: main.py -f <compiled bytecode file>")
    sys.exit(1)

print(f"Loaded {len(bytecode)} bytes of bytecode.\n")

running = True
pc = 0
mem = bytearray(memsize // 8)
flags = bytearray(16 * 16 // 8)
reg = [0] * 16  # 16 general-purpose 32-bit registers

nextreturn = 0
graphics_screen = None
keyboard_state = 0
graphics_mode = 0  # cached copy of flag 0's value, kept in sync by the "flag" opcode
keyboard_mode = 0  # cached copy of flag 1's value, kept in sync by the "flag" opcode
# Dedicated framebuffer, sized for the largest supported mode (128x64@8bpp = 65536 bits).
# Graphics/keyboard no longer live in addressable RAM at all -- use setpixel/getpixel/getkeyboard.
framebuffer = bytearray(65536 // 8)

def initialize_graphics():
    global graphics_screen
    ensure_pygame()  # fatal if unavailable
    if graphics_screen is None:
        try:
            pygame.init()
            graphics_screen = pygame.display.set_mode((64 * 10, 64 * 10))
        except Exception as e:
            print(f"Fatal: pygame failed to initialize graphics ({e}). Stopping.")
            sys.exit(1)
        supportedFlags[1][1] = 1 # NES is now supported

def shutdown_graphics():
    global graphics_screen, keyboard_state, keyboard_mode
    if pygame is not None and graphics_screen is not None:
        pygame.display.quit()
        graphics_screen = None
        keyboard_state = 0
        keyboard_mode = 0
        supportedFlags[1][1] = 0
        set_bits(flags, 1 * 4, 0, 4)

def update_graphics():
    global running
    if graphics_screen is None:
        return
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
            return

def render_screen():
    if graphics_screen is None:
        return
    graphics_screen.fill((0, 0, 0))
    width = 64 if get_flag_value(0) in (1, 2, 3) else 128 if get_flag_value(0) in (4, 5, 6) else 0
    if width == 0:
        pygame.display.flip()
        return
    for x in range(width):
        for y in range(64):
            if get_bit(framebuffer, x + y * width):
                pygame.draw.rect(graphics_screen, (255, 255, 255), (x * 10, y * 10, 10, 10))
    pygame.display.flip()
    for addr in range(len(framebuffer) * 8):
        set_bit(framebuffer, addr, 0)

def update_keyboard():
    global keyboard_state
    if pygame is None or graphics_screen is None:
        return
    pygame.event.pump()
    keys = pygame.key.get_pressed()
    keyboard_state = (
        keys[pygame.K_UP] |
        keys[pygame.K_DOWN] << 1 |
        keys[pygame.K_LEFT] << 2 |
        keys[pygame.K_RIGHT] << 3 |
        keys[pygame.K_c] << 4 |
        keys[pygame.K_v] << 5 |
        keys[pygame.K_BACKSPACE] << 6 |
        keys[pygame.K_RETURN] << 7
    )

while running and pc < len(bytecode):
    opbyte = bytecode[pc]
    pc += 1
    cmd = opcode_by_byte.get(opbyte)
    if cmd is None:
        print(f"Unknown opcode 0x{opbyte:02X} at position {pc - 1}, halting.")
        break

    sizes = op_codes[cmd]["sizes"]
    operands, pc = read_operands(bytecode, pc, sizes)

    match cmd:
        case "nop":
            pass

        case "flag":
            flag, val = operands
            if flag == 0:
                if val == 1:
                    if graphics_mode == 0:
                        initialize_graphics()
                if val == 0:
                    if pygame is not None and graphics_mode != 0:
                        shutdown_graphics()
                graphics_mode = val
            elif flag == 1:
                keyboard_mode = val
            set_bits(flags, flag * 4, val, 4)

        case "isflagsupported":
            flag, val, r = operands
            if flag in (0, 1) and val == 1:
                ensure_pygame()  # need to actually check pygame availability to answer this
            reg[r] = supportedFlags[flag][val]

        case "setbyte":
            addr, byte = operands
            mem[addr] = byte & 0xFF

        case "write":
            addr, text = operands
            for i, ch in enumerate(text):
                mem[(addr + i)] = ord(ch) & 0xFF

        case "outc":
            addr, = operands
            print(chr(mem[addr]), end="")

        case "out":
            addr, = operands
            print(mem[addr], end="")

        case "input":
            chars, addr = operands
            data = input("")
            for i in range(min(chars, len(data))):
                mem[(addr + i)] = ord(data[i]) & 0xFF

        case "print":
            text, = operands
            print(text)

        case "printn":
            text, = operands
            print(text, end="")

        case "printv":
            chars, addr = operands
            for i in range(chars):
                print(chr(mem[(addr + i)]), end="")
            print()

        case "printvn":
            chars, addr = operands
            for i in range(chars):
                print(chr(mem[(addr + i)]), end="")

        case "cur":
            print(pc)

        case "memory":
            columns = 16
            print("     |" + " ".join(f"{i:02x}" for i in range(columns)))
            print("-" * (6 + columns * 3))
            for row in range(0, len(mem), columns):
                chunk = mem[row:row + columns]
                hexed = " ".join(f"{b:02x}" for b in chunk)
                print(f"{row:<5}|{hexed}")

        case "flags":
            for i in range(16 * 16 * 4):
                print(get_bit(flags, i), end="")
            print()

        case "setpc":
            pos, = operands
            pc = pos

        case "subroutine":
            pos, = operands
            nextreturn = pc
            pc = pos

        case "return":
            pc = nextreturn

        case "sleep":
            ms, = operands
            time.sleep(ms / 1000)

        case "refreshscreen":
            update_graphics()  # pump window events (incl. QUIT) only when a frame is actually presented
            if running:
                render_screen()

        case "romread8":
            ra, rd = operands
            reg[rd] = rom_read(bytecode, reg[ra], 1)

        case "romread16":
            ra, rd = operands
            reg[rd] = rom_read(bytecode, reg[ra], 2)

        case "romread32":
            ra, rd = operands
            reg[rd] = rom_read(bytecode, reg[ra], 4)

        case "setpixel":
            rx, ry, rc = operands
            x, y, color = reg[rx], reg[ry], reg[rc]
            mode = get_flag_value(0)
            width = 128 if mode in (4, 5, 6) else 64
            target = x + y * width
            set_bit(framebuffer, target, 1 if color else 0)

        case "getpixel":
            rx, ry, rdst = operands
            x, y = reg[rx], reg[ry]
            mode = get_flag_value(0)
            width = 128 if mode in (4, 5, 6) else 64
            source = x + y * width
            reg[rdst] = get_bit(framebuffer, source)

        case "getkeyboard":
            r, = operands
            update_keyboard()  # only poll the real keyboard when a program actually asks for it
            reg[r] = keyboard_state

        case "gettime":
            r, = operands
            reg[r] = int(time.time() * 1000) % (1 << 32)  # time measured in milliseconds

        case "getmemsize":
            r, = operands
            reg[r] = len(mem)

        case "getflag":
            flag_index, r = operands
            reg[r] = get_flag_value(flag_index)

        case "randreg":
            min_val, max_val, r = operands
            reg[r] = random.randrange(min_val, max_val)

        case "setreg":
            r, val = operands
            reg[r] = val % (1 << 32)

        case "movreg":
            dst, src = operands
            reg[dst] = reg[src]

        case "loadreg8":
            r, addr = operands
            reg[r] = get_val(mem, addr, 1)

        case "loadreg16":
            r, addr = operands
            reg[r] = get_val(mem, addr, 2)

        case "loadreg32":
            r, addr = operands
            reg[r] = get_val(mem, addr, 4)

        case "storereg8":
            r, addr = operands
            set_val(mem, addr, reg[r] & 0xFF, 1)

        case "storereg16":
            r, addr = operands
            set_val(mem, addr, reg[r] & 0xFFFF, 2)

        case "storereg32":
            r, addr = operands
            set_val(mem, addr, reg[r] & 0xFFFFFFFF, 4)

        case "addreg":
            r1, r2, dst = operands
            reg[dst] = (reg[r1] + reg[r2]) % (1 << 32)

        case "subreg":
            r1, r2, dst = operands
            reg[dst] = (reg[r1] - reg[r2]) % (1 << 32)

        case "mulreg":
            r1, r2, dst = operands
            reg[dst] = (reg[r1] * reg[r2]) % (1 << 32)

        case "divreg":
            r1, r2, dst = operands
            if reg[r2] != 0:
                reg[dst] = reg[r1] // reg[r2]

        case "modreg":
            r1, r2, dst = operands
            if reg[r2] != 0:
                reg[dst] = reg[r1] % reg[r2]

        case "addregv":
            r1, val, dst = operands
            reg[dst] = (reg[r1] + val) % (1 << 32)

        case "subregv":
            r1, val, dst = operands
            reg[dst] = (reg[r1] - val) % (1 << 32)

        case "mulregv":
            r1, val, dst = operands
            reg[dst] = (reg[r1] * val) % (1 << 32)

        case "compreg":
            r1, r2, dst = operands
            if reg[r1] == reg[r2]:
                reg[dst] = 0
            elif reg[r1] > reg[r2]:
                reg[dst] = 1
            else:
                reg[dst] = 2

        case "ifreg":
            r, pos = operands
            if reg[r] != 0:
                pc = pos

        case "ifnotreg":
            r, pos = operands
            if reg[r] == 0:
                pc = pos

        case "ifsubreg":
            r, pos = operands
            if reg[r] != 0:
                nextreturn = pc
                pc = pos

        case "ifnotsubreg":
            r, pos = operands
            if reg[r] == 0:
                nextreturn = pc
                pc = pos

        case "notreg":
            r1, dst = operands
            reg[dst] = (~reg[r1]) & 0xFFFFFFFF

        case "orreg":
            r1, r2, dst = operands
            reg[dst] = reg[r1] | reg[r2]

        case "andreg":
            r1, r2, dst = operands
            reg[dst] = reg[r1] & reg[r2]

        case "norreg":
            r1, r2, dst = operands
            reg[dst] = (~(reg[r1] | reg[r2])) & 0xFFFFFFFF

        case "nandreg":
            r1, r2, dst = operands
            reg[dst] = (~(reg[r1] & reg[r2])) & 0xFFFFFFFF

        case "xorreg":
            r1, r2, dst = operands
            reg[dst] = reg[r1] ^ reg[r2]

        case "xnorreg":
            r1, r2, dst = operands
            reg[dst] = (~(reg[r1] ^ reg[r2])) & 0xFFFFFFFF

        case "shlreg":
            r1, r2, dst = operands
            reg[dst] = (reg[r1] << reg[r2]) & 0xFFFFFFFF

        case "shrreg":
            r1, r2, dst = operands
            reg[dst] = reg[r1] >> reg[r2]

        case "loadregi8":
            ra, rd = operands
            reg[rd] = get_val(mem, reg[ra], 1)

        case "loadregi16":
            ra, rd = operands
            reg[rd] = get_val(mem, reg[ra], 2)

        case "loadregi32":
            ra, rd = operands
            reg[rd] = get_val(mem, reg[ra], 4)

        case "storeregi8":
            ra, rv = operands
            set_val(mem, reg[ra], reg[rv] & 0xFF, 1)

        case "storeregi16":
            ra, rv = operands
            set_val(mem, reg[ra], reg[rv] & 0xFFFF, 2)

        case "storeregi32":
            ra, rv = operands
            set_val(mem, reg[ra], reg[rv] & 0xFFFFFFFF, 4)

        case "eqreg":
            r1, r2, dst = operands
            reg[dst] = 1 if reg[r1] == reg[r2] else 0

        case "neqreg":
            r1, r2, dst = operands
            reg[dst] = 1 if reg[r1] != reg[r2] else 0

        case "ltreg":
            r1, r2, dst = operands
            reg[dst] = 1 if reg[r1] < reg[r2] else 0

        case "gtreg":
            r1, r2, dst = operands
            reg[dst] = 1 if reg[r1] > reg[r2] else 0

        case "lereg":
            r1, r2, dst = operands
            reg[dst] = 1 if reg[r1] <= reg[r2] else 0

        case "gereg":
            r1, r2, dst = operands
            reg[dst] = 1 if reg[r1] >= reg[r2] else 0

        case "outreg":
            r, = operands
            print(reg[r], end="")

        case "exit":
            running = False

if pygame is not None:
    pygame.quit()