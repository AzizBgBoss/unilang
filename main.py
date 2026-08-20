'''
unilang by AzizBgBoss
https://github.com/AzizBgBoss/unilang

Tips:
- Use # to add comments that are ignored by the interpreter
- Sizes are specified in bits
- 1 bit variables are good for boolean values, 8 bit variables are good for characters, and 32 bit variables are good for integers
- 2-bit ranges are 0-3, 4-bit ranges are 0-15, 8-bit ranges are 0-255, 16-bit ranges are 0-65535, and 32-bit ranges are 0-4294967295
- flags are special 1-bit variables that can be used for custom features
- flag 0 (graphics) (this will consume the last n bits of memory):
0 - disable graphics
1 - enable monochrome graphics (64x64) (1 x 64 x 64 = 4096 bits = 512 bytes)
2 - enable 4-shade grayscale graphics (64x64) (4 x 64 x 64 = 16384 bits = 2048 bytes)
3 - enable 8-bit color graphics (64x64) (8 x 64 x 64 = 32768 bits = 4096 bytes)
4 - enable monochrome graphics (128x64) (1 x 128 x 64 = 8192 bits = 1024 bytes)
5 - enable 4-shade grayscale graphics (128x64) (4 x 128 x 64 = 32768 bits = 4096 bytes)
6 - enable 8-bit color graphics (128x64) (8 x 128 x 64 = 65536 bits = 8192 bytes)

Commands:
#...# - comment (stops until the next #)
flag[n]=val - set the value of a flag with a certain index to a 4-bit value
isflagsupported(n, val, addr:size) - check if a flag with a certain index is supported (changes based on platforms)
mem[addr:size]=val - set the value of a memory address with a certain size
setmem(addr1:size1, addr2:size2) - set the value of a memory address with a certain size to the value of another memory address with a certain size
outc(addr:size) - output the character of a memory address with a certain size
out(addr:size) - output the value of a memory address with a certain size
input(chars, addr:size) - read a certain amount of characters and store them in memory
print("text") - print text
printn("text") - print text without a newline
printv(chars, addr:size) - print a certain amount of characters from memory
printvn(chars, addr:size) - print a certain amount of characters from memory without a newline
rand(addr:size) - set a memory address with a certain size to a random value
add(val, addr1:size1, addr2:size2) - add a value to a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
addv(addr1:size1, addr2:size2, addr3:size3) - add two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
sub(val, addr1:size1, addr2:size2) - subtract a value from a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
subv(addr1:size1, addr2:size2, addr3:size3) - subtract two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
mul(val, addr1:size1, addr2:size2) - multiply a value with a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
mulv(addr1:size1, addr2:size2, addr3:size3) - multiply two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
div(val, addr1:size1, addr2:size2) - divide a memory address with a certain size by a value and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
divv(addr1:size1, addr2:size2, addr3:size3) - divide two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
mod(val, addr1:size1, addr2:size2) - get the modulus of a memory address with a certain size by a value and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
modv(addr1:size1, addr2:size2, addr3:size3) - get the modulus of two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
cur - print the current position in the program
memory - print the current state of memory
flags - print the current state of flags
setcur(pos) - set the current position in the program
compare(val, addr1:size1, addr2:size2) - compare a value to a memory address with a certain size and store the result in a special memory address (0 = equal, 1 = val > addr2, 2 = val < addr2)
comparev(addr1:size1, addr2:size2, addr3:size3) - compare two memory addresses with certain sizes and store the result in a special memory address (0 = equal, 1 = addr1 > addr2, 2 = addr1 < addr2)
isequal(val, addr1:size1, addr2:size2) - check if a value is equal to a memory address with a certain size and store the result in a special memory address (0 = not equal, 1 = equal)
not(addr1:size1, addr2:size2) - flip the bits of a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
or(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise OR operation on two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
and(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise AND operation on two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
nor(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise NOR operation on two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
nand(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise NAND operation on two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
xor(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise XOR operation on two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
xnor(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise XNOR operation on two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
shl(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise shift left operation on a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
shr(addr1:size1, addr2:size2, addr3:size3) - perform a bitwise shift right operation on a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
subroutine(cur) - call a subroutine at a certain position in the program
return - return from a subroutine
if(addr:size, cur) - if the value of a memory address with a certain size is not 0, jump to a certain position in the program
ifnot(addr:size, cur) - if the value of a memory address with a certain size is 0, jump to a certain position in the program
ifsubroutine(addr:size, cur) - if the value of a memory address with a certain size is not 0, call a subroutine at a certain position in the program
ifnotsubroutine(addr:size, cur) - if the value of a memory address with a certain size is 0, call a subroutine at a certain position in the program
sleep(ms) - sleep for a certain amount of milliseconds
exit - exit the program
'''

