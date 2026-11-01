#pragma once
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <float.h>

/*
unilang bytecode VM by AzizBgBoss
https://github.com/AzizBgBoss/unilang

This VM executes compiled unilang bytecode (produced by a separate converter/assembler or written by hand if you hate yourself enough).

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
1 - enable monochrome graphics (64x64) (1 x 64 x 64 = 4096 bits = 512 bytes = 0.5 KB)
2 - enable 2-bit grayscale graphics (4 shades, 64x64) (2 x 64 x 64 = 8192 bits = 1024 bytes = 1 KB)
3 - enable 8-bit color graphics (64x64) (8 x 64 x 64 = 32768 bits = 4096 bytes = 4 KB)
4 - enable monochrome graphics (128x64) (1 x 128 x 64 = 8192 bits = 1024 bytes = 1 KB)
5 - enable 2-bit grayscale graphics (4 shades, 128x64) (2 x 128 x 64 = 16384 bits = 2048 bytes = 2 KB)
6 - enable 8-bit color graphics (128x64) (8 x 128 x 64 = 65536 bits = 8192 bytes = 8 KB)
7 - enable 16-bit RGB555 color graphics (128x128) (16 x 128 x 128 = 262144 bits = 32768 bytes = 32 KB)
8 - enable 16-bit RGB555 color graphics (256x128) (16 x 256 x 128 = 524288 bits = 65536 bytes = 64 KB)

- flag 1 (keyboard) (this will consume the last n bits of memory (after graphics if it's used)) (Only gets supported once graphics are supported and on):
0 - disable keyboard
1 - NES style keyboard (Up + Down + Left + Right + A + B + select + start) (1 x 8 = 8 bits)
On keyboard: (Up + Down + Left + Right + C + V + Backspace + Enter)
*/

#if defined(ARDUINO_ARCH_AVR)
  #include <avr/pgmspace.h>
  #define READ_FLASH_BYTE(addr) pgm_read_byte(addr)
#else
  #define PROGMEM
  #define READ_FLASH_BYTE(addr) (*(const uint8_t*)(addr))
#endif

/**
 * 4x6 monochrome bitmap font for rendering
 * Author: Luiz Bills <luizbills@pm.me>
 *
 * License: Public Domain
 *
 * Based on the following projects:
 * - https://github.com/dhepper/font8x8
 * - https://alasseearfalas.itch.io/another-tiny-pixel-font-mono-3x5
 */

// Contains unicode points U+0020 - U+007F (basic latin)

#define ULVM_GLYPH_W 4
#define ULVM_GLYPH_H 6

const uint8_t ulvm_font4x6_basic[][3] PROGMEM = {
    { 0x00, 0x00, 0x00 }, // U+0020 (space)
    { 0x22, 0x20, 0x20 }, // U+0021 (!)
    { 0x55, 0x00, 0x00 }, // U+0022 (")
    { 0x57, 0x57, 0x50 }, // U+0023 (#)
    { 0x63, 0x67, 0x20 }, // U+0024 ($)
    { 0x54, 0x21, 0x50 }, // U+0025 (%)
    { 0x63, 0x65, 0x60 }, // U+0026 (&)
    { 0x22, 0x00, 0x00 }, // U+0027 (')
    { 0x21, 0x11, 0x20 }, // U+0028 (()
    { 0x24, 0x44, 0x20 }, // U+0029 ())
    { 0x52, 0x50, 0x00 }, // U+002a (*)
    { 0x02, 0x72, 0x00 }, // U+002b (+)
    { 0x00, 0x00, 0x21 }, // U+002c (,)
    { 0x00, 0x70, 0x00 }, // U+002d (-)
    { 0x00, 0x00, 0x10 }, // U+002e (.)
    { 0x44, 0x21, 0x10 }, // U+002f (/)
    { 0x75, 0x55, 0x70 }, // U+0030 (0)
    { 0x32, 0x22, 0x70 }, // U+0031 (1)
    { 0x74, 0x71, 0x70 }, // U+0032 (2)
    { 0x74, 0x74, 0x70 }, // U+0033 (3)
    { 0x55, 0x74, 0x40 }, // U+0034 (4)
    { 0x71, 0x74, 0x70 }, // U+0035 (5)
    { 0x71, 0x75, 0x70 }, // U+0036 (6)
    { 0x74, 0x44, 0x40 }, // U+0037 (7)
    { 0x75, 0x75, 0x70 }, // U+0038 (8)
    { 0x75, 0x74, 0x70 }, // U+0039 (9)
    { 0x00, 0x20, 0x20 }, // U+003a (:)
    { 0x00, 0x20, 0x21 }, // U+003b (;)
    { 0x42, 0x12, 0x40 }, // U+003c (<)
    { 0x07, 0x07, 0x00 }, // U+003d (=)
    { 0x12, 0x42, 0x10 }, // U+003e (>)
    { 0x74, 0x60, 0x20 }, // U+003f (?)
    { 0x25, 0x51, 0x60 }, // U+0040 (@)
    { 0x25, 0x75, 0x50 }, // U+0041 (A)
    { 0x35, 0x35, 0x30 }, // U+0042 (B)
    { 0x61, 0x11, 0x60 }, // U+0043 (C)
    { 0x35, 0x55, 0x30 }, // U+0044 (D)
    { 0x71, 0x31, 0x70 }, // U+0045 (E)
    { 0x71, 0x31, 0x10 }, // U+0046 (F)
    { 0x61, 0x55, 0x60 }, // U+0047 (G)
    { 0x55, 0x75, 0x50 }, // U+0048 (H)
    { 0x72, 0x22, 0x70 }, // U+0049 (I)
    { 0x44, 0x45, 0x20 }, // U+004a (J)
    { 0x55, 0x35, 0x50 }, // U+004b (K)
    { 0x11, 0x11, 0x70 }, // U+004c (L)
    { 0x57, 0x75, 0x50 }, // U+004d (M)
    { 0x75, 0x55, 0x50 }, // U+004e (N)
    { 0x25, 0x55, 0x20 }, // U+004f (O)
    { 0x75, 0x71, 0x10 }, // U+0050 (P)
    { 0x65, 0x53, 0x60 }, // U+0051 (Q)
    { 0x35, 0x35, 0x50 }, // U+0052 (R)
    { 0x61, 0x74, 0x30 }, // U+0053 (S)
    { 0x72, 0x22, 0x20 }, // U+0054 (T)
    { 0x55, 0x55, 0x70 }, // U+0055 (U)
    { 0x55, 0x52, 0x20 }, // U+0056 (V)
    { 0x55, 0x77, 0x50 }, // U+0057 (W)
    { 0x55, 0x25, 0x50 }, // U+0058 (X)
    { 0x55, 0x22, 0x20 }, // U+0059 (Y)
    { 0x74, 0x21, 0x70 }, // U+005a (Z)
    { 0x62, 0x22, 0x60 }, // U+005b ([)
    { 0x11, 0x24, 0x40 }, // U+005c (\)
    { 0x32, 0x22, 0x30 }, // U+005d (])
    { 0x25, 0x00, 0x00 }, // U+005e (^)
    { 0x00, 0x00, 0x70 }, // U+005f (_)
    { 0x12, 0x00, 0x00 }, // U+0060 (`)
    { 0x06, 0x55, 0x60 }, // U+0061 (a)
    { 0x13, 0x55, 0x30 }, // U+0062 (b)
    { 0x06, 0x11, 0x60 }, // U+0063 (c)
    { 0x46, 0x55, 0x60 }, // U+0064 (d)
    { 0x02, 0x53, 0x60 }, // U+0065 (e)
    { 0x42, 0x72, 0x20 }, // U+0066 (f)
    { 0x02, 0x56, 0x42 }, // U+0067 (g)
    { 0x11, 0x35, 0x50 }, // U+0068 (h)
    { 0x02, 0x02, 0x20 }, // U+0069 (i)
    { 0x02, 0x02, 0x21 }, // U+006a (j)
    { 0x15, 0x35, 0x50 }, // U+006b (k)
    { 0x22, 0x22, 0x40 }, // U+006c (l)
    { 0x05, 0x75, 0x50 }, // U+006d (m)
    { 0x03, 0x55, 0x50 }, // U+006e (n)
    { 0x02, 0x55, 0x20 }, // U+006f (o)
    { 0x03, 0x55, 0x31 }, // U+0070 (p)
    { 0x06, 0x55, 0x64 }, // U+0071 (q)
    { 0x02, 0x51, 0x10 }, // U+0072 (r)
    { 0x06, 0x14, 0x30 }, // U+0073 (s)
    { 0x27, 0x22, 0x40 }, // U+0074 (t)
    { 0x05, 0x55, 0x60 }, // U+0075 (u)
    { 0x05, 0x52, 0x20 }, // U+0076 (v)
    { 0x05, 0x57, 0x50 }, // U+0077 (w)
    { 0x05, 0x22, 0x50 }, // U+0078 (x)
    { 0x05, 0x56, 0x42 }, // U+0079 (y)
    { 0x07, 0x41, 0x70 }, // U+007a (z)
    { 0x62, 0x12, 0x60 }, // U+007b ({)
    { 0x22, 0x22, 0x20 }, // U+007c (|)
    { 0x32, 0x42, 0x30 }, // U+007d (})
    { 0x03, 0x60, 0x00 }, // U+007e (~)
};

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
    OP_RANDREG,         // set a register to a random value between min and max (exclusive) - parameters: r1(min)(1), r2(max)(1), dst(1)
    OP_SRANDREG,        // set the unilang propriatary pseudo-random function seed to the value in the reg - parameters: reg(1)
    OP_DRAWSTRING,      // draw a set of characters from memory (RAM or ROM) at a certain position using the 4x6 font included with auto wrapping, returns the next glyph x and y - parameters: regx(1), regy(1), regcolor(1), regaddr(1), reglen(1), src(1) [0=ram,1=rom], regreturnx(1), regreturny(1)
    OP_NUMTOSTR,        // convert a number in a register to ASCII decimal digits written into RAM, result length into a register - parameters: reg(1), addr(4), reglen(1)
    OP_FLOATOP,         // IEEE-754 float arithmetic/comparison - parameters: op(1), format(1), reg_left_addr(1), reg_right_addr(1), reg_dest_addr(1)
    OP_FLOATCAST,       // numeric conversion/copy - parameters: src_format(1), dst_format(1), reg_src_addr(1), reg_dst_addr(1)
    OP_ROMREAD64 = 0x23, // read 8 bytes from ROM into two registers - parameters: reg_addr(1), reg_hi(1), reg_lo(1)
    OP_FILLRECT = 0x24,  // fill a rectangle (clipped to the screen) - parameters: regx(1), regy(1), regw(1), regh(1), regc(1)
    OP_DRAWLINE,         // draw a line (Bresenham, clipped) - parameters: regx0(1), regy0(1), regx1(1), regy1(1), regc(1)
    OP_BLIT,             // draw a sprite region from a ROM/RAM sheet; 11 parameter words live in RAM at desc_addr (see ulvm_blit) - parameters: desc_addr(4)

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
    OP_SDIVREG = 0x6D, // signed 32-bit division
    OP_SMODREG, // signed 32-bit remainder
    OP_SSHRREG, // arithmetic right shift
    OP_OUTREGSIGNED, // print a signed 32-bit register

    // Extended ALU / fused compare ops (0x71+)
    OP_SLTREG = 0x71, // dst = (int32)r1 < (int32)r2 - parameters: r1(1), r2(1), dst(1)
    OP_SGTREG,        // signed >
    OP_SLEREG,        // signed <=
    OP_SGEREG,        // signed >=
    OP_ANDREGV,       // dst = r1 & val - parameters: r1(1), val(4), dst(1)
    OP_ORREGV,        // dst = r1 | val - parameters: r1(1), val(4), dst(1)
    OP_XORREGV,       // dst = r1 ^ val - parameters: r1(1), val(4), dst(1)
    OP_SHLREGV,       // dst = r1 << imm - parameters: r1(1), imm(1), dst(1)
    OP_SHRREGV,       // dst = r1 >> imm (logical) - parameters: r1(1), imm(1), dst(1)
    OP_SSHRREGV,      // dst = r1 >> imm (arithmetic) - parameters: r1(1), imm(1), dst(1)
    OP_CMPREGV,       // dst = cond(r1, val) ? 1 : 0 - parameters: r1(1), val(4), dst(1), cond(1)
    OP_JCMP,          // if cond(r1, r2) jump to pos - parameters: r1(1), r2(1), cond(1), pos(4)
    OP_JCMPV,         // if cond(r1, val) jump to pos - parameters: r1(1), val(4), cond(1), pos(4)
    // cond: 0 ==, 1 !=, 2 < (u), 3 > (u), 4 <= (u), 5 >= (u), 6 < (s), 7 > (s), 8 <= (s), 9 >= (s)

    // Program Control
    OP_EXIT = 0xFF // exit the program - parameters: none
};

