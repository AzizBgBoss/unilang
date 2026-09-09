#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum OP_CODES
{
    // System / I/O / Misc Operations
    OP_NOP = 0x00,      // do nothing (padding/alignment) - parameters: none
    OP_FLAG,            // set the value of a flag with a certain index to a 4-bit value - parameters: flag(1), val(1)
    OP_ISFLAGSUPPORTED, // check if a flag with a certain index is supported, result into reg - parameters: flag(1), val(1), reg(1)
    OP_SETBYTE,         // set a single memory byte - parameters: addr(4), byte(1)
    OP_WRITE,           // write text as raw bytes starting at addr - parameters: addr(4), text(0)
    OP_OUTC,            // output the byte at addr as a character - parameters: addr(4)
    OP_OUT,             // output the byte at addr as a number - parameters: addr(4)
    OP_INPUT,           // read chars and store them as bytes starting at addr - parameters: chars(4), addr(4)
    OP_PRINT,           // print text - parameters: text(0)
    OP_PRINTN,          // print text without a newline - parameters: text(0)
    OP_PRINTV,          // print chars bytes from memory as characters - parameters: chars(4), addr(4)
    OP_PRINTVN,         // same as printv without a newline - parameters: chars(4), addr(4)
    OP_CUR,             // print the current position in the bytecode - parameters: none
    OP_MEMORY,          // print the current state of memory (hex dump) - parameters: none
    OP_FLAGS,           // print the current state of flags - parameters: none
    OP_SETPC,           // set the program counter to a fixed bytecode position - parameters: pos(4)
    OP_SUBROUTINE,      // call a subroutine at a fixed bytecode position - parameters: pos(4)
    OP_RETURN,          // return from a subroutine - parameters: none
    OP_SLEEP,           // sleep for a certain amount of milliseconds - parameters: ms(4)
    OP_REFRESHSCREEN,   // redraw the current frame and clear the framebuffer - parameters: none
    OP_ROMREAD8,        // read 1 byte from ROM into reg_dst - parameters: reg_addr(1), reg_dst(1)
    OP_ROMREAD16,       // read 2 bytes from ROM into reg_dst - parameters: reg_addr(1), reg_dst(1)
    OP_ROMREAD32,       // read 4 bytes from ROM into reg_dst - parameters: reg_addr(1), reg_dst(1)
    OP_SETPIXEL,        // draw one pixel using x/y/color values from registers - parameters: regx(1), regy(1), regc(1)
    OP_GETPIXEL,        // read one pixel's value into a register - parameters: regx(1), regy(1), regdst(1)
    OP_GETKEYBOARD,     // read keyboard state into a register - parameters: reg(1)
    OP_GETTIME,         // get the current time (ms) into a register - parameters: reg(1)
    OP_GETMEMSIZE,      // get the size of memory in bytes into a register - parameters: reg(1)
    OP_GETFLAG,         // read the current value of a flag into a register - parameters: flag(1), reg(1)
    OP_RANDREG,         // set a register to a random value between min and max (exclusive) - parameters: min(4), max(4), reg(1)

    // Direct Register & Memory Operations
    OP_SETREG = 0x40, // reg = val - parameters: reg(1), val(4)
    OP_MOVREG,        // dst = src - parameters: dst(1), src(1)
    OP_LOADREG8,      // reg = mem byte at addr (zero-extended) - parameters: reg(1), addr(4)
    OP_LOADREG16,     // reg = mem 2 bytes at addr - parameters: reg(1), addr(4)
    OP_LOADREG32,     // reg = mem 4 bytes at addr - parameters: reg(1), addr(4)
    OP_STOREREG8,     // mem byte at addr = reg & 0xFF - parameters: reg(1), addr(4)
    OP_STOREREG16,    // mem 2 bytes at addr = reg & 0xFFFF - parameters: reg(1), addr(4)
    OP_STOREREG32,    // mem 4 bytes at addr = reg - parameters: reg(1), addr(4)
    OP_ADDREG,        // dst = r1 + r2 - parameters: r1(1), r2(1), dst(1)
    OP_SUBREG,        // dst = r1 - r2 - parameters: r1(1), r2(1), dst(1)
    OP_MULREG,        // dst = r1 * r2 - parameters: r1(1), r2(1), dst(1)
    OP_DIVREG,        // dst = r1 // r2 (no-op if r2 == 0) - parameters: r1(1), r2(1), dst(1)
    OP_MODREG,        // dst = r1 % r2 (no-op if r2 == 0) - parameters: r1(1), r2(1), dst(1)
    OP_ADDREGV,       // dst = r1 + val - parameters: r1(1), val(4), dst(1)
    OP_SUBREGV,       // dst = r1 - val - parameters: r1(1), val(4), dst(1)
    OP_MULREGV,       // dst = r1 * val - parameters: r1(1), val(4), dst(1)
    OP_COMPREG,       // dst = 0 if equal, 1 if r1 > r2, 2 if r1 < r2 - parameters: r1(1), r2(1), dst(1)
    OP_IFREG,         // if reg != 0, jump to pos - parameters: reg(1), pos(4)
    OP_IFNOTREG,      // if reg == 0, jump to pos - parameters: reg(1), pos(4)
    OP_IFSUBREG,      // if reg != 0, call subroutine at pos - parameters: reg(1), pos(4)
    OP_IFNOTSUBREG,   // if reg == 0, call subroutine at pos - parameters: reg(1), pos(4)
    OP_NOTREG,        // dst = ~r1 (32-bit) - parameters: r1(1), dst(1)
    OP_ORREG,         // dst = r1 | r2 - parameters: r1(1), r2(1), dst(1)
    OP_ANDREG,        // dst = r1 & r2 - parameters: r1(1), r2(1), dst(1)
    OP_NORREG,        // dst = ~(r1 | r2) (32-bit) - parameters: r1(1), r2(1), dst(1)
    OP_NANDREG,       // dst = ~(r1 & r2) (32-bit) - parameters: r1(1), r2(1), dst(1)
    OP_XORREG,        // dst = r1 ^ r2 - parameters: r1(1), r2(1), dst(1)
    OP_XNORREG,       // dst = ~(r1 ^ r2) (32-bit) - parameters: r1(1), r2(1), dst(1)
    OP_SHLREG,        // dst = (r1 << r2) & 0xFFFFFFFF - parameters: r1(1), r2(1), dst(1)
    OP_SHRREG,        // dst = r1 >> r2 - parameters: r1(1), r2(1), dst(1)

    // Indirect Memory Access
    OP_LOADREGI8 = 0x60, // reg_dst = mem byte at address reg[reg_addr] - parameters: reg_addr(1), reg_dst(1)
    OP_LOADREGI16,       // reg_dst = mem 2 bytes at address reg[reg_addr] - parameters: reg_addr(1), reg_dst(1)
    OP_LOADREGI32,       // reg_dst = mem 4 bytes at address reg[reg_addr] - parameters: reg_addr(1), reg_dst(1)
    OP_STOREREGI8,       // mem byte at address reg[reg_addr] = reg[reg_val] & 0xFF - parameters: reg_addr(1), reg_val(1)
    OP_STOREREGI16,      // mem 2 bytes at address reg[reg_addr] = reg[reg_val] & 0xFFFF - parameters: reg_addr(1), reg_val(1)
    OP_STOREREGI32,      // mem 4 bytes at address reg[reg_addr] = reg[reg_val] - parameters: reg_addr(1), reg_val(1)

    // Direct Boolean Comparisons & Register Output
    OP_EQREG,  // dst = r1 == r2 - parameters: r1(1), r2(1), dst(1)
    OP_NEQREG, // dst = r1 != r2 - parameters: r1(1), r2(1), dst(1)
    OP_LTREG,  // dst = r1 < r2 - parameters: r1(1), r2(1), dst(1)
    OP_GTREG,  // dst = r1 > r2 - parameters: r1(1), r2(1), dst(1)
    OP_LEREG,  // dst = r1 <= r2 - parameters: r1(1), r2(1), dst(1)
    OP_GEREG,  // dst = r1 >= r2 - parameters: r1(1), r2(1), dst(1)
    OP_OUTREG, // print a register's full numeric value - parameters: reg(1)

    // Program Control
    OP_EXIT = 0xFF // exit the program - parameters: none
};

