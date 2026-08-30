'''
unilang bytecode VM by AzizBgBoss
https://github.com/AzizBgBoss/unilang

This VM executes compiled unilang bytecode (produced by a separate converter/assembler or written by hand if you hate yourself enough).

Operand encoding conventions (all big-endian):
  addr  -> 4 bytes (unsigned int)
  size  -> 1 byte  (bits, 0-255)
  val   -> 4 bytes (unsigned int)
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

0x00 (nop)              none                                           - do nothing (padding/alignment)
0x01 (flag)             flag(1), val(1)                                - set the value of a flag with a certain index to a 4-bit value
0x02 (isflagsupported)  flag(1), val(1), addr(4), size(1)              - check if a flag with a certain index is supported (changes based on platforms)
0x03 (mem)              addr(4), size(1), val(4)                       - set the value of a memory address with a certain size
0x04 (setmem)           addr1(4), size1(1), addr2(4), size2(1)         - copy the value of the variable at the address stored in addr1 to the variable at the address stored in addr2. the size of the addresses stored in addr1 and addr2 must be 32, the size of the target variables can change
0x05 (write)            text(0), addr(4), size(1)                      - write text to a memory address with a certain size
0x06 (outc)             addr(4), size(1)                               - output the character of a memory address with a certain size
0x07 (out)              addr(4), size(1)                               - output the value of a memory address with a certain size
0x08 (input)            chars(4), addr(4), size(1)                     - read a certain amount of characters and store them in memory
0x09 (print)            text(0)                                        - print text
0x0A (printn)           text(0)                                        - print text without a newline
0x0B (printv)           chars(4), addr(4), size(1)                     - print a certain amount of characters from memory
0x0C (printvn)          chars(4), addr(4), size(1)                     - print a certain amount of characters from memory without a newline
0x0D (rand)             min(4), max(4), addr(4), size(1)              - set a memory address with a certain size to a random value between min and max (exclusive)
0x0E (add)              val(4), addr1(4), size1(1), addr2(4)           - add a value to a memory address with a certain size and store it in another memory address
0x0F (addv)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - add two memory addresses with certain sizes and store it in another memory address
0x10 (sub)              val(4), addr1(4), size1(1), addr2(4)           - subtract a value from a memory address with a certain size and store it in another memory address
0x11 (subv)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - subtract two memory addresses with certain sizes and store it in another memory address
0x12 (mul)              val(4), addr1(4), size1(1), addr2(4)           - multiply a value with a memory address with a certain size and store it in another memory address
0x13 (mulv)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - multiply two memory addresses with certain sizes and store it in another memory address
0x14 (div)              val(4), addr1(4), size1(1), addr2(4)           - divide a memory address with a certain size by a value and store it in another memory address
0x15 (divv)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - divide two memory addresses with certain sizes and store it in another memory address
0x16 (mod)              val(4), addr1(4), size1(1), addr2(4)           - get the modulus of a memory address with a certain size by a value and store it in another memory address
0x17 (modv)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - get the modulus of two memory addresses with certain sizes and store it in another memory address
0x18 (cur)              none                                           - print the current position in the bytecode
0x19 (memory)           none                                           - print the current state of memory
0x1A (flags)            none                                           - print the current state of flags
0x1B (setpc)            pos(4)                                         - set the program counter to a fixed bytecode position
0x1F (compare)          val(4), addr1(4), size1(1)                     - compare a value to a memory address with a certain size and store the result in a special memory address (0 = equal, 1 = val > addr1, 2 = val < addr1)
0x20 (comparev)         addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - compare two memory addresses with certain sizes and store the result in a memory address (0 = equal, 1 = addr1 > addr2, 2 = addr1 < addr2)
0x21 (isequal)          val(4), addr1(4), size1(1), addr2(4)           - check if a value is equal to a memory address with a certain size and store the result (0 or 1) in another memory address (size 1)
0x22 (not)              addr1(4), size1(1), addr2(4), size2(1)         - flip the bits of a memory address with a certain size and store it in another memory address
0x23 (or)               addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise OR on two memory addresses and store it in another memory address
0x24 (and)              addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise AND on two memory addresses and store it in another memory address
0x25 (nor)              addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise NOR on two memory addresses and store it in another memory address
0x26 (nand)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise NAND on two memory addresses and store it in another memory address
0x27 (xor)              addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise XOR on two memory addresses and store it in another memory address
0x28 (xnor)             addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise XNOR on two memory addresses and store it in another memory address
0x29 (shl)              addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise shift left on a memory address and store it in another memory address
0x2A (shr)              addr1(4), size1(1), addr2(4), size2(1), addr3(4), size3(1) - perform a bitwise shift right on a memory address and store it in another memory address
0x2B (subroutine)       pos(4)                                         - call a subroutine at a fixed bytecode position
0x2C (return)           none                                           - return from a subroutine
0x2D (if)               addr(4), size(1), pos(4)                       - if the value of a memory address is not 0, jump to a fixed bytecode position
0x2E (ifnot)            addr(4), size(1), pos(4)                       - if the value of a memory address is 0, jump to a fixed bytecode position
0x2F (ifsubroutine)     addr(4), size(1), pos(4)                       - if the value of a memory address is not 0, call a subroutine at a fixed bytecode position
0x30 (ifnotsubroutine)  addr(4), size(1), pos(4)                       - if the value of a memory address is 0, call a subroutine at a fixed bytecode position
0x31 (sleep)            ms(4)                                          - sleep for a certain amount of milliseconds
0x32 (gettime)          addr(4), size(1)                               - get the current time and store it in a memory address
0x33 (getmemsize)       addr(4), size(1)                               - get the size of memory in bits and store it in a memory address
0xFF (exit)             none                                           - exit the program
'''