typedef struct UnilangVM UnilangVM;

typedef void     (*vm_outputchar)(char c);
typedef uint8_t  (*vm_readbyte)(uint32_t addr);
typedef int      (*vm_isflagsupported)(uint8_t flag, uint8_t val);
typedef void     (*vm_setpixel)(uint8_t x, uint8_t y, uint16_t color);
typedef uint16_t (*vm_getpixel)(uint8_t x, uint8_t y);
typedef uint8_t  (*vm_getkeyboard)(void);
typedef uint32_t (*vm_gettime)(void);
typedef void     (*vm_sleep)(uint32_t ms);
typedef void     (*vm_refreshscreen)(void);
typedef uint32_t (*vm_input)(char *buf, uint32_t maxchars);
typedef void     (*vm_enablegraphics)(uint16_t wx, uint16_t wy, uint8_t colorsize);
typedef void     (*vm_disablegraphics)(void);
typedef void     (*vm_clearscreen)(void);

#define ULVM_FLAG_NUM 16
#define ULVM_FLAG_SIZE 4

#ifndef ULVM_MEM_SIZE
#define ULVM_MEM_SIZE 64 // in bytes
#warning "ULVM_MEM_SIZE not defined, defaulting to 64 bytes"
#endif

#define ULVM_CALLSTACK_SIZE 16

struct UnilangVM
{
    uint32_t pc;
    int running;
    int faulted;
    uint32_t fault_pc;
    uint8_t fault_opcode;
    uint32_t *instruction_hits;

    uint8_t mem[ULVM_MEM_SIZE];
    uint8_t flags[(ULVM_FLAG_NUM * ULVM_FLAG_SIZE) / 8];
    uint32_t reg[16];

    uint32_t callstack[ULVM_CALLSTACK_SIZE];
    uint8_t callsp;

    vm_outputchar outputchar;
    vm_readbyte readbyte;
    const uint8_t *rom;      // optional flat bytecode (see ULVM_set_rom); NULL = use readbyte()
    uint32_t rom_size;
    vm_isflagsupported isflagsupported;
    uint8_t graphics_mode;

    vm_setpixel setpixel;
    vm_getpixel getpixel;
    vm_getkeyboard getkeyboard;
    vm_gettime gettime;
    vm_sleep sleep;
    vm_refreshscreen refreshscreen;
    vm_input input;
    vm_enablegraphics enablegraphics;
    vm_disablegraphics disablegraphics;
    vm_clearscreen clearscreen;

    uint32_t rand_state;
    uint16_t gfx_w, gfx_h;
    uint16_t cursor_x, cursor_y;
};

static inline void ULVM_init(UnilangVM *vm, vm_outputchar outputchar, vm_readbyte readbyte)
{
    memset(vm, 0, sizeof(*vm));
    vm->pc = 0;
    vm->running = 1;
    vm->outputchar = outputchar;
    vm->readbyte = readbyte;
    vm->rand_state = 1;
}

static inline uint32_t ulvm_rand(UnilangVM *vm)
{
    // xorshift32 - unilang's own pseudo-random function
    uint32_t x = vm->rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    vm->rand_state = x;
    return x;
}

static inline void ulvm_srand(UnilangVM *vm, uint32_t seed)
{
    vm->rand_state = (seed == 0) ? 1 : seed;
}