typedef struct UnilangVM UnilangVM;

typedef void     (*vm_outputchar)(char c);
typedef uint8_t  (*vm_readbyte)(uint32_t addr);
typedef int      (*vm_isflagsupported)(uint8_t flag, uint8_t val);
typedef void     (*vm_setpixel)(uint8_t x, uint8_t y, uint8_t color);
typedef uint8_t  (*vm_getpixel)(uint8_t x, uint8_t y);
typedef uint8_t  (*vm_getkeyboard)(void);
typedef uint32_t (*vm_gettime)(void);
typedef void     (*vm_sleep)(uint32_t ms);
typedef void     (*vm_refreshscreen)(void);
typedef uint32_t (*vm_input)(char *buf, uint32_t maxchars);

#define ULVM_FLAG_NUM 16
#define ULVM_FLAG_SIZE 4

#ifndef ULVM_MEM_SIZE
#define ULVM_MEM_SIZE 64 // in bytes
#warning "ULVM_MEM_SIZE not defined, defaulting to 64 bytes"
#endif

#ifndef ULVM_CALLSTACK_SIZE
#define ULVM_CALLSTACK_SIZE 16
#endif

struct UnilangVM
{
    uint32_t pc;
    int running;

    uint8_t mem[ULVM_MEM_SIZE];
    uint8_t flags[(ULVM_FLAG_NUM * ULVM_FLAG_SIZE) / 8];
    uint32_t reg[16];