import random
import sys
import time

memsize = 1024 * 4
supportedFlags = [[0] * 16] * 16

supportedFlags[0][0] = 1 # disabling graphics is ofc supported
supportedFlags[0][1] = 1 # 64x64@1

print(f"Starting unilang interpreter with {memsize} bits of memory ({memsize // 8} bytes)...\n")

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
        
def readuntil(program, cur, stop):
    tok = ""
    while cur < len(program):
        if program[cur] not in stop:
            tok += program[cur]
            cur += 1
        else:
            return tok
    return tok
            
def readuntilcutoff(program, cur):
    tok = ""
    while cur < len(program):
        if program[cur] not in cutoffs:
            tok += program[cur]
            cur += 1
        else:
            return tok
    return tok

# check for -f flag in command line arguments
if len(sys.argv) > 1 and sys.argv[1] == "-f":
    with open(sys.argv[2], "r") as f:
        program = f.read()
else:
    program = input(">")

print(program + '\n')

cutoffs = "[];\n()"

running = True
cur = 0
tok = ""
mem = bytearray(memsize // 8)
flags = bytearray(16 * 16 // 8)

nextreturn = 0

import tkinter as tk

def on_close():
    global running
    running = False

while running and cur < len(program):
    if get_val(flags, 0, 4) == 1:
        try:
            root.update()
        except tk.TclError:
            running = False
            break
        canvas.delete("all")
        for x in range(64):
            for y in range(64):
                if get_bit(mem, memsize - x * 64 - y - 1):
                    canvas.create_rectangle(x*10, y*10, x*10+10, y*10+10, fill="white", outline="")
    if program[cur] == "#":
        tok = readuntil(program, cur, "#")
        cur += len(tok) + 1
        continue
    tok = readuntilcutoff(program, cur)
    cur += len(tok)
    if tok.strip() == "":
        cur += 1
        continue
    if tok == "flag":
        cur += 1
        tok = readuntil(program, cur, "]")
        cur += len(tok)
        flag = int(tok)

        cur += 2
        tok = readuntil(program, cur, ";\n")
        cur += len(tok)
        val = int(tok)

        cur += 2

        if flag == 0:
            if val == 1:
                if get_val(flags, flag * 4, 4) == 0:
                    root = tk.Tk()
                    root.protocol("WM_DELETE_WINDOW", lambda: on_close())
                    canvas = tk.Canvas(root, width=64*10, height=64*10, bg="black")
                    canvas.pack()
            if val == 0:
                if get_val(flags, flag * 4, 4) != 0:
                    root.destroy()
        set_val(flags, flag * 4, val, 4)
    if tok == "isflagsupported":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        flag = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok) 

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)

        cur += 2

        set_val(mem, addr, supportedFlags[flag][val], size)
    if tok == "mem":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)
        
        cur += 1
        tok = readuntil(program, cur, "]")
        cur += len(tok)
        size = int(tok)
        
        cur += 2
        tok = readuntil(program, cur, ";\n")
        cur += len(tok)
        val = int(tok)
        
        cur += 2
        
        set_val(mem, addr, val, size)
    if tok == "setmem":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)
        
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)
        
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)
        
        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)
        
        cur += 2
        
        set_val(mem, addr1, get_val(mem, addr2, size2), size1)
    if tok == "outc":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)
        
        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)
        
        cur += 2
        
        print(chr(get_val(mem, addr, size)), end = "")
    if tok == "out":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)
        
        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)
        
        cur += 2
        
        print(get_val(mem, addr, size), end = "")
    if tok == "input":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        chars = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)

        cur += 2

        data = input("")
        for i in range(min(chars, len(data))):
            set_val(mem, addr + i * size, ord(data[i]), size)
    if tok == "print":
        cur += 2
        tok = readuntil(program, cur, "\"")
        cur += len(tok)
        print(tok)

        cur += 3
    if tok == "printn":
        cur += 2
        tok = readuntil(program, cur, "\"")
        cur += len(tok)
        print(tok, end = "")

        cur += 3
    if tok == "printv":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        chars = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)

        cur += 2

        for i in range(chars):
            print(chr(get_val(mem, addr + i * size, size)), end = "")
        print()
    if tok == "printvn":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        chars = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)

        cur += 2

        for i in range(chars):
            print(chr(get_val(mem, addr + i * size, size)), end = "")
    if tok == "rand":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)

        cur += 2

        set_val(mem, addr, random.randint(0, 2 ** size - 1), size)
    if tok == "add":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        set_val(mem, addr2, val + get_val(mem, addr1, size1), size2)
    if tok == "addv":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        set_val(mem, addr3, get_val(mem, addr1, size1) + get_val(mem, addr2, size2), size3)
    if tok == "sub":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        set_val(mem, addr2, get_val(mem, addr1, size1) - val, size2)
    if tok == "subv":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        set_val(mem, addr3, get_val(mem, addr1, size1) - get_val(mem, addr2, size2), size3)
    if tok == "mul":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        set_val(mem, addr2, val * get_val(mem, addr1, size1), size2)
    if tok == "mulv":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        set_val(mem, addr3, get_val(mem, addr1, size1) * get_val(mem, addr2, size2), size3)
    if tok == "div":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        if val != 0:
            set_val(mem, addr2, get_val(mem, addr1, size1) // val, size2)
    if tok == "divv":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val2 = get_val(mem, addr2, size2)
        if val2 != 0:
            set_val(mem, addr3, get_val(mem, addr1, size1) // val2, size3)
    if tok == "mod":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        if val != 0:
            set_val(mem, addr2, get_val(mem, addr1, size1) % val, size2)
    if tok == "modv":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val2 = get_val(mem, addr2, size2)
        if val2 != 0:
            set_val(mem, addr3, get_val(mem, addr1, size1) % val2, size3)
    if tok == "cur":
        print(cur)
    if tok == "memory":
        for i in range(memsize):
            print(get_bit(mem, i), end = "")
        print()
    if tok == "flags":
        for i in range(16 * 16 * 4):
            print(get_bit(flags, i), end = "")
        print()
    if tok == "setcur":
        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        cur = int(tok)
    if tok == "compare":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        if val == get_val(mem, addr1, size1):
            set_val(mem, 0, 0, 2)
        elif val > get_val(mem, addr1, size1):
            set_val(mem, 0, 1, 2)
        else:
            set_val(mem, 0, 2, 2)
    if tok == "comparev":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        if get_val(mem, addr1, size1) == get_val(mem, addr2, size2):
            set_val(mem, addr3, 0, size3)
        elif get_val(mem, addr1, size1) > get_val(mem, addr2, size2):
            set_val(mem, addr3, 1, size3)
        else:
            set_val(mem, addr3, 2, size3)
    
    if tok == "isequal":
        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        val = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        if val == get_val(mem, addr1, size1):
            set_val(mem, addr2, 1, size2)
        else:
            set_val(mem, addr2, 0, size2)
    if tok == "not":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size2 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        result = val1 ^ ((1 << size1) - 1)
        set_val(mem, addr2, result, size2)
    if tok == "or":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        set_val(mem, addr3, val1 | val2, size3)
    if tok == "and":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        set_val(mem, addr3, val1 & val2, size3)
    if tok == "nor":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        result = ~(val1 | val2) & ((1 << max(size1, size2)) - 1)
        set_val(mem, addr3, result, size3)
    if tok == "nand":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        result = ~(val1 & val2) & ((1 << max(size1, size2)) - 1)
        set_val(mem, addr3, result, size3)
    if tok == "xor":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        set_val(mem, addr3, val1 ^ val2, size3)
    if tok == "xnor":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        val2 = get_val(mem, addr2, size2)
        result = ~(val1 ^ val2) & ((1 << max(size1, size2)) - 1)
        set_val(mem, addr3, result, size3)
    if tok == "shl":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        shift = get_val(mem, addr2, size2)
        result = (val1 << shift) % (2 ** size1)
        set_val(mem, addr3, result, size3)
    if tok == "shr":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size1 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size2 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr3 = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size3 = int(tok)

        cur += 2

        val1 = get_val(mem, addr1, size1)
        shift = get_val(mem, addr2, size2)
        result = val1 >> shift
        set_val(mem, addr3, result, size3)
    if tok == "subroutine":
        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        nextreturn = cur + 1
        cur = int(tok)
    if tok == "return":
        cur = nextreturn
    if tok == "if":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        target = int(tok)

        cur += 2

        if get_val(mem, addr, size) != 0:
            cur = target
    if tok == "ifnot":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        target = int(tok)

        cur += 2

        if get_val(mem, addr, size) == 0:
            cur = target
    if tok == "ifsubroutine":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        target = int(tok)

        cur += 2

        if get_val(mem, addr, size) != 0:
            nextreturn = cur
            cur = target
    if tok == "ifnotsubroutine":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ",")
        cur += len(tok)
        size = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        target = int(tok)

        cur += 2

        if get_val(mem, addr, size) == 0:
            nextreturn = cur
            cur = target
    if tok == "sleep":
        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        ms = int(tok)

        cur += 2

        time.sleep(ms / 1000)
    if tok == "exit":
        running = False