static inline void ULVM_set_graphics(UnilangVM *vm, vm_setpixel setpixel, vm_getpixel getpixel, vm_refreshscreen refreshscreen,
                                      vm_enablegraphics enablegraphics, vm_disablegraphics disablegraphics, vm_clearscreen clearscreen)
{
    vm->setpixel = setpixel;
    vm->getpixel = getpixel;
    vm->refreshscreen = refreshscreen;
    vm->enablegraphics = enablegraphics;
    vm->disablegraphics = disablegraphics;
    vm->clearscreen = clearscreen;
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

// Optional fast path: point the VM at bytecode that is directly addressable
// (RAM, or PROGMEM on AVR -- it is read through READ_FLASH_BYTE). Instruction
// fetch then skips the readbyte() function-pointer call. `readbyte` is still
// required by ULVM_init but is unused while a flat ROM is set.
static inline void ULVM_set_rom(UnilangVM *vm, const uint8_t *rom, uint32_t size)
{
    vm->rom = rom;
    vm->rom_size = size;
}

// The fetch helpers below run several times per VM instruction; GCC at -O2
// did not inline ulvm_next4f (a fifth of the run time was spent calling it).
#if defined(__GNUC__)
#define ULVM_HOT static inline __attribute__((always_inline))
#else
#define ULVM_HOT static inline
#endif

// Instruction fetch. Used inside ULVM_run with pc/rom/rom_size held in locals.
ULVM_HOT uint32_t ulvm_fetch(UnilangVM *vm, const uint8_t *rom, uint32_t rom_size, uint32_t pc)
{
    if (rom)
        return pc < rom_size ? (uint32_t)READ_FLASH_BYTE(rom + pc) : 0u;
    return (uint32_t)vm->readbyte(pc);
}

static inline uint32_t ulvm_next2(UnilangVM *vm, const uint8_t *rom, uint32_t rom_size, uint32_t *pc)
{
    uint32_t hi = ulvm_fetch(vm, rom, rom_size, (*pc)++);
    uint32_t lo = ulvm_fetch(vm, rom, rom_size, (*pc)++);
    return (hi << 8) | lo;
}

static inline uint32_t ulvm_next4(UnilangVM *vm, const uint8_t *rom, uint32_t rom_size, uint32_t *pc)
{
    uint32_t hi = ulvm_next2(vm, rom, rom_size, pc);
    uint32_t lo = ulvm_next2(vm, rom, rom_size, pc);
    return (hi << 16) | lo;
}

// Fast path: `fast` is computed once per instruction (>= 16 bytes of ROM left), so the
// per-byte bounds checks are skipped for all but the last few instructions of a program.
ULVM_HOT uint32_t ulvm_next2f(UnilangVM *vm, const uint8_t *rom, uint32_t rom_size, uint32_t *pc, int fast)
{
    if (fast)
    {
        uint32_t p = *pc;
        *pc = p + 2;
        return ((uint32_t)READ_FLASH_BYTE(rom + p) << 8) | (uint32_t)READ_FLASH_BYTE(rom + p + 1);
    }
    return ulvm_next2(vm, rom, rom_size, pc);
}

ULVM_HOT uint32_t ulvm_next4f(UnilangVM *vm, const uint8_t *rom, uint32_t rom_size, uint32_t *pc, int fast)
{
    if (fast)
    {
        uint32_t p = *pc;
        *pc = p + 4;
        return ((uint32_t)READ_FLASH_BYTE(rom + p) << 24) | ((uint32_t)READ_FLASH_BYTE(rom + p + 1) << 16)
             | ((uint32_t)READ_FLASH_BYTE(rom + p + 2) << 8) | (uint32_t)READ_FLASH_BYTE(rom + p + 3);
    }
    return ulvm_next4(vm, rom, rom_size, pc);
}

#define ULVM_NEXTBYTE() (fast ? (uint32_t)READ_FLASH_BYTE(rom + pc++) : ulvm_fetch(vm, rom, rom_size, pc++))
#define ULVM_NEXT2BYTES() ulvm_next2f(vm, rom, rom_size, &pc, fast)
#define ULVM_NEXT4BYTES() ulvm_next4f(vm, rom, rom_size, &pc, fast)

// Random-access ROM read (romread opcodes, drawstring from ROM).
static inline uint8_t ulvm_rom_byte(UnilangVM *vm, uint32_t addr)
{
    if (vm->rom)
        return addr < vm->rom_size ? (uint8_t)READ_FLASH_BYTE(vm->rom + addr) : 0;
    return vm->readbyte(addr);
}

#define ULVM_INBOUNDS(addr, size) \
    ((addr) <= (uint32_t)ULVM_MEM_SIZE && \
     (size) <= (uint32_t)ULVM_MEM_SIZE - (uint32_t)(addr))
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

static inline void ulvm_print_int(UnilangVM *vm, int32_t value)
{
    uint32_t magnitude;
    if (value < 0)
    {
        vm->outputchar('-');
        magnitude = 0u - (uint32_t)value;
    }
    else
        magnitude = (uint32_t)value;
    ulvm_print_uint(vm, magnitude);
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
        val = (val << 8) | (uint32_t)ulvm_rom_byte(vm, addr + (uint32_t)i);
    return val;
}

static inline uint32_t ulvm_load_memory(UnilangVM *vm, uint32_t addr, uint8_t size)
{
    if (!ULVM_INBOUNDS(addr, size))
        return 0;
    switch (size)
    {
    case 1:
        return vm->mem[addr];
    case 2:
        return ((uint32_t)vm->mem[addr] << 8) | vm->mem[addr + 1];
    case 4:
        return ((uint32_t)vm->mem[addr] << 24)
            | ((uint32_t)vm->mem[addr + 1] << 16)
            | ((uint32_t)vm->mem[addr + 2] << 8)
            | vm->mem[addr + 3];
    default:
        return 0;
    }
}

static inline void ulvm_store_memory(UnilangVM *vm, uint32_t addr, uint8_t size, uint32_t value)
{
    if (!ULVM_INBOUNDS(addr, size))
        return;
    switch (size)
    {
    case 1:
        vm->mem[addr] = (uint8_t)value;
        break;
    case 2:
        vm->mem[addr] = (uint8_t)(value >> 8);
        vm->mem[addr + 1] = (uint8_t)value;
        break;
    case 4:
        vm->mem[addr] = (uint8_t)(value >> 24);
        vm->mem[addr + 1] = (uint8_t)(value >> 16);
        vm->mem[addr + 2] = (uint8_t)(value >> 8);
        vm->mem[addr + 3] = (uint8_t)value;
        break;
    default:
        break;
    }
}

static inline uint64_t ulvm_load_memory64(UnilangVM *vm, uint32_t addr)
{
    if (!ULVM_INBOUNDS(addr, 8))
        return 0;
    return ((uint64_t)ulvm_load_memory(vm, addr, 4) << 32)
        | ulvm_load_memory(vm, addr + 4, 4);
}

static inline void ulvm_store_memory64(UnilangVM *vm, uint32_t addr, uint64_t value)
{
    if (!ULVM_INBOUNDS(addr, 8))
        return;
    ulvm_store_memory(vm, addr, 4, (uint32_t)(value >> 32));
    ulvm_store_memory(vm, addr + 4, 4, (uint32_t)value);
}

static inline uint8_t ulvm_numeric_format_valid(uint8_t format)
{
    return format >= 1 && format <= 8;
}

static inline uint8_t ulvm_ieee_format_supported(uint8_t format)
{
    if (format == 7)
        return FLT_RADIX == 2 && sizeof(float) == 4 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128;
    if (format == 8)
        return FLT_RADIX == 2 && sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024;
    return 1;
}

static inline void ulvm_cast_integer(UnilangVM *vm, uint32_t src, uint8_t src_format,
                                     uint32_t dest, uint8_t dest_format)
{
    int64_t value = 0;
    switch (src_format)
    {
    case 1: value = (int8_t)ulvm_load_memory(vm, src, 1); break;
    case 2: value = ulvm_load_memory(vm, src, 1); break;
    case 3: value = (int16_t)ulvm_load_memory(vm, src, 2); break;
    case 4: value = ulvm_load_memory(vm, src, 2); break;
    case 5: value = (int32_t)ulvm_load_memory(vm, src, 4); break;
    case 6: value = ulvm_load_memory(vm, src, 4); break;
    default: return;
    }

    switch (dest_format)
    {
    case 1:
    case 2: ulvm_store_memory(vm, dest, 1, (uint8_t)value); break;
    case 3:
    case 4: ulvm_store_memory(vm, dest, 2, (uint16_t)value); break;
    case 5:
    case 6: ulvm_store_memory(vm, dest, 4, (uint32_t)value); break;
    default: break;
    }
}

static inline double ulvm_load_numeric(UnilangVM *vm, uint32_t addr, uint8_t format)
{
    uint32_t raw;
    uint64_t raw64;
    float f;
    double d;
    switch (format)
    {
    case 1: return (double)(int8_t)ulvm_load_memory(vm, addr, 1);
    case 2: return (double)ulvm_load_memory(vm, addr, 1);
    case 3: return (double)(int16_t)ulvm_load_memory(vm, addr, 2);
    case 4: return (double)ulvm_load_memory(vm, addr, 2);
    case 5: return (double)(int32_t)ulvm_load_memory(vm, addr, 4);
    case 6: return (double)ulvm_load_memory(vm, addr, 4);
    case 7:
        raw = ulvm_load_memory(vm, addr, 4);
        memcpy(&f, &raw, sizeof(f));
        return (double)f;
    case 8:
        raw64 = ulvm_load_memory64(vm, addr);
        memcpy(&d, &raw64, sizeof(d));
        return d;
    default:
        return 0.0;
    }
}

static inline void ulvm_store_numeric(UnilangVM *vm, uint32_t addr, uint8_t format, double value)
{
    uint32_t raw;
    uint64_t raw64;
    float f;
    double d;
    int32_t si32;
    switch (format)
    {
    case 1:
        ulvm_store_memory(vm, addr, 1,
            (value > -129.0 && value < 128.0) ? (uint8_t)(int8_t)value : 0);
        break;
    case 2:
        ulvm_store_memory(vm, addr, 1,
            (value > -1.0 && value < 256.0) ? (uint8_t)value : 0);
        break;
    case 3:
        ulvm_store_memory(vm, addr, 2,
            (value > -32769.0 && value < 32768.0) ? (uint16_t)(int16_t)value : 0);
        break;
    case 4:
        ulvm_store_memory(vm, addr, 2,
            (value > -1.0 && value < 65536.0) ? (uint16_t)value : 0);
        break;
    case 5:
        if (value > -2147483649.0 && value < 2147483648.0)
        {
            si32 = (int32_t)value;
            ulvm_store_memory(vm, addr, 4, (uint32_t)si32);
        }
        else
            ulvm_store_memory(vm, addr, 4, 0);
        break;
    case 6:
        ulvm_store_memory(vm, addr, 4,
            (value > -1.0 && value < 4294967296.0) ? (uint32_t)value : 0);
        break;
    case 7:
        f = (float)value;
        memcpy(&raw, &f, sizeof(raw));
        ulvm_store_memory(vm, addr, 4, raw);
        break;
    case 8:
        d = value;
        memcpy(&raw64, &d, sizeof(raw64));
        ulvm_store_memory64(vm, addr, raw64);
        break;
    default:
        break;
    }
}

static inline void ulvm_graphics_dims(uint8_t val, uint16_t *wx, uint16_t *wy, uint8_t *colorsize)
{
    switch (val)
    {
    case 1: *wx = 64;  *wy = 64; *colorsize = 1; break;
    case 2: *wx = 64;  *wy = 64; *colorsize = 4; break;
    case 3: *wx = 64;  *wy = 64; *colorsize = 8; break;
    case 4: *wx = 128; *wy = 64; *colorsize = 1; break;
    case 5: *wx = 128; *wy = 64; *colorsize = 4; break;
    case 6: *wx = 128; *wy = 64; *colorsize = 8; break;
    case 7: *wx = 128; *wy = 128; *colorsize = 16; break;
    case 8: *wx = 256; *wy = 128; *colorsize = 16; break;
    default: *wx = 0; *wy = 0; *colorsize = 0; break;
    }
}

static inline void ulvm_drawchar(UnilangVM *vm, uint16_t x, uint16_t y, uint16_t color, uint8_t c)
{
    if (!vm->setpixel)
        return;
    if (c < 0x20 || c > 0x7F)
        c = 0x20;
    const uint8_t *glyph = ulvm_font4x6_basic[c - 0x20];
    for (int row = 0; row < 6; row++)
    {
        uint8_t byte = READ_FLASH_BYTE(&glyph[row / 2]);  // the font lives in PROGMEM on AVR
        uint8_t nibble = (row % 2 == 0) ? ((byte >> 4) & 0xF) : (byte & 0xF);
        for (int col = 0; col < 4; col++)
        {
            if (nibble & (1 << col))
                vm->setpixel((uint8_t)(x + col), (uint8_t)(y + row), color);
        }
    }
}

static inline void ulvm_drawstring(UnilangVM *vm, uint16_t color, char c)
{
    if (c == '\n')
    {
        vm->cursor_x = 0;
        vm->cursor_y += ULVM_GLYPH_H;
    }
    else
    {
        ulvm_drawchar(vm, vm->cursor_x, vm->cursor_y, color, (uint8_t)c);
        vm->cursor_x += ULVM_GLYPH_W;
        if (vm->cursor_x + 4 > vm->gfx_w)
        {
            vm->cursor_x = 0;
            vm->cursor_y += ULVM_GLYPH_H;
        }
    }
    if (vm->cursor_y + 6 > vm->gfx_h)
    {
        vm->cursor_x = 0;
        vm->cursor_y = 0;
    }
}

static inline void ulvm_drawstring_mem(UnilangVM *vm, uint16_t x, uint16_t y, uint16_t color,
                                        uint32_t addr, uint32_t len, uint8_t rom,
                                        uint16_t *retx, uint16_t *rety)
{
    if (!rom)
    {
        if (addr > (uint32_t)ULVM_MEM_SIZE)
            len = 0;
        else if (len > (uint32_t)ULVM_MEM_SIZE - addr)
            len = (uint32_t)ULVM_MEM_SIZE - addr;
    }
    vm->cursor_x = x;
    vm->cursor_y = y;
    for (uint32_t i = 0; i < len; i++)
    {
        uint8_t c;
        if (rom)
        {
            c = ulvm_rom_byte(vm, addr + i);
        }
        else
        {
            c = vm->mem[addr + i];
        }
        ulvm_drawstring(vm, color, (char)c);
    }
    if (retx) *retx = vm->cursor_x;
    if (rety) *rety = vm->cursor_y;
}

static inline uint8_t ulvm_itoa(UnilangVM *vm, uint32_t v, uint32_t addr)
{
    char tmp[10];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0 && n < 10)
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    uint32_t available = addr <= (uint32_t)ULVM_MEM_SIZE
        ? (uint32_t)ULVM_MEM_SIZE - addr : 0;
    for (int i = 0; i < n; i++)
    {
        uint32_t offset = (uint32_t)(n - 1 - i);
        if (offset < available)
            vm->mem[addr + offset] = (uint8_t)tmp[i];
    }
    return (uint8_t)n;
}