    uint32_t callstack[ULVM_CALLSTACK_SIZE];
    uint8_t callsp;

    vm_outputchar outputchar;
    vm_readbyte readbyte;
    vm_isflagsupported isflagsupported;
    vm_setpixel setpixel;
    vm_getpixel getpixel;
    vm_getkeyboard getkeyboard;
    vm_gettime gettime;
    vm_sleep sleep;
    vm_refreshscreen refreshscreen;
    vm_input input;
};

static inline void ULVM_init(UnilangVM *vm, vm_outputchar outputchar, vm_readbyte readbyte)
{
    memset(vm, 0, sizeof(*vm));
    vm->pc = 0;
    vm->running = 1;
    vm->outputchar = outputchar;
    vm->readbyte = readbyte;
}

static inline void ULVM_set_graphics(UnilangVM *vm, vm_setpixel setpixel, vm_getpixel getpixel, vm_refreshscreen refreshscreen)
{
    vm->setpixel = setpixel;
    vm->getpixel = getpixel;
    vm->refreshscreen = refreshscreen;
}

static inline void ULVM_set_keyboard(UnilangVM *vm, vm_getkeyboard getkeyboard)
{
    vm->getkeyboard = getkeyboard;
}

static inline void ULVM_set_time(UnilangVM *vm, vm_gettime gettime, vm_sleep sleep)
{
    vm->gettime = gettime;
    vm->sleep = sleep;
}

static inline void ULVM_set_input(UnilangVM *vm, vm_input input)
{
    vm->input = input;
}

static inline void ULVM_set_flagsupport(UnilangVM *vm, vm_isflagsupported isflagsupported)
{
    vm->isflagsupported = isflagsupported;
}

#define ULVM_NEXTBYTE() ((uint32_t)vm->readbyte(vm->pc++))