import random
import sys
import time

# commandname: {"byte": the opcode byte, "operands": number of operands, "sizes": byte width of each operand (0 = length-prefixed text)}
op_codes = {
    "nop":              {"byte": 0x00, "operands": 0, "sizes": []},
    "flag":             {"byte": 0x01, "operands": 2, "sizes": [1, 1]},              # flag, val
    "isflagsupported":  {"byte": 0x02, "operands": 4, "sizes": [1, 1, 4, 1]},        # flag, val, addr, size
    "mem":              {"byte": 0x03, "operands": 3, "sizes": [4, 1, 4]},           # addr, size, val
    "setmem":           {"byte": 0x04, "operands": 4, "sizes": [4, 1, 4, 1]},        # addr1, size1, addr2, size2
    "write":            {"byte": 0x05, "operands": 3, "sizes": [0, 4, 1]},           # text, addr, size
    "outc":             {"byte": 0x06, "operands": 2, "sizes": [4, 1]},              # addr, size
    "out":              {"byte": 0x07, "operands": 2, "sizes": [4, 1]},              # addr, size
    "input":            {"byte": 0x08, "operands": 3, "sizes": [4, 4, 1]},           # chars, addr, size
    "print":            {"byte": 0x09, "operands": 1, "sizes": [0]},                 # text
    "printn":           {"byte": 0x0A, "operands": 1, "sizes": [0]},                 # text
    "printv":           {"byte": 0x0B, "operands": 3, "sizes": [4, 4, 1]},           # chars, addr, size
    "printvn":          {"byte": 0x0C, "operands": 3, "sizes": [4, 4, 1]},           # chars, addr, size
    "rand":             {"byte": 0x0D, "operands": 4, "sizes": [4, 4, 4, 1]},        # min, max, addr, size
    "add":              {"byte": 0x0E, "operands": 4, "sizes": [4, 4, 1, 4]},        # val, addr1, size1, addr2
    "addv":             {"byte": 0x0F, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},  # addr1,size1,addr2,size2,addr3,size3
    "sub":              {"byte": 0x10, "operands": 4, "sizes": [4, 4, 1, 4]},
    "subv":             {"byte": 0x11, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "mul":              {"byte": 0x12, "operands": 4, "sizes": [4, 4, 1, 4]},
    "mulv":             {"byte": 0x13, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "div":              {"byte": 0x14, "operands": 4, "sizes": [4, 4, 1, 4]},
    "divv":             {"byte": 0x15, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "mod":              {"byte": 0x16, "operands": 4, "sizes": [4, 4, 1, 4]},
    "modv":             {"byte": 0x17, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "cur":              {"byte": 0x18, "operands": 0, "sizes": []},
    "memory":           {"byte": 0x19, "operands": 0, "sizes": []},
    "flags":            {"byte": 0x1A, "operands": 0, "sizes": []},
    "setpc":            {"byte": 0x1B, "operands": 1, "sizes": [4]},                 # pos (fixed)
    "compare":          {"byte": 0x1F, "operands": 4, "sizes": [4, 4, 1, 1]},
    "comparev":         {"byte": 0x20, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "isequal":          {"byte": 0x21, "operands": 4, "sizes": [4, 4, 1, 4]},
    "not":              {"byte": 0x22, "operands": 4, "sizes": [4, 1, 4, 1]},
    "or":               {"byte": 0x23, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "and":              {"byte": 0x24, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "nor":              {"byte": 0x25, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "nand":             {"byte": 0x26, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "xor":              {"byte": 0x27, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "xnor":             {"byte": 0x28, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "shl":              {"byte": 0x29, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "shr":              {"byte": 0x2A, "operands": 6, "sizes": [4, 1, 4, 1, 4, 1]},
    "subroutine":       {"byte": 0x2B, "operands": 1, "sizes": [4]},                 # pos (fixed)
    "return":           {"byte": 0x2C, "operands": 0, "sizes": []},
    "if":               {"byte": 0x2D, "operands": 3, "sizes": [4, 1, 4]},           # addr, size, pos (fixed)
    "ifnot":            {"byte": 0x2E, "operands": 3, "sizes": [4, 1, 4]},
    "ifsubroutine":     {"byte": 0x2F, "operands": 3, "sizes": [4, 1, 4]},
    "ifnotsubroutine":  {"byte": 0x30, "operands": 3, "sizes": [4, 1, 4]},
    "sleep":            {"byte": 0x31, "operands": 1, "sizes": [4]},                 # ms
    "gettime":          {"byte": 0x32, "operands": 2, "sizes": [4, 1]},              # addr, size
    "getmemsize":       {"byte": 0x33, "operands": 2, "sizes": [4, 1]},              # addr, size
    "exit":             {"byte": 0xFF, "operands": 0, "sizes": []},
}

# reverse lookup: opcode byte -> command name
opcode_by_byte = {v["byte"]: k for k, v in op_codes.items()}

memsize = 1024 * 8
supportedFlags = [[0] * 16] * 16

supportedFlags[0][0] = 1 # disabling graphics is ofc supported
supportedFlags[1][0] = 1 # disabling keyboard is ofc supported

try:
    import pygame
    if memsize >= 64 * 64:
        supportedFlags[0][1] = 1 # 64x64@1
        if memsize == 64 * 64:
            print("Warning: The memory size fits perfectly for 64x64@1 graphics. But will not allow for more variables without graphical issues.")
    else:
        supportedFlags[0][1] = 0
        print(f"Graphics not supported because the memory size is too small ({memsize} bits, 64x64@1 requires at least {64 * 64} bits).")
except:
    pygame = None
    print("Pygame not installed. Graphics and keyboard (flag 0-1) will not be supported.")

print(f"Starting unilang bytecode VM with {memsize} bits of memory ({memsize // 8} bytes)...\n")

def get_bit(mem, addr):
    return (mem[addr // 8] >> (addr % 8)) & 1

def set_bit(mem, addr, val):
    if val:
        mem[addr // 8] |= (1 << (addr % 8))
    else:
        mem[addr // 8] &= ~(1 << (addr % 8))

def set_val(mem, addr, val, size):
    val %= 2 ** size
    for i in range(size):
        set_bit(mem, addr + i, val & (1 << (size - 1 - i)))

def get_val(mem, addr, size):
    val = 0
    for i in range(size):
        val |= get_bit(mem, addr + i) << (size - i - 1)
    return val

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

nextreturn = 0
graphics_screen = None
usedram = 0 # the amount of bits used by special flags from the right
keyboard_state = 0

def initialize_graphics():
    global graphics_screen, usedram
    if pygame is not None and graphics_screen is None:
        pygame.init()
        graphics_screen = pygame.display.set_mode((64 * 10, 64 * 10))
        supportedFlags[1][1] = 1 # NES is now supported
        usedram += 64 * 64

def shutdown_graphics():
    global graphics_screen, usedram, keyboard_state
    if pygame is not None and graphics_screen is not None:
        pygame.display.quit()
        graphics_screen = None
        keyboard_state = 0
        supportedFlags[1][1] = 0
        set_val(flags, 1 * 4, 0, 4)
        usedram -= 64 * 64

def update_graphics():
    global running, keyboard_state
    if graphics_screen is None:
        return

    key_masks = {
        pygame.K_UP: 0x80,
        pygame.K_DOWN: 0x40,
        pygame.K_LEFT: 0x20,
        pygame.K_RIGHT: 0x10,
        pygame.K_c: 0x08,
        pygame.K_v: 0x04,
        pygame.K_BACKSPACE: 0x02,
        pygame.K_RETURN: 0x01,
    }
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
            return
        if event.type == pygame.KEYDOWN:
            keyboard_state |= key_masks.get(event.key, 0)
        if event.type == pygame.KEYUP:
            keyboard_state &= ~key_masks.get(event.key, 0)

    graphics_screen.fill((0, 0, 0))
    for x in range(64):
        for y in range(64):
            if get_bit(mem, memsize - x - y * 64 - 1):
                pygame.draw.rect(graphics_screen, (255, 255, 255), (x * 10, y * 10, 10, 10))
    pygame.display.flip()

def update_keyboard():
    global keyboard_state
    if pygame is None or graphics_screen is None:
        return

    pygame.event.pump()
    keys = pygame.key.get_pressed()
    held_state = (
        keys[pygame.K_UP] |
        keys[pygame.K_DOWN] << 1 |
        keys[pygame.K_LEFT] << 2 |
        keys[pygame.K_RIGHT] << 3 |
        keys[pygame.K_c] << 4 |
        keys[pygame.K_v] << 5 |
        keys[pygame.K_BACKSPACE] << 6 |
        keys[pygame.K_RETURN] << 7
    )
    keyboard_state |= held_state
    set_val(mem, memsize - usedram - 8, keyboard_state, 8)

while running and pc < len(bytecode):
    if get_val(flags, 0 * 4, 4) == 1:
        update_graphics()
        if not running:
            break
    if get_val(flags, 1 * 4, 4) == 1:
        update_keyboard()

    opbyte = bytecode[pc]
    pc += 1
    cmd = opcode_by_byte.get(opbyte)
    if cmd is None:
        print(f"Unknown opcode 0x{opbyte:02X} at position {pc - 1}, halting.")
        break

    sizes = op_codes[cmd]["sizes"]
    operands, pc = read_operands(bytecode, pc, sizes)

    if cmd == "nop":
        pass

    elif cmd == "flag":
        flag, val = operands
        if flag == 0:
            if val == 1:
                if get_val(flags, flag * 4, 4) == 0:
                    initialize_graphics()
            if val == 0:
                if pygame is not None and get_val(flags, flag * 4, 4) != 0:
                    shutdown_graphics()
        set_val(flags, flag * 4, val, 4)

    elif cmd == "isflagsupported":
        flag, val, addr, size = operands
        set_val(mem, addr, supportedFlags[flag][val], size)

    elif cmd == "mem":
        addr, size, val = operands
        set_val(mem, addr, val, size)

    elif cmd == "setmem":
        addr1, size1, addr2, size2 = operands
        set_val(mem, get_val(mem, addr2, 32), get_val(mem, get_val(mem, addr1, 32), size1), size2)

    elif cmd == "write":
        text, addr, size = operands
        for i in range(len(text)):
            set_val(mem, addr + i * size, ord(text[i]), size)

    elif cmd == "outc":
        addr, size = operands
        print(chr(get_val(mem, addr, size)), end="")

    elif cmd == "out":
        addr, size = operands
        print(get_val(mem, addr, size), end="")

    elif cmd == "input":
        chars, addr, size = operands
        data = input("")
        for i in range(min(chars, len(data))):
            set_val(mem, addr + i * size, ord(data[i]), size)

    elif cmd == "print":
        text, = operands
        print(text)

    elif cmd == "printn":
        text, = operands
        print(text, end="")

    elif cmd == "printv":
        chars, addr, size = operands
        for i in range(chars):
            print(chr(get_val(mem, addr + i * size, size)), end="")
        print()

    elif cmd == "printvn":
        chars, addr, size = operands
        for i in range(chars):
            print(chr(get_val(mem, addr + i * size, size)), end="")

    elif cmd == "rand":
        min_val, max_val, addr, size = operands
        set_val(mem, addr, random.randrange(min_val, max_val), size)

    elif cmd == "add":
        val, addr1, size1, addr2 = operands
        # note: size2 assumed equal to size1 in this encoding; adjust op_codes sizes if you need distinct size2
        set_val(mem, addr2, val + get_val(mem, addr1, size1), size1)

    elif cmd == "addv":
        addr1, size1, addr2, size2, addr3, size3 = operands
        set_val(mem, addr3, get_val(mem, addr1, size1) + get_val(mem, addr2, size2), size3)

    elif cmd == "sub":
        val, addr1, size1, addr2 = operands
        set_val(mem, addr2, get_val(mem, addr1, size1) - val, size1)

    elif cmd == "subv":
        addr1, size1, addr2, size2, addr3, size3 = operands
        set_val(mem, addr3, get_val(mem, addr1, size1) - get_val(mem, addr2, size2), size3)

    elif cmd == "mul":
        val, addr1, size1, addr2 = operands
        set_val(mem, addr2, val * get_val(mem, addr1, size1), size1)

    elif cmd == "mulv":
        addr1, size1, addr2, size2, addr3, size3 = operands
        set_val(mem, addr3, get_val(mem, addr1, size1) * get_val(mem, addr2, size2), size3)

    elif cmd == "div":
        val, addr1, size1, addr2 = operands
        if val != 0:
            set_val(mem, addr2, get_val(mem, addr1, size1) // val, size1)

    elif cmd == "divv":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val2 = get_val(mem, addr2, size2)
        if val2 != 0:
            set_val(mem, addr3, get_val(mem, addr1, size1) // val2, size3)

    elif cmd == "mod":
        val, addr1, size1, addr2 = operands
        if val != 0:
            set_val(mem, addr2, get_val(mem, addr1, size1) % val, size1)

    elif cmd == "modv":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val2 = get_val(mem, addr2, size2)
        if val2 != 0:
            set_val(mem, addr3, get_val(mem, addr1, size1) % val2, size3)

    elif cmd == "cur":
        print(pc)

    elif cmd == "memory":
        columns = 16
        print("    |" + "".join(hex(i) for i in range(columns)))
        print("-" * (5 + columns))
        for row in range(0, memsize, columns):
            bits = "".join(str(get_bit(mem, i)) for i in range(row, min(row + columns, memsize)))
            print(f"{row // columns:<4}|{bits:<{columns}}")

    elif cmd == "flags":
        for i in range(16 * 16 * 4):
            print(get_bit(flags, i), end="")
        print()

    elif cmd == "setpc":
        pos, = operands
        pc = pos

    elif cmd == "compare":
        val, addr1, size1, _pad = operands
        if val == get_val(mem, addr1, size1):
            set_val(mem, 0, 0, 2)
        elif val > get_val(mem, addr1, size1):
            set_val(mem, 0, 1, 2)
        else:
            set_val(mem, 0, 2, 2)

    elif cmd == "comparev":
        addr1, size1, addr2, size2, addr3, size3 = operands
        if get_val(mem, addr1, size1) == get_val(mem, addr2, size2):
            set_val(mem, addr3, 0, size3)
        elif get_val(mem, addr1, size1) > get_val(mem, addr2, size2):
            set_val(mem, addr3, 1, size3)
        else:
            set_val(mem, addr3, 2, size3)

    elif cmd == "isequal":
        val, addr1, size1, addr2 = operands
        if val == get_val(mem, addr1, size1):
            set_val(mem, addr2, 1, 1)
        else:
            set_val(mem, addr2, 0, 1)

    elif cmd == "not":
        addr1, size1, addr2, size2 = operands
        val1 = get_val(mem, addr1, size1)
        result = val1 ^ ((1 << size1) - 1)
        set_val(mem, addr2, result, size2)

    elif cmd == "or":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        set_val(mem, addr3, val1 | val2, size3)

    elif cmd == "and":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        set_val(mem, addr3, val1 & val2, size3)

    elif cmd == "nor":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        result = ~(val1 | val2) & ((1 << max(size1, size2)) - 1)
        set_val(mem, addr3, result, size3)

    elif cmd == "nand":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        result = ~(val1 & val2) & ((1 << max(size1, size2)) - 1)
        set_val(mem, addr3, result, size3)

    elif cmd == "xor":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        set_val(mem, addr3, val1 ^ val2, size3)

    elif cmd == "xnor":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        result = ~(val1 ^ val2) & ((1 << max(size1, size2)) - 1)
        set_val(mem, addr3, result, size3)

    elif cmd == "shl":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        shift = get_val(mem, addr2, size2)
        result = (val1 << shift) % (2 ** size1)
        set_val(mem, addr3, result, size3)

    elif cmd == "shr":
        addr1, size1, addr2, size2, addr3, size3 = operands
        val1 = get_val(mem, addr1, size1)
        shift = get_val(mem, addr2, size2)
        result = val1 >> shift
        set_val(mem, addr3, result, size3)

    elif cmd == "subroutine":
        pos, = operands
        nextreturn = pc
        pc = pos

    elif cmd == "return":
        pc = nextreturn

    elif cmd == "if":
        addr, size, pos = operands
        if get_val(mem, addr, size) != 0:
            pc = pos

    elif cmd == "ifnot":
        addr, size, pos = operands
        if get_val(mem, addr, size) == 0:
            pc = pos

    elif cmd == "ifsubroutine":
        addr, size, pos = operands
        if get_val(mem, addr, size) != 0:
            nextreturn = pc
            pc = pos

    elif cmd == "ifnotsubroutine":
        addr, size, pos = operands
        if get_val(mem, addr, size) == 0:
            nextreturn = pc
            pc = pos

    elif cmd == "sleep":
        ms, = operands
        time.sleep(ms / 1000)

    elif cmd == "gettime":
        addr, size = operands
        set_val(mem, addr, int(time.time() * 1000), size)

    elif cmd == "getmemsize":
        addr, size = operands
        set_val(mem, addr, memsize, size)

    elif cmd == "exit":
        running = False

if pygame is not None:
    pygame.quit()