'''
unilang by AzizBgBoss
https://github.com/AzizBgBoss/unilang

Commands:
mem[addr:size]:val - set the value of a memory address with a certain size
outc(addr:size) - output the character of a memory address with a certain size
out(addr:size) - output the value of a memory address with a certain size
input(chars:addr:size) - read a certain amount of characters and store them in memory
print("text") - print text
printv(chars:addr:size) - print a certain amount of characters from memory
rand(addr:size) - set a memory address with a certain size to a random value
add(val, addr1:size1, addr2:size2) - add a value to a memory address with a certain size and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
addv(addr1:size1, addr2:size2, addr3:size3) - add two memory addresses with certain sizes and store it in another memory address with a certain size (you can use the same memory address for both the input and output)
cur - print the current position in the program
memory - print the current state of memory
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
ifsubroutine(addr:size, cur) - if the value of a memory address with a certain size is not 0, call a subroutine at a certain position in the program
exit - exit the program
'''

import random

memsize = 16
mem = bytearray(memsize // 8)

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
        if program[cur] != stop:
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

program = input(">")
program = program.replace(" ", "")
program = program.replace("\n", ";")

print(program)

cutoffs = "[];()"

running = True
cur = 0
tok = ""

nextreturn = 0

while running and cur < len(program):
    tok = readuntilcutoff(program, cur)
    cur += len(tok)
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
        tok = readuntil(program, cur, ";")
        cur += len(tok)
        val = int(tok)
        
        cur += 1
        
        set_val(mem, addr, val, size)
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
        
        print(chr(get_val(mem, addr, size)))
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
        
        print(get_val(mem, addr, size))
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

        cur += 1

        for i in range(chars):
            print(chr(get_val(mem, addr + i * size, size)), end = "")
        print()
    if tok == "rand":
        cur += 1
        tok = readuntil(program, cur, ":")
        cur += len(tok)
        addr = int(tok)

        cur += 1
        tok = readuntil(program, cur, ")")
        cur += len(tok)
        size = int(tok)

        cur += 1

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
    if tok == "cur":
        print(cur)
    if tok == "memory":
        for i in range(memsize):
            print(get_bit(mem, i), end = "")
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
            set_val(mem, 0, 1, 2)
        else:
            set_val(mem, 0, 0, 2)
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
    if tok == "exit":
        running = False