/* Runs up to `budget` instructions (or until the program exits) and returns
   nonzero once the VM has halted. pc and the ROM pointer live in locals for the
   duration, so a large budget is much cheaper per instruction than calling
   ULVM_handlenextinstruction() in a loop. Pick a budget that matches how often
   the host must do other work (event pumping, watchdog, ...). */
ULVM_HOT int ulvm_cond(uint8_t c, uint32_t a, uint32_t b)
{
    switch (c)
    {
    case 0: return a == b;
    case 1: return a != b;
    case 2: return a < b;
    case 3: return a > b;
    case 4: return a <= b;
    case 5: return a >= b;
    case 6: return (int32_t)a < (int32_t)b;
    case 7: return (int32_t)a > (int32_t)b;
    case 8: return (int32_t)a <= (int32_t)b;
    case 9: return (int32_t)a >= (int32_t)b;
    default: return 0;
    }
}

// Drawing helpers. Everything is clipped to the screen (gfx_w x gfx_h once graphics are on).
static inline int32_t ulvm_gw(UnilangVM *vm) { return vm->gfx_w ? (int32_t)vm->gfx_w : 256; }
static inline int32_t ulvm_gh(UnilangVM *vm) { return vm->gfx_h ? (int32_t)vm->gfx_h : 256; }

static inline void ulvm_fillrect(UnilangVM *vm, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
    if (!vm->setpixel || w <= 0 || h <= 0)
        return;
    int32_t x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > ulvm_gw(vm)) x1 = ulvm_gw(vm);
    if (y1 > ulvm_gh(vm)) y1 = ulvm_gh(vm);
    for (int32_t yy = y; yy < y1; yy++)
        for (int32_t xx = x; xx < x1; xx++)
            vm->setpixel((uint8_t)xx, (uint8_t)yy, c);
}