static inline uint32_t ulvm_next2(UnilangVM *vm)
{
    uint32_t hi = ULVM_NEXTBYTE();
    uint32_t lo = ULVM_NEXTBYTE();
    return (hi << 8) | lo;
}

static inline uint32_t ulvm_next4(UnilangVM *vm)
{
    uint32_t hi = ulvm_next2(vm);
    uint32_t lo = ulvm_next2(vm);
    return (hi << 16) | lo;
}

#define ULVM_NEXT2BYTES() ulvm_next2(vm)
#define ULVM_NEXT4BYTES() ulvm_next4(vm)

#define ULVM_INBOUNDS(addr, size) ((uint64_t)(addr) + (uint64_t)(size) <= (uint64_t)ULVM_MEM_SIZE)
#define ULVM_R(i) (vm->reg[(i) & 0x0F])

static inline uint8_t ulvm_getflag(UnilangVM *vm, uint8_t flag)
{
    flag &= 0x0F;
    uint8_t byte = vm->flags[flag / 2];
    return (flag % 2 == 0) ? ((byte >> 4) & 0xF) : (byte & 0xF);
}

static inline void ulvm_setflag(UnilangVM *vm, uint8_t flag, uint8_t val)
{
    flag &= 0x0F;
    uint8_t idx = flag / 2;
    uint8_t shift = (flag % 2 == 0) ? 4 : 0;
    vm->flags[idx] = (uint8_t)((vm->flags[idx] & ~(0xF << shift)) | ((val & 0xF) << shift));
}

