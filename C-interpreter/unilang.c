#pragma once
#include <stdio.h>
#include <stdint.h>

/*
unilang bytecode VM by AzizBgBoss
https://github.com/AzizBgBoss/unilang

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
*/

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

typedef void (*vm_outputchar)(char c);
typedef uint8_t (*vm_readbyte)(uint32_t addr);

#define ULVM_FLAG_NUM 16
#define ULVM_FLAG_SIZE 4

#ifndef ULVM_MEM_SIZE
#define ULVM_MEM_SIZE 64 // in bytes
#warning "ULVM_MEM_SIZE not defined, defaulting to 64 bytes"
#endif

typedef struct UnilangVM
{
    uint32_t pc;
    uint8_t mem[ULVM_MEM_SIZE];
    uint8_t flags[ULVM_FLAG_NUM / (8 / ULVM_FLAG_SIZE)];
    uint8_t supportedflags[ULVM_FLAG_NUM / 8]; // the user will handle setting this in their main()
    vm_outputchar outputchar;
    vm_readbyte readbyte;
};

void ULVM_init(UnilangVM *vm, vm_outputchar outputchar, vm_readbyte readbyte)
{
    vm->pc = 0;
    vm->outputchar = outputchar;
    vm->readbyte = readbyte;
    memset(vm->mem, 0, ULVM_MEM_SIZE);
    memset(vm->flags, 0, ULVM_FLAG_NUM / (8 / ULVM_FLAG_SIZE));
};

#define ULVM_NEXTBYTE() ((uint32_t)vm->readbyte(vm->pc++))
#define ULVM_NEXT2BYTES() (ULVM_NEXTBYTE() << 8 | ULVM_NEXTBYTE())
#define ULVM_NEXT4BYTES() (ULVM_NEXT2BYTES() << 16 | ULVM_NEXT2BYTES())

void ULVM_handlenextinstruction(UnilangVM *vm)
{
    switch (ULVM_NEXTBYTE())
    {
    case OP_NOP:
        break;
    case OP_FLAG:
    {
        uint8_t flag = ULVM_NEXTBYTE();
        uint8_t value = ULVM_NEXTBYTE();
        vm->flags[flag / (8 / ULVM_FLAG_SIZE)] = value << (4 * (flag % (8 / ULVM_FLAG_SIZE)));
        break;
    }
    case OP_PRINT:
    {
        uint16_t size = ULVM_NEXT2BYTES();
        for (uint16_t i = 0; i < size; i++)
        {
            vm->outputchar((char)ULVM_NEXTBYTE());
        }
        vm->outputchar('\n');
        break;
    }
    case OP_EXIT:
        vm->pc = -1;
        break;
    }
};