static inline void ulvm_drawline(UnilangVM *vm, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c)
{
    if (!vm->setpixel)
        return;
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0; // -|dy|
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    int32_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    for (int32_t guard = 0; guard < 4096; guard++)
    {
        if (x0 >= 0 && y0 >= 0 && x0 < maxx && y0 < maxy)
            vm->setpixel((uint8_t)x0, (uint8_t)y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Sprite blit. Descriptor = 11 big-endian 32-bit words in RAM at `d`:
//  0 x, 1 y (destination, may be negative)   2 w, 3 h (size in pixels)
//  4 src (byte address of the sheet in ROM, or in RAM if flags bit 5)
//  5 srcx, 6 srcy (top-left of the region inside the sheet)   7 stride (sheet width in pixels)
//  8 flags: bit0 flip X, bit1 flip Y, bit2 4bpp (low nibble = even x, else 8bpp),
//           bit3 map colours through the RAM table at word 9, bit4 skip pixels equal to word 10,
//           bit5 sheet is in RAM
//  9 palette (RAM address of a table indexed by the source value)   10 transparent value
static inline void ulvm_blit(UnilangVM *vm, uint32_t d)
{
    if (!vm->setpixel)
        return;
    int32_t x = (int32_t)ulvm_load_memory(vm, d + 0, 4), y = (int32_t)ulvm_load_memory(vm, d + 4, 4);
    int32_t w = (int32_t)ulvm_load_memory(vm, d + 8, 4), h = (int32_t)ulvm_load_memory(vm, d + 12, 4);
    uint32_t src = ulvm_load_memory(vm, d + 16, 4);
    int32_t sx = (int32_t)ulvm_load_memory(vm, d + 20, 4), sy = (int32_t)ulvm_load_memory(vm, d + 24, 4);
    int32_t stride = (int32_t)ulvm_load_memory(vm, d + 28, 4);
    uint32_t fl = ulvm_load_memory(vm, d + 32, 4), pal = ulvm_load_memory(vm, d + 36, 4);
    uint32_t tr = ulvm_load_memory(vm, d + 40, 4);
    int32_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    for (int32_t r = 0; r < h; r++)
    {
        int32_t py = y + r;
        if (py < 0 || py >= maxy)
            continue;
        int32_t srow = (fl & 2) ? (h - 1 - r) : r;
        for (int32_t c = 0; c < w; c++)
        {
            int32_t px = x + c;
            if (px < 0 || px >= maxx)
                continue;
            int32_t scol = (fl & 1) ? (w - 1 - c) : c;
            uint32_t idx = (uint32_t)((sy + srow) * stride + sx + scol);
            uint32_t v;
            if (fl & 4)
            {
                uint32_t a = src + (idx >> 1);
                uint32_t b = (fl & 32) ? (a < (uint32_t)ULVM_MEM_SIZE ? vm->mem[a] : 0u) : (uint32_t)ulvm_rom_byte(vm, a);
                v = (idx & 1) ? (b >> 4) : (b & 15);
            }
            else
            {
                uint32_t a = src + idx;
                v = (fl & 32) ? (a < (uint32_t)ULVM_MEM_SIZE ? vm->mem[a] : 0u) : (uint32_t)ulvm_rom_byte(vm, a);
            }
            if ((fl & 16) && v == tr)
                continue;
            if (fl & 8)
                v = (pal + v < (uint32_t)ULVM_MEM_SIZE) ? vm->mem[pal + v] : v;
            vm->setpixel((uint8_t)px, (uint8_t)py, (uint8_t)v);
        }
    }
}

// Dispatch. By default the interpreter loop is a plain switch. Defining
// ULVM_COMPUTED_GOTO=1 (GCC only) switches to computed goto ("labels as
// values") with threaded dispatch: every handler fetches the next opcode and
// jumps straight to its handler. Measured on x86-64 with GCC 13 this was
// 0-30% SLOWER than the switch at -O2 and about even at -O3, so it is off by
// default; it is kept for targets where it may pay off (try it on yours).
// Clang 18 crashes while optimizing the threaded loop, so it is never
// enabled there.
#ifndef ULVM_COMPUTED_GOTO
#define ULVM_COMPUTED_GOTO 0
#endif
#if ULVM_COMPUTED_GOTO && (!defined(__GNUC__) || defined(__clang__))
#undef ULVM_COMPUTED_GOTO
#define ULVM_COMPUTED_GOTO 0
#endif

#if ULVM_COMPUTED_GOTO
#define ULVM_CASE(op) case op: ulvm_l_##op:
#define ULVM_DISPATCH_TABLE \
    [OP_ADDREG] = &&ulvm_l_OP_ADDREG, \
    [OP_ADDREGV] = &&ulvm_l_OP_ADDREGV, \
    [OP_ANDREG] = &&ulvm_l_OP_ANDREG, \
    [OP_ANDREGV] = &&ulvm_l_OP_ANDREGV, \
    [OP_BLIT] = &&ulvm_l_OP_BLIT, \
    [OP_CMPREGV] = &&ulvm_l_OP_CMPREGV, \
    [OP_COMPREG] = &&ulvm_l_OP_COMPREG, \
    [OP_CUR] = &&ulvm_l_OP_CUR, \
    [OP_DIVREG] = &&ulvm_l_OP_DIVREG, \
    [OP_DRAWLINE] = &&ulvm_l_OP_DRAWLINE, \
    [OP_DRAWSTRING] = &&ulvm_l_OP_DRAWSTRING, \
    [OP_EQREG] = &&ulvm_l_OP_EQREG, \
    [OP_EXIT] = &&ulvm_l_OP_EXIT, \
    [OP_FILLRECT] = &&ulvm_l_OP_FILLRECT, \
    [OP_FLAG] = &&ulvm_l_OP_FLAG, \
    [OP_FLAGS] = &&ulvm_l_OP_FLAGS, \
    [OP_FLOATCAST] = &&ulvm_l_OP_FLOATCAST, \
    [OP_FLOATOP] = &&ulvm_l_OP_FLOATOP, \
    [OP_GEREG] = &&ulvm_l_OP_GEREG, \
    [OP_GETFLAG] = &&ulvm_l_OP_GETFLAG, \
    [OP_GETKEYBOARD] = &&ulvm_l_OP_GETKEYBOARD, \
    [OP_GETMEMSIZE] = &&ulvm_l_OP_GETMEMSIZE, \
    [OP_GETPIXEL] = &&ulvm_l_OP_GETPIXEL, \
    [OP_GETTIME] = &&ulvm_l_OP_GETTIME, \
    [OP_GTREG] = &&ulvm_l_OP_GTREG, \
    [OP_IFNOTREG] = &&ulvm_l_OP_IFNOTREG, \
    [OP_IFNOTSUBREG] = &&ulvm_l_OP_IFNOTSUBREG, \
    [OP_IFREG] = &&ulvm_l_OP_IFREG, \
    [OP_IFSUBREG] = &&ulvm_l_OP_IFSUBREG, \
    [OP_INPUT] = &&ulvm_l_OP_INPUT, \
    [OP_ISFLAGSUPPORTED] = &&ulvm_l_OP_ISFLAGSUPPORTED, \
    [OP_JCMP] = &&ulvm_l_OP_JCMP, \
    [OP_JCMPV] = &&ulvm_l_OP_JCMPV, \
    [OP_LEREG] = &&ulvm_l_OP_LEREG, \
    [OP_LOADREG16] = &&ulvm_l_OP_LOADREG16, \
    [OP_LOADREG32] = &&ulvm_l_OP_LOADREG32, \
    [OP_LOADREG8] = &&ulvm_l_OP_LOADREG8, \
    [OP_LOADREGI16] = &&ulvm_l_OP_LOADREGI16, \
    [OP_LOADREGI32] = &&ulvm_l_OP_LOADREGI32, \
    [OP_LOADREGI8] = &&ulvm_l_OP_LOADREGI8, \
    [OP_LTREG] = &&ulvm_l_OP_LTREG, \
    [OP_MEMORY] = &&ulvm_l_OP_MEMORY, \
    [OP_MODREG] = &&ulvm_l_OP_MODREG, \
    [OP_MOVREG] = &&ulvm_l_OP_MOVREG, \
    [OP_MULREG] = &&ulvm_l_OP_MULREG, \
    [OP_MULREGV] = &&ulvm_l_OP_MULREGV, \
    [OP_NANDREG] = &&ulvm_l_OP_NANDREG, \
    [OP_NEQREG] = &&ulvm_l_OP_NEQREG, \
    [OP_NOP] = &&ulvm_l_OP_NOP, \
    [OP_NORREG] = &&ulvm_l_OP_NORREG, \
    [OP_NOTREG] = &&ulvm_l_OP_NOTREG, \
    [OP_NUMTOSTR] = &&ulvm_l_OP_NUMTOSTR, \
    [OP_ORREG] = &&ulvm_l_OP_ORREG, \
    [OP_ORREGV] = &&ulvm_l_OP_ORREGV, \
    [OP_OUT] = &&ulvm_l_OP_OUT, \
    [OP_OUTC] = &&ulvm_l_OP_OUTC, \
    [OP_OUTREG] = &&ulvm_l_OP_OUTREG, \
    [OP_OUTREGSIGNED] = &&ulvm_l_OP_OUTREGSIGNED, \
    [OP_PRINT] = &&ulvm_l_OP_PRINT, \
    [OP_PRINTN] = &&ulvm_l_OP_PRINTN, \
    [OP_PRINTV] = &&ulvm_l_OP_PRINTV, \
    [OP_PRINTVN] = &&ulvm_l_OP_PRINTVN, \
    [OP_RANDREG] = &&ulvm_l_OP_RANDREG, \
    [OP_REFRESHSCREEN] = &&ulvm_l_OP_REFRESHSCREEN, \
    [OP_RETURN] = &&ulvm_l_OP_RETURN, \
    [OP_ROMREAD16] = &&ulvm_l_OP_ROMREAD16, \
    [OP_ROMREAD32] = &&ulvm_l_OP_ROMREAD32, \
    [OP_ROMREAD64] = &&ulvm_l_OP_ROMREAD64, \
    [OP_ROMREAD8] = &&ulvm_l_OP_ROMREAD8, \
    [OP_SDIVREG] = &&ulvm_l_OP_SDIVREG, \
    [OP_SETBYTE] = &&ulvm_l_OP_SETBYTE, \
    [OP_SETPC] = &&ulvm_l_OP_SETPC, \
    [OP_SETPIXEL] = &&ulvm_l_OP_SETPIXEL, \
    [OP_SETREG] = &&ulvm_l_OP_SETREG, \
    [OP_SGEREG] = &&ulvm_l_OP_SGEREG, \
    [OP_SGTREG] = &&ulvm_l_OP_SGTREG, \
    [OP_SHLREG] = &&ulvm_l_OP_SHLREG, \
    [OP_SHLREGV] = &&ulvm_l_OP_SHLREGV, \
    [OP_SHRREG] = &&ulvm_l_OP_SHRREG, \
    [OP_SHRREGV] = &&ulvm_l_OP_SHRREGV, \
    [OP_SLEEP] = &&ulvm_l_OP_SLEEP, \
    [OP_SLEREG] = &&ulvm_l_OP_SLEREG, \
    [OP_SLTREG] = &&ulvm_l_OP_SLTREG, \
    [OP_SMODREG] = &&ulvm_l_OP_SMODREG, \
    [OP_SRANDREG] = &&ulvm_l_OP_SRANDREG, \
    [OP_SSHRREG] = &&ulvm_l_OP_SSHRREG, \
    [OP_SSHRREGV] = &&ulvm_l_OP_SSHRREGV, \
    [OP_STOREREG16] = &&ulvm_l_OP_STOREREG16, \
    [OP_STOREREG32] = &&ulvm_l_OP_STOREREG32, \
    [OP_STOREREG8] = &&ulvm_l_OP_STOREREG8, \
    [OP_STOREREGI16] = &&ulvm_l_OP_STOREREGI16, \
    [OP_STOREREGI32] = &&ulvm_l_OP_STOREREGI32, \
    [OP_STOREREGI8] = &&ulvm_l_OP_STOREREGI8, \
    [OP_SUBREG] = &&ulvm_l_OP_SUBREG, \
    [OP_SUBREGV] = &&ulvm_l_OP_SUBREGV, \
    [OP_SUBROUTINE] = &&ulvm_l_OP_SUBROUTINE, \
    [OP_WRITE] = &&ulvm_l_OP_WRITE, \
    [OP_XNORREG] = &&ulvm_l_OP_XNORREG, \
    [OP_XORREG] = &&ulvm_l_OP_XORREG, \
    [OP_XORREGV] = &&ulvm_l_OP_XORREGV, \

// Ends a handler: fetch the next opcode and jump to its handler directly.
// Only used where vm->running is known to still be 1.
#define ULVM_NEXT() \
    do { \
        if (!budget) goto ulvm_out; \
        budget--; \
        fast = pc <= fast_end; \
        if (!fast && rom && pc >= rom_size) \
        { \
            vm->faulted = 1; \
            vm->fault_pc = pc; \
            vm->fault_opcode = 0; \
            vm->running = 0; \
            goto ulvm_out; \
        } \
        instruction_pc = pc; \
        if (hits && instruction_pc < rom_size) \
            hits[instruction_pc]++; \
        opbyte = (uint8_t)ULVM_NEXTBYTE(); \
        goto *ulvm_dispatch[opbyte]; \
    } while (0)
#else
#define ULVM_CASE(op) case op:
#define ULVM_NEXT() break
#endif

static inline int ULVM_run(UnilangVM *vm, uint32_t budget)
{
    uint32_t pc = vm->pc;
    const uint8_t *rom = vm->rom;
    const uint32_t rom_size = vm->rom_size;
    uint32_t *const hits = vm->instruction_hits;
    // last pc with 16 bytes of ROM after it; UINT32_MAX-free sentinel when
    // there is no flat ROM (or it is tiny): then pc <= fast_end never holds
    const int have_fast = rom != NULL && rom_size >= 16u;
    const uint32_t fast_end = have_fast ? rom_size - 16u : 0u;
    uint32_t instruction_pc;
    int fast;
    uint8_t opbyte;

#if ULVM_COMPUTED_GOTO
    /* One indirect jump per handler instead of one shared switch jump: the
       branch predictor gets a separate history for every opcode. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    static const void *const ulvm_dispatch[256] = {
        [0 ... 255] = &&ulvm_l_default,   /* unknown opcode: fault */
        ULVM_DISPATCH_TABLE
    };
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

    while (budget && vm->running)
    {
    budget--;
    // fast: at least 16 bytes of flat ROM left, so operand reads skip their
    // bounds checks. One comparison covers the common case.
    fast = pc <= fast_end;
    if (!fast && rom && pc >= rom_size)
    {
        vm->faulted = 1;
        vm->fault_pc = pc;
        vm->fault_opcode = 0;
        vm->running = 0;
        break;
    }
    instruction_pc = pc;
    if (hits && instruction_pc < rom_size)
        hits[instruction_pc]++;
    opbyte = (uint8_t)ULVM_NEXTBYTE();
#if ULVM_COMPUTED_GOTO
    goto *ulvm_dispatch[opbyte];
#endif

    switch (opbyte)
    {
    ULVM_CASE(OP_NOP)
        ULVM_NEXT();

    ULVM_CASE(OP_FLAG)
    {
        uint8_t flag = (uint8_t)ULVM_NEXTBYTE();
        uint8_t val = (uint8_t)ULVM_NEXTBYTE();
        ulvm_setflag(vm, flag, val);
        if (flag == 0 && val != vm->graphics_mode)
        {
            if (val == 0)
            {
                if (vm->disablegraphics)
                    vm->disablegraphics();
            }
            else
            {
                uint16_t wx, wy;
                uint8_t colorsize;
                ulvm_graphics_dims(val, &wx, &wy, &colorsize);
                vm->gfx_w = wx;
                vm->gfx_h = wy;
                vm->cursor_x = 0;
                vm->cursor_y = 0;
                if (vm->enablegraphics)
                    vm->enablegraphics(wx, wy, colorsize);
            }
            vm->graphics_mode = val;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_ISFLAGSUPPORTED)
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
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SETBYTE)
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        uint8_t byte = (uint8_t)ULVM_NEXTBYTE();
        if (ULVM_INBOUNDS(addr, 1))
            vm->mem[addr] = byte;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_WRITE)
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        uint32_t len = ULVM_NEXT2BYTES();
        uint32_t available = addr <= (uint32_t)ULVM_MEM_SIZE
            ? (uint32_t)ULVM_MEM_SIZE - addr : 0;
        for (uint32_t i = 0; i < len; i++)
        {
            uint8_t b = (uint8_t)ULVM_NEXTBYTE();
            if (i < available)
                vm->mem[addr + i] = b;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_OUTC)
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        if (ULVM_INBOUNDS(addr, 1))
            vm->outputchar((char)vm->mem[addr]);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_OUT)
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        if (ULVM_INBOUNDS(addr, 1))
            ulvm_print_uint(vm, vm->mem[addr]);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_INPUT)
    {
        uint32_t chars = ULVM_NEXT4BYTES();
        uint32_t addr = ULVM_NEXT4BYTES();
        if (vm->input && ULVM_INBOUNDS(addr, 0))
        {
            uint32_t maxchars = chars;
            uint32_t available = (uint32_t)ULVM_MEM_SIZE - addr;
            if (maxchars > available)
                maxchars = available;
            vm->input((char *)&vm->mem[addr], maxchars);
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_PRINT)
    ULVM_CASE(OP_PRINTN)
    {
        uint32_t len = ULVM_NEXT2BYTES();
        for (uint32_t i = 0; i < len; i++)
            vm->outputchar((char)ULVM_NEXTBYTE());
        if (opbyte == OP_PRINT)
            vm->outputchar('\n');
        ULVM_NEXT();
    }

    ULVM_CASE(OP_PRINTV)
    ULVM_CASE(OP_PRINTVN)
    {
        uint32_t chars = ULVM_NEXT4BYTES();
        uint32_t addr = ULVM_NEXT4BYTES();
        uint32_t available = addr <= (uint32_t)ULVM_MEM_SIZE
            ? (uint32_t)ULVM_MEM_SIZE - addr : 0;
        if (chars > available)
            chars = available;
        for (uint32_t i = 0; i < chars; i++)
            vm->outputchar((char)vm->mem[addr + i]);
        if (opbyte == OP_PRINTV)
            vm->outputchar('\n');
        ULVM_NEXT();
    }

    ULVM_CASE(OP_CUR)
        ulvm_print_uint(vm, pc);
        ULVM_NEXT();

    ULVM_CASE(OP_MEMORY)
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
        ULVM_NEXT();
    }

    ULVM_CASE(OP_FLAGS)
        for (uint8_t f = 0; f < ULVM_FLAG_NUM; f++)
        {
            uint8_t v = ulvm_getflag(vm, f);
            for (int b = 3; b >= 0; b--)
                vm->outputchar((char)('0' + ((v >> b) & 1)));
        }
        vm->outputchar('\n');
        ULVM_NEXT();

    ULVM_CASE(OP_SETPC)
        pc = ULVM_NEXT4BYTES();
        ULVM_NEXT();

    ULVM_CASE(OP_SUBROUTINE)
    {
        uint32_t pos = ULVM_NEXT4BYTES();
        if (vm->callsp < ULVM_CALLSTACK_SIZE)
            vm->callstack[vm->callsp++] = pc;
        pc = pos;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_RETURN)
        if (vm->callsp > 0)
            pc = vm->callstack[--vm->callsp];
        ULVM_NEXT();

    ULVM_CASE(OP_SLEEP)
    {
        uint32_t ms = ULVM_NEXT4BYTES();
        if (vm->sleep)
            vm->sleep(ms);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_REFRESHSCREEN)
        if (vm->refreshscreen)
            vm->refreshscreen();
        if (vm->clearscreen)
            vm->clearscreen();
        ULVM_NEXT();

    ULVM_CASE(OP_ROMREAD8)
    ULVM_CASE(OP_ROMREAD16)
    ULVM_CASE(OP_ROMREAD32)
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rd = (uint8_t)ULVM_NEXTBYTE();
        int size = (opbyte == OP_ROMREAD8) ? 1 : (opbyte == OP_ROMREAD16) ? 2 : 4;
        ULVM_R(rd) = ulvm_rom_read(vm, ULVM_R(ra), size);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_ROMREAD64)
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rhi = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rlo = (uint8_t)ULVM_NEXTBYTE();
        uint64_t value = 0;
        uint32_t addr = ULVM_R(ra);
        for (uint8_t i = 0; i < 8; i++)
            value = (value << 8) | (uint64_t)ulvm_rom_byte(vm, addr + i);
        ULVM_R(rhi) = (uint32_t)(value >> 32);
        ULVM_R(rlo) = (uint32_t)value;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SETPIXEL)
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        if (vm->setpixel)
            vm->setpixel((uint8_t)ULVM_R(rx), (uint8_t)ULVM_R(ry), (uint16_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_GETPIXEL)
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(rdst) = vm->getpixel ? vm->getpixel((uint8_t)ULVM_R(rx), (uint8_t)ULVM_R(ry)) : 0;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_GETKEYBOARD)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = vm->getkeyboard ? vm->getkeyboard() : 0;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_GETTIME)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = vm->gettime ? vm->gettime() : 0;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_GETMEMSIZE)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = ULVM_MEM_SIZE;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_GETFLAG)
    {
        uint8_t flag = (uint8_t)ULVM_NEXTBYTE();
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(r) = ulvm_getflag(vm, flag);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_RANDREG)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t r2 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint32_t min = ULVM_R(r1);
        uint32_t max = ULVM_R(r2);
        if (max > min)
            ULVM_R(dst) = min + (ulvm_rand(vm) % (max - min));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SRANDREG)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ulvm_srand(vm, ULVM_R(r));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_DRAWSTRING)
    {
        uint8_t regx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t regy = (uint8_t)ULVM_NEXTBYTE();
        uint8_t regcolor = (uint8_t)ULVM_NEXTBYTE();
        uint8_t regaddr = (uint8_t)ULVM_NEXTBYTE();
        uint8_t reglen = (uint8_t)ULVM_NEXTBYTE();
        uint8_t src = (uint8_t)ULVM_NEXTBYTE();
        uint8_t regreturnx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t regreturny = (uint8_t)ULVM_NEXTBYTE();
        uint16_t retx, rety;
        ulvm_drawstring_mem(vm, (uint16_t)ULVM_R(regx), (uint16_t)ULVM_R(regy), (uint16_t)ULVM_R(regcolor),
                            ULVM_R(regaddr), ULVM_R(reglen), src, &retx, &rety);
        ULVM_R(regreturnx) = retx;
        ULVM_R(regreturny) = rety;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NUMTOSTR)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        uint8_t rlen = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(rlen) = ulvm_itoa(vm, ULVM_R(r), addr);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_FLOATOP)
    {
        uint8_t operation = (uint8_t)ULVM_NEXTBYTE();
        uint8_t format = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rleft = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rright = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdest = (uint8_t)ULVM_NEXTBYTE();
        if ((format != 7 && format != 8) || operation < 1 || operation > 11
            || !ulvm_ieee_format_supported(format))
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            break;
        }

        double left = ulvm_load_numeric(vm, ULVM_R(rleft), format);
        double right = ulvm_load_numeric(vm, ULVM_R(rright), format);
        uint32_t dest = ULVM_R(rdest);
        if (operation >= 6)
        {
            uint8_t result = operation == 6 ? left == right
                : operation == 7 ? left != right
                : operation == 8 ? left < right
                : operation == 9 ? left > right
                : operation == 10 ? left <= right
                : left >= right;
            ulvm_store_memory(vm, dest, 1, result);
            break;
        }

        if (format == 7)
        {
            float a = (float)left, b = (float)right, result;
            switch (operation)
            {
            case 1: result = a + b; break;
            case 2: result = a - b; break;
            case 3: result = a * b; break;
            case 4: result = a / b; break;
            case 5: result = -a; break;
            default: result = 0.0f; break;
            }
            ulvm_store_numeric(vm, dest, 7, (double)result);
        }
        else
        {
            double result;
            switch (operation)
            {
            case 1: result = left + right; break;
            case 2: result = left - right; break;
            case 3: result = left * right; break;
            case 4: result = left / right; break;
            case 5: result = -left; break;
            default: result = 0.0; break;
            }
            ulvm_store_numeric(vm, dest, 8, result);
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_FLOATCAST)
    {
        uint8_t src_format = (uint8_t)ULVM_NEXTBYTE();
        uint8_t dst_format = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rsrc = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        if (!ulvm_numeric_format_valid(src_format) || !ulvm_numeric_format_valid(dst_format)
            || !ulvm_ieee_format_supported(src_format) || !ulvm_ieee_format_supported(dst_format))
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            break;
        }
        if (src_format <= 6 && dst_format <= 6)
        {
            ulvm_cast_integer(vm, ULVM_R(rsrc), src_format, ULVM_R(rdst), dst_format);
            break;
        }
        double value = ulvm_load_numeric(vm, ULVM_R(rsrc), src_format);
        ulvm_store_numeric(vm, ULVM_R(rdst), dst_format, value);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SETREG)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t val = ULVM_NEXT4BYTES();
        ULVM_R(r) = val;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_MOVREG)
    {
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint8_t src = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(dst) = ULVM_R(src);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_LOADREG8)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        ULVM_R(r) = (addr < (uint32_t)ULVM_MEM_SIZE) ? (uint32_t)vm->mem[addr] : 0u;
        ULVM_NEXT();
    }
    ULVM_CASE(OP_LOADREG16)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        ULVM_R(r) = ULVM_INBOUNDS(addr, 2) ? (((uint32_t)vm->mem[addr] << 8) | vm->mem[addr + 1]) : 0u;
        ULVM_NEXT();
    }
    ULVM_CASE(OP_LOADREG32)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        ULVM_R(r) = ULVM_INBOUNDS(addr, 4)
            ? (((uint32_t)vm->mem[addr] << 24) | ((uint32_t)vm->mem[addr + 1] << 16) | ((uint32_t)vm->mem[addr + 2] << 8) | vm->mem[addr + 3])
            : 0u;
        ULVM_NEXT();
    }
    ULVM_CASE(OP_STOREREG8)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        if (addr < (uint32_t)ULVM_MEM_SIZE) vm->mem[addr] = (uint8_t)ULVM_R(r);
        ULVM_NEXT();
    }
    ULVM_CASE(OP_STOREREG16)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        if (ULVM_INBOUNDS(addr, 2))
        {
            uint32_t v = ULVM_R(r);
            vm->mem[addr] = (uint8_t)(v >> 8);
            vm->mem[addr + 1] = (uint8_t)v;
        }
        ULVM_NEXT();
    }
    ULVM_CASE(OP_STOREREG32)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        if (ULVM_INBOUNDS(addr, 4))
        {
            uint32_t v = ULVM_R(r);
            vm->mem[addr] = (uint8_t)(v >> 24);
            vm->mem[addr + 1] = (uint8_t)(v >> 16);
            vm->mem[addr + 2] = (uint8_t)(v >> 8);
            vm->mem[addr + 3] = (uint8_t)v;
        }
        ULVM_NEXT();
    }