static inline void ulvm_print_uint(UnilangVM *vm, uint32_t v)
{
    char buf[10];
    int n = 0;
    if (v == 0)
    {
        vm->outputchar('0');
        return;
    }
    while (v > 0 && n < 10)
    {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        vm->outputchar(buf[--n]);
}

static inline void ulvm_print_hex_nibble(UnilangVM *vm, uint8_t n)
{
    vm->outputchar((char)(n < 10 ? '0' + n : 'a' + (n - 10)));
}

static inline void ulvm_print_hex_byte(UnilangVM *vm, uint8_t b)
{
    ulvm_print_hex_nibble(vm, (b >> 4) & 0xF);
    ulvm_print_hex_nibble(vm, b & 0xF);
}

static inline uint32_t ulvm_rom_read(UnilangVM *vm, uint32_t addr, int size)
{
    uint32_t val = 0;
    for (int i = 0; i < size; i++)
        val = (val << 8) | (uint32_t)vm->readbyte(addr + (uint32_t)i);
    return val;
}

static inline int ULVM_handlenextinstruction(UnilangVM *vm)
{
    uint8_t opbyte = (uint8_t)ULVM_NEXTBYTE();

    switch (opbyte)
    {
    case OP_NOP:
        break;

    case OP_FLAG:
    {
        uint8_t flag = (uint8_t)ULVM_NEXTBYTE();
        uint8_t val = (uint8_t)ULVM_NEXTBYTE();
        ulvm_setflag(vm, flag, val);
        break;
    }

    case OP_ISFLAGSUPPORTED:
    {
        uint8_t flag = (uint8_t)ULVM_NEXTBYTE();
        uint8_t val = (uint8_t)ULVM_NEXTBYTE();
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        int supported;
        if (val == 0)
            supported = 1;
        else
            supported = vm->isflagsupported ? vm->isflagsupported(flag, val) : 0;
        ULVM_R(r) = supported ? 1u : 0u;
        break;
    }

    case OP_SETBYTE:
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        uint8_t byte = (uint8_t)ULVM_NEXTBYTE();
        if (ULVM_INBOUNDS(addr, 1))
            vm->mem[addr] = byte;
        break;
    }

    case OP_WRITE:
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        uint32_t len = ULVM_NEXT2BYTES();
        for (uint32_t i = 0; i < len; i++)
        {
            uint8_t b = (uint8_t)ULVM_NEXTBYTE();
            if (ULVM_INBOUNDS(addr + i, 1))
                vm->mem[addr + i] = b;
        }
        break;
    }

    case OP_OUTC:
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        if (ULVM_INBOUNDS(addr, 1))
            vm->outputchar((char)vm->mem[addr]);
        break;
    }

    case OP_OUT:
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        if (ULVM_INBOUNDS(addr, 1))
            ulvm_print_uint(vm, vm->mem[addr]);
        break;
    }

    case OP_INPUT:
    {
        uint32_t chars = ULVM_NEXT4BYTES();
        uint32_t addr = ULVM_NEXT4BYTES();
        if (vm->input && ULVM_INBOUNDS(addr, 0))
        {
            uint32_t maxchars = chars;
            if ((uint64_t)addr + maxchars > ULVM_MEM_SIZE)
                maxchars = (uint32_t)(ULVM_MEM_SIZE - addr);
            vm->input((char *)&vm->mem[addr], maxchars);
        }
        break;
    }

    case OP_PRINT:
    case OP_PRINTN:
    {
        uint32_t len = ULVM_NEXT2BYTES();
        for (uint32_t i = 0; i < len; i++)
            vm->outputchar((char)ULVM_NEXTBYTE());
        if (opbyte == OP_PRINT)
            vm->outputchar('\n');
        break;
    }

    case OP_PRINTV:
    case OP_PRINTVN:
    {
        uint32_t chars = ULVM_NEXT4BYTES();
        uint32_t addr = ULVM_NEXT4BYTES();
        for (uint32_t i = 0; i < chars; i++)
            if (ULVM_INBOUNDS(addr + i, 1))
                vm->outputchar((char)vm->mem[addr + i]);
        if (opbyte == OP_PRINTV)
            vm->outputchar('\n');
        break;
    }

    case OP_CUR:
        ulvm_print_uint(vm, vm->pc);
        break;

    case OP_MEMORY:
    {
        const uint32_t columns = 16;
        for (uint32_t row = 0; row < ULVM_MEM_SIZE; row += columns)
        {
            ulvm_print_uint(vm, row);
            vm->outputchar('|');
            for (uint32_t c = 0; c < columns && row + c < ULVM_MEM_SIZE; c++)
            {
                ulvm_print_hex_byte(vm, vm->mem[row + c]);
                vm->outputchar(' ');
            }
            vm->outputchar('\n');
        }
        break;
    }

    case OP_FLAGS:
        for (uint8_t f = 0; f < ULVM_FLAG_NUM; f++)
        {
            uint8_t v = ulvm_getflag(vm, f);
            for (int b = 3; b >= 0; b--)
                vm->outputchar((char)('0' + ((v >> b) & 1)));
        }
        vm->outputchar('\n');
        break;

    case OP_SETPC:
        vm->pc = ULVM_NEXT4BYTES();
        break;

    case OP_SUBROUTINE:
    {
        uint32_t pos = ULVM_NEXT4BYTES();
        if (vm->callsp < ULVM_CALLSTACK_SIZE)
            vm->callstack[vm->callsp++] = vm->pc;
        vm->pc = pos;
        break;
    }

    case OP_RETURN:
        if (vm->callsp > 0)
            vm->pc = vm->callstack[--vm->callsp];
        break;

    case OP_SLEEP:
    {
        uint32_t ms = ULVM_NEXT4BYTES();
        if (vm->sleep)
            vm->sleep(ms);
        break;
    }

    case OP_REFRESHSCREEN:
        if (vm->refreshscreen)
            vm->refreshscreen();
        break;

    case OP_ROMREAD8:
    case OP_ROMREAD16:
    case OP_ROMREAD32:
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rd = (uint8_t)ULVM_NEXTBYTE();
        int size = (opbyte == OP_ROMREAD8) ? 1 : (opbyte == OP_ROMREAD16) ? 2 : 4;
        ULVM_R(rd) = ulvm_rom_read(vm, ULVM_R(ra), size);
        break;
    }

    case OP_SETPIXEL:
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        if (vm->setpixel)
            vm->setpixel((uint8_t)ULVM_R(rx), (uint8_t)ULVM_R(ry), (uint8_t)ULVM_R(rc));
        break;
    }

    case OP_GETPIXEL:
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(rdst) = vm->getpixel ? vm->getpixel((uint8_t)ULVM_R(rx), (uint8_t)ULVM_R(ry)) : 0;
        break;
    }

    case OP_GETKEYBOARD:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = vm->getkeyboard ? vm->getkeyboard() : 0;
        break;
    }

    case OP_GETTIME:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = vm->gettime ? vm->gettime() : 0;
        break;
    }

    case OP_GETMEMSIZE:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = ULVM_MEM_SIZE;
        break;
    }

    case OP_GETFLAG:
    {
        uint8_t flag = (uint8_t)ULVM_NEXTBYTE();
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = ulvm_getflag(vm, flag);
        break;
    }

    case OP_RANDREG:
    {
        uint32_t min = ULVM_NEXT4BYTES();
        uint32_t max = ULVM_NEXT4BYTES();
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t span = (max > min) ? (max - min) : 1;
        ULVM_R(r) = min + ((uint32_t)rand() % span);
        break;
    }

    case OP_SETREG:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t val = ULVM_NEXT4BYTES();
        ULVM_R(r) = val;
        break;
    }

    case OP_MOVREG:
    {
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint8_t src = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(dst) = ULVM_R(src);
        break;
    }

    case OP_LOADREG8:
    case OP_LOADREG16:
    case OP_LOADREG32:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        int size = (opbyte == OP_LOADREG8) ? 1 : (opbyte == OP_LOADREG16) ? 2 : 4;
        uint32_t val = 0;
        if (ULVM_INBOUNDS(addr, size))
            for (int i = 0; i < size; i++)
                val = (val << 8) | vm->mem[addr + i];
        ULVM_R(r) = val;
        break;
    }

    case OP_STOREREG8:
    case OP_STOREREG16:
    case OP_STOREREG32:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        int size = (opbyte == OP_STOREREG8) ? 1 : (opbyte == OP_STOREREG16) ? 2 : 4;
        if (ULVM_INBOUNDS(addr, size))
        {
            uint32_t val = ULVM_R(r);
            for (int i = 0; i < size; i++)
                vm->mem[addr + i] = (uint8_t)(val >> (8 * (size - 1 - i)));
        }
        break;
    }

    case OP_ADDREG:
    case OP_SUBREG:
    case OP_MULREG:
    case OP_DIVREG:
    case OP_MODREG:
    case OP_COMPREG:
    case OP_ORREG:
    case OP_ANDREG:
    case OP_NORREG:
    case OP_NANDREG:
    case OP_XORREG:
    case OP_XNORREG:
    case OP_SHLREG:
    case OP_SHRREG:
    case OP_EQREG:
    case OP_NEQREG:
    case OP_LTREG:
    case OP_GTREG:
    case OP_LEREG:
    case OP_GEREG:
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t r2 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint32_t a = ULVM_R(r1), b = ULVM_R(r2);
        switch (opbyte)
        {
        case OP_ADDREG: ULVM_R(dst) = a + b; break;
        case OP_SUBREG: ULVM_R(dst) = a - b; break;
        case OP_MULREG: ULVM_R(dst) = a * b; break;
        case OP_DIVREG: if (b != 0) ULVM_R(dst) = a / b; break;
        case OP_MODREG: if (b != 0) ULVM_R(dst) = a % b; break;
        case OP_COMPREG: ULVM_R(dst) = (a == b) ? 0u : (a > b) ? 1u : 2u; break;
        case OP_ORREG: ULVM_R(dst) = a | b; break;
        case OP_ANDREG: ULVM_R(dst) = a & b; break;
        case OP_NORREG: ULVM_R(dst) = ~(a | b); break;
        case OP_NANDREG: ULVM_R(dst) = ~(a & b); break;
        case OP_XORREG: ULVM_R(dst) = a ^ b; break;
        case OP_XNORREG: ULVM_R(dst) = ~(a ^ b); break;
        case OP_SHLREG: ULVM_R(dst) = (b < 32) ? (a << b) : 0; break;
        case OP_SHRREG: ULVM_R(dst) = (b < 32) ? (a >> b) : 0; break;
        case OP_EQREG: ULVM_R(dst) = (a == b) ? 1u : 0u; break;
        case OP_NEQREG: ULVM_R(dst) = (a != b) ? 1u : 0u; break;
        case OP_LTREG: ULVM_R(dst) = (a < b) ? 1u : 0u; break;
        case OP_GTREG: ULVM_R(dst) = (a > b) ? 1u : 0u; break;
        case OP_LEREG: ULVM_R(dst) = (a <= b) ? 1u : 0u; break;
        case OP_GEREG: ULVM_R(dst) = (a >= b) ? 1u : 0u; break;
        default: break;
        }
        break;
    }

    case OP_ADDREGV:
    case OP_SUBREGV:
    case OP_MULREGV:
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint32_t val = ULVM_NEXT4BYTES();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint32_t a = ULVM_R(r1);
        if (opbyte == OP_ADDREGV) ULVM_R(dst) = a + val;
        else if (opbyte == OP_SUBREGV) ULVM_R(dst) = a - val;
        else ULVM_R(dst) = a * val;
        break;
    }

    case OP_IFREG:
    case OP_IFNOTREG:
    case OP_IFSUBREG:
    case OP_IFNOTSUBREG:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t pos = ULVM_NEXT4BYTES();
        int take = (opbyte == OP_IFREG || opbyte == OP_IFSUBREG) ? (ULVM_R(r) != 0) : (ULVM_R(r) == 0);
        if (take)
        {
            if ((opbyte == OP_IFSUBREG || opbyte == OP_IFNOTSUBREG) && vm->callsp < ULVM_CALLSTACK_SIZE)
                vm->callstack[vm->callsp++] = vm->pc;
            vm->pc = pos;
        }
        break;
    }

    case OP_NOTREG:
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(dst) = ~ULVM_R(r1);
        break;
    }

    case OP_LOADREGI8:
    case OP_LOADREGI16:
    case OP_LOADREGI32:
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rd = (uint8_t)ULVM_NEXTBYTE();
        int size = (opbyte == OP_LOADREGI8) ? 1 : (opbyte == OP_LOADREGI16) ? 2 : 4;
        uint32_t addr = ULVM_R(ra);
        uint32_t val = 0;
        if (ULVM_INBOUNDS(addr, size))
            for (int i = 0; i < size; i++)
                val = (val << 8) | vm->mem[addr + i];
        ULVM_R(rd) = val;
        break;
    }

    case OP_STOREREGI8:
    case OP_STOREREGI16:
    case OP_STOREREGI32:
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rv = (uint8_t)ULVM_NEXTBYTE();
        int size = (opbyte == OP_STOREREGI8) ? 1 : (opbyte == OP_STOREREGI16) ? 2 : 4;
        uint32_t addr = ULVM_R(ra);
        if (ULVM_INBOUNDS(addr, size))
        {
            uint32_t val = ULVM_R(rv);
            for (int i = 0; i < size; i++)
                vm->mem[addr + i] = (uint8_t)(val >> (8 * (size - 1 - i)));
        }
        break;
    }

    case OP_OUTREG:
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ulvm_print_uint(vm, ULVM_R(r));
        break;
    }

    case OP_EXIT:
        vm->running = 0;
        break;

    default:

        vm->running = 0;
        break;
    }

    return !vm->running;
}