#define ULVM_BINOP(OPC, EXPR) \
    ULVM_CASE(OPC) \
    { \
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE(); \
        uint8_t r2 = (uint8_t)ULVM_NEXTBYTE(); \
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE(); \
        uint32_t a = ULVM_R(r1), b = ULVM_R(r2); \
        ULVM_R(dst) = (EXPR); \
        ULVM_NEXT(); \
    }
    ULVM_BINOP(OP_ADDREG, a + b)
    ULVM_BINOP(OP_SUBREG, a - b)
    ULVM_BINOP(OP_MULREG, a * b)
    ULVM_BINOP(OP_ORREG, a | b)
    ULVM_BINOP(OP_ANDREG, a & b)
    ULVM_BINOP(OP_EQREG, (a == b) ? 1u : 0u)
    ULVM_BINOP(OP_NEQREG, (a != b) ? 1u : 0u)
    ULVM_BINOP(OP_LTREG, (a < b) ? 1u : 0u)
    ULVM_BINOP(OP_GTREG, (a > b) ? 1u : 0u)
    ULVM_BINOP(OP_LEREG, (a <= b) ? 1u : 0u)
    ULVM_BINOP(OP_GEREG, (a >= b) ? 1u : 0u)
    ULVM_BINOP(OP_SLTREG, ((int32_t)a < (int32_t)b) ? 1u : 0u)
    ULVM_BINOP(OP_SGTREG, ((int32_t)a > (int32_t)b) ? 1u : 0u)
    ULVM_BINOP(OP_SLEREG, ((int32_t)a <= (int32_t)b) ? 1u : 0u)
    ULVM_BINOP(OP_SGEREG, ((int32_t)a >= (int32_t)b) ? 1u : 0u)
    ULVM_BINOP(OP_SHRREG, (b < 32) ? (a >> b) : 0u)
    ULVM_BINOP(OP_SHLREG, (b < 32) ? (a << b) : 0u)
#undef ULVM_BINOP

    ULVM_CASE(OP_DIVREG)
    ULVM_CASE(OP_MODREG)
    ULVM_CASE(OP_COMPREG)
    ULVM_CASE(OP_NORREG)
    ULVM_CASE(OP_NANDREG)
    ULVM_CASE(OP_XORREG)
    ULVM_CASE(OP_XNORREG)
    ULVM_CASE(OP_SDIVREG)
    ULVM_CASE(OP_SMODREG)
    ULVM_CASE(OP_SSHRREG)
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
        case OP_SDIVREG:
            if ((int32_t)b != 0)
                ULVM_R(dst) = (a == 0x80000000u && (int32_t)b == -1)
                    ? a : (uint32_t)((int32_t)a / (int32_t)b);
            break;
        case OP_SMODREG:
            if ((int32_t)b != 0)
                ULVM_R(dst) = (a == 0x80000000u && (int32_t)b == -1)
                    ? 0u : (uint32_t)((int32_t)a % (int32_t)b);
            break;
        case OP_SSHRREG:
            if (b >= 32)
                ULVM_R(dst) = (a & 0x80000000u) ? UINT32_MAX : 0u;
            else if (b == 0)
                ULVM_R(dst) = a;
            else
                ULVM_R(dst) = (a >> b) | ((a & 0x80000000u) ? (UINT32_MAX << (32 - b)) : 0u);
            break;
        default: break;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_ADDREGV)
    ULVM_CASE(OP_SUBREGV)
    ULVM_CASE(OP_MULREGV)
    ULVM_CASE(OP_ANDREGV)
    ULVM_CASE(OP_ORREGV)
    ULVM_CASE(OP_XORREGV)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint32_t val = ULVM_NEXT4BYTES();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint32_t a = ULVM_R(r1);
        if (opbyte == OP_ADDREGV) ULVM_R(dst) = a + val;
        else if (opbyte == OP_SUBREGV) ULVM_R(dst) = a - val;
        else if (opbyte == OP_MULREGV) ULVM_R(dst) = a * val;
        else if (opbyte == OP_ANDREGV) ULVM_R(dst) = a & val;
        else if (opbyte == OP_ORREGV) ULVM_R(dst) = a | val;
        else ULVM_R(dst) = a ^ val;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SHLREGV)
    ULVM_CASE(OP_SHRREGV)
    ULVM_CASE(OP_SSHRREGV)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint32_t imm = ULVM_NEXTBYTE();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint32_t a = ULVM_R(r1);
        if (opbyte == OP_SHLREGV) ULVM_R(dst) = (imm < 32) ? (a << imm) : 0u;
        else if (opbyte == OP_SHRREGV) ULVM_R(dst) = (imm < 32) ? (a >> imm) : 0u;
        else if (imm >= 32) ULVM_R(dst) = (a & 0x80000000u) ? UINT32_MAX : 0u;
        else if (imm == 0) ULVM_R(dst) = a;
        else ULVM_R(dst) = (a >> imm) | ((a & 0x80000000u) ? (UINT32_MAX << (32 - imm)) : 0u);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_CMPREGV)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint32_t val = ULVM_NEXT4BYTES();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        uint8_t cond = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(dst) = ulvm_cond(cond, ULVM_R(r1), val) ? 1u : 0u;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_JCMP)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t r2 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t cond = (uint8_t)ULVM_NEXTBYTE();
        uint32_t pos = ULVM_NEXT4BYTES();
        if (ulvm_cond(cond, ULVM_R(r1), ULVM_R(r2))) pc = pos;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_JCMPV)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint32_t val = ULVM_NEXT4BYTES();
        uint8_t cond = (uint8_t)ULVM_NEXTBYTE();
        uint32_t pos = ULVM_NEXT4BYTES();
        if (ulvm_cond(cond, ULVM_R(r1), val)) pc = pos;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_FILLRECT)
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rw = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rh = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        ulvm_fillrect(vm, (int32_t)ULVM_R(rx), (int32_t)ULVM_R(ry), (int32_t)ULVM_R(rw), (int32_t)ULVM_R(rh), (uint8_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_DRAWLINE)
    {
        uint8_t rx0 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry0 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rx1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        ulvm_drawline(vm, (int32_t)ULVM_R(rx0), (int32_t)ULVM_R(ry0), (int32_t)ULVM_R(rx1), (int32_t)ULVM_R(ry1), (uint8_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_BLIT)
    {
        uint32_t d = ULVM_NEXT4BYTES();
        ulvm_blit(vm, d);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_IFREG)
    ULVM_CASE(OP_IFNOTREG)
    ULVM_CASE(OP_IFSUBREG)
    ULVM_CASE(OP_IFNOTSUBREG)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t pos = ULVM_NEXT4BYTES();
        int take = (opbyte == OP_IFREG || opbyte == OP_IFSUBREG) ? (ULVM_R(r) != 0) : (ULVM_R(r) == 0);
        if (take)
        {
            if ((opbyte == OP_IFSUBREG || opbyte == OP_IFNOTSUBREG) && vm->callsp < ULVM_CALLSTACK_SIZE)
                vm->callstack[vm->callsp++] = pc;
            pc = pos;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NOTREG)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(dst) = ~ULVM_R(r1);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_LOADREGI8)
    ULVM_CASE(OP_LOADREGI16)
    ULVM_CASE(OP_LOADREGI32)
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rd = (uint8_t)ULVM_NEXTBYTE();
        uint8_t size = (opbyte == OP_LOADREGI8) ? 1 : (opbyte == OP_LOADREGI16) ? 2 : 4;
        uint32_t addr = ULVM_R(ra);
        ULVM_R(rd) = ulvm_load_memory(vm, addr, size);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_STOREREGI8)
    ULVM_CASE(OP_STOREREGI16)
    ULVM_CASE(OP_STOREREGI32)
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rv = (uint8_t)ULVM_NEXTBYTE();
        uint8_t size = (opbyte == OP_STOREREGI8) ? 1 : (opbyte == OP_STOREREGI16) ? 2 : 4;
        uint32_t addr = ULVM_R(ra);
        ulvm_store_memory(vm, addr, size, ULVM_R(rv));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_OUTREG)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ulvm_print_uint(vm, ULVM_R(r));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_OUTREGSIGNED)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        ulvm_print_int(vm, (int32_t)ULVM_R(r));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_EXIT)
        vm->running = 0;
        break;

    default:
#if ULVM_COMPUTED_GOTO
    ulvm_l_default:
#endif
        vm->faulted = 1;
        vm->fault_pc = instruction_pc;
        vm->fault_opcode = opbyte;
        vm->running = 0;
        break;
    }
    }

#if ULVM_COMPUTED_GOTO
ulvm_out:
#endif
    vm->pc = pc;
    return !vm->running;
}

// Executes exactly one instruction (kept for compatibility).
static inline int ULVM_handlenextinstruction(UnilangVM *vm)
{
    return ULVM_run(vm, 1);
}