#pragma once
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

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
3 - enable 2-bit RGB555 palette graphics (64x64) (2 x 64 x 64 = 8192 bits = 1024 bytes = 1 KB)
4 - enable 8-bit color graphics (64x64) (8 x 64 x 64 = 32768 bits = 4096 bytes = 4 KB)
5 - enable 8-bit RGB555 palette graphics (64x64) (8 x 64 x 64 = 32768 bits = 4096 bytes = 4 KB)
6 - enable monochrome graphics (128x64) (1 x 128 x 64 = 8192 bits = 1024 bytes = 1 KB)
7 - enable 2-bit grayscale graphics (4 shades, 128x64) (2 x 128 x 64 = 16384 bits = 2048 bytes = 2 KB)
8 - enable 2-bit RGB555 palette graphics (128x64) (2 x 128 x 64 = 16384 bits = 2048 bytes = 2 KB)
9 - enable 8-bit color graphics (128x64) (8 x 128 x 64 = 65536 bits = 8192 bytes = 8 KB)
10 - enable 8-bit RGB555 palette graphics (128x64) (8 x 128 x 64 = 65536 bits = 8192 bytes = 8 KB)
11 - enable 16-bit RGB555 color graphics (128x128) (16 x 128 x 128 = 262144 bits = 32768 bytes = 32 KB)
12 - enable 16-bit RGB555 color graphics (256x128) (16 x 256 x 128 = 524288 bits = 65536 bytes = 64 KB)
Palette modes (3, 5, 8, 10): setpixel/drawchar/fillrect values are palette indices (0-3 or 0-255). The host turns an index into an RGB555 color
through the setpalette(index, rgb555) opcode. enablegraphics() receives colorsize = 0x80 | bits per pixel for these modes.

- flag 1 (keyboard) (this will consume the last n bits of memory (after graphics if it's used)) (Only gets supported once graphics are supported and on):
0 - disable keyboard
1 - NES style keyboard (Up + Down + Left + Right + A + B + select + start) (1 x 8 = 8 bits)
On the SDL desktop host the buttons map to keys: (Up + Down + Left + Right + A=C + B=V + Select=Backspace + Start=Enter)
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
    OP_SETPALETTE,       // set a palette entry, palette graphics modes only (flag 0 = 3, 5, 8, 10) - parameters: regindex(1), regcolor(1) (RGB555)
    OP_SAVE,             // save a VM RAM block through the host - parameters: regaddr(1), reglen(1), regresult(1)
    OP_LOAD,             // load a VM RAM block through the host - parameters: regaddr(1), reglen(1), regresult(1)
    OP_BEEP,             // start a host-side tone - parameters: regfreq(1), regms(1)
    OP_AUDIO_COMMAND,    // send a host audio command - parameters: command(1), rega(1), regb(1), regresult(1)
    OP_AUDIO_LOAD,       // copy audio data to the host - parameters: regkind(1), regindex(1), regaddr(1), reglen(1), source(1), regresult(1)
    OP_NET_OPEN = 0x90,  // open a host network handle - parameters: regkind(1), reglocal(1), regremote(1), reghostaddr(1), regflags(1), regresult(1)
    OP_NET_STATUS,       // query a handle state - parameters: reghandle(1), regresult(1)
    OP_NET_SEND,         // send memory bytes through a handle - parameters: reghandle(1), regaddr(1), reglen(1), regresult(1)
    OP_NET_RECV,         // receive bytes into VM memory - parameters: reghandle(1), regaddr(1), regcap(1), regresult(1)
    OP_NET_ACCEPT,       // accept a peer on a listening handle - parameters: reghandle(1), regresult(1)
    OP_NET_CLOSE,        // close a handle - parameters: reghandle(1), regresult(1)
    OP_NET_OPTION,       // set a handle option - parameters: reghandle(1), regoption(1), regvalue(1), regresult(1)
    OP_NET_AVAIL,        // how many bytes are pending - parameters: reghandle(1), regresult(1)
    OP_NET_LOCALIP,      // store the local IP string at regaddr - parameters: regaddr(1), regresult(1)
    OP_NET_PEER,         // copy the peer host/port into VM memory - parameters: reghandle(1), reghostaddr(1), regportaddr(1), regresult(1)
    OP_NET_LASTERROR,    // get the last network error - parameters: regresult(1)
    OP_NET_HTTP_REQUEST = 0x9B, // parameters: reghandle, regmethod, regpath, regheaders, regbody, reglen, regresult
    OP_NET_HTTP_STATUS,  // parameters: reghandle, regresult
    OP_NET_DISCOVERY,    // parameters: regflags, regport, reggameid, regnameaddr, regresult
    OP_NET_DISCOVERY_COUNT, // parameters: regresult
    OP_NET_DISCOVERY_GET, // parameters: regindex, regnameaddr, reghostaddr, regportaddr, reggameidaddr, regresult
    OP_NET_LOCALPORT = 0xA0, // parameters: reghandle, regresult
    OP_MEMCPY = 0x2D,    // copy VM memory to RAM or ROM to RAM; validates ranges before copying - parameters: regdst(1), regsrc(1), reglen(1), source(1) [0=RAM,1=ROM]
    OP_MEMSET,           // fill a VM memory range; validates the range before writing - parameters: regdst(1), regvalue(1), reglen(1)
    OP_FILL_SCREEN = 0x2F, // fill the active framebuffer - parameters: regcolor(1)
    OP_FILL_CIRCLE = 0x31, // fill a clipped circle - parameters: regx(1), regy(1), regradius(1), regc(1)
    OP_DRAW_RECT,        // draw a clipped outline rectangle; dimensions are edge-inclusive - parameters: regx(1), regy(1), regw(1), regh(1), regc(1)
    OP_JMPREG = 0x37,    // jump to a bytecode address held in a register - parameters: regtarget(1)
    OP_CALLREG,          // call a bytecode address held in a register - parameters: regtarget(1)
    OP_PUSHR = 0x39,     // push a register value onto the value stack - parameters: regsrc(1)
    OP_POPR,             // pop a value into a register - parameters: regdst(1)
    OP_OUTFMT = 0x3B,    // formatted numeric output: addr(4), format, conversion, width, precision, flags
    OP_MINREG = 0x33,    // minimum of two 32-bit values - parameters: rleft(1), rright(1), rdst(1), signed(1)
    OP_MAXREG,           // maximum of two 32-bit values - parameters: rleft(1), rright(1), rdst(1), signed(1)
    OP_ABSREG,           // two's-complement absolute value - parameters: rsrc(1), rdst(1)
    OP_MUL64,            // 32x32 to 64-bit product in two registers - parameters: rleft(1), rright(1), rhigh(1), rlow(1), signed(1)

    OP_SETREG64 = 0x3C,  // dst:reg, value:u64
    OP_LOADREG64,        // dst:reg, addr:u32
    OP_STOREREG64,       // src:reg, addr:u32
    OP_LOADREGI64,       // addr:reg, dst:reg
    OP_ALU64 = 0x30,     // selector:u8, left:reg, right:reg, dst:reg
    OP_OUTREG64 = 0x5E,  // value:reg
    OP_OUTREGSIGNED64,   // value:reg
    OP_STOREREGI64 = 0x7E, // addr:reg, src:reg
    OP_NUMTOSTR64,       // value:reg, addr:u32, out_length:reg

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
typedef void     (*vm_fillrect_fast)(uint8_t x, uint8_t y, uint16_t w, uint16_t h, uint16_t color);
typedef void     (*vm_blitrow_fast)(uint8_t x, uint8_t y, uint16_t count,
                                    const uint16_t *colors, const uint8_t *opaque);
typedef uint16_t (*vm_getpixel)(uint8_t x, uint8_t y);
typedef uint8_t  (*vm_getkeyboard)(void);
typedef uint64_t (*vm_gettime)(void);
typedef void     (*vm_sleep)(uint32_t ms);
typedef void     (*vm_refreshscreen)(void);
typedef uint32_t (*vm_input)(char *buf, uint32_t maxchars);
typedef void     (*vm_enablegraphics)(uint16_t wx, uint16_t wy, uint8_t colorsize);
typedef void     (*vm_disablegraphics)(void);
typedef void     (*vm_clearscreen)(void);
typedef void     (*vm_setpalette)(uint8_t index, uint16_t color);
typedef int      (*vm_storage)(uint8_t *data, uint32_t len);
typedef void     (*vm_beep)(uint32_t frequency, uint32_t duration_ms);
typedef uint32_t (*vm_audio_command)(uint8_t command, uint32_t a, uint32_t b);
typedef int      (*vm_audio_load)(uint8_t kind, uint8_t index, const uint8_t *data, uint32_t len);

#define ULVM_NET_MAX_HANDLES 8u
#define ULVM_NET_MAX_BUFFER 2048u
#define ULVM_NET_MAX_HOST 64u
#define ULVM_NET_MAX_TRANSFER ULVM_NET_MAX_BUFFER
#define ULVM_NET_DEFAULT_LOCAL_PORT 0u
#define ULVM_NET_OPEN_LISTEN 1u
#define ULVM_NET_OPEN_BROADCAST 2u
#define ULVM_NET_DISCOVERY_PORT 45678u
#define ULVM_NET_MAX_HTTP_HEADERS 512u
#define ULVM_NET_MAX_SESSIONS 8u
#define ULVM_NET_DISCOVERY_TTL_MS 5000u
#define ULVM_NET_DISCOVERY_LISTEN 1u
#define ULVM_NET_DISCOVERY_ANNOUNCE 2u

enum ULVM_NET_KIND
{
    ULVM_NET_KIND_NONE = 0,
    ULVM_NET_KIND_TCP = 1,
    ULVM_NET_KIND_UDP = 2,
    ULVM_NET_KIND_HTTP = 3
};

enum ULVM_NET_STATUS
{
    ULVM_NET_STATUS_CLOSED = 0,
    ULVM_NET_STATUS_LISTENING = 1,
    ULVM_NET_STATUS_CONNECTING = 2,
    ULVM_NET_STATUS_CONNECTED = 3,
    ULVM_NET_STATUS_READY = 4
};

enum ULVM_NET_ERRORS
{
    ULVM_NET_ERR_NONE = 0,
    ULVM_NET_ERR_INVALID_HANDLE,
    ULVM_NET_ERR_BAD_KIND,
    ULVM_NET_ERR_NO_BUFFER,
    ULVM_NET_ERR_TIMEOUT,
    ULVM_NET_ERR_IO,
    ULVM_NET_ERR_UNSUPPORTED,
    ULVM_NET_ERR_LIMIT
};

enum ULVM_NET_OPTIONS
{
    ULVM_NET_OPT_TIMEOUT_MS = 1,
    ULVM_NET_OPT_TCP_NODELAY = 2,
    ULVM_NET_OPT_FRAME_SIZE = 3,
    ULVM_NET_OPT_HTTP_HEADERS = 4,
    ULVM_NET_OPT_UDP_BROADCAST = 5
};

enum ULVM_NET_HTTP_METHOD
{
    ULVM_NET_HTTP_GET = 1,
    ULVM_NET_HTTP_POST = 2
};

typedef struct ULVMNetHandle
{
    uint8_t active;
    uint8_t kind;
    uint8_t status;
    uint8_t flags;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t last_error;
    uint8_t peer_host[ULVM_NET_MAX_HOST];
    uint16_t peer_port;
    uint16_t http_status;
    uint32_t timeout_ms;
    uint16_t frame_size;
    char http_headers[ULVM_NET_MAX_HTTP_HEADERS];
    uint8_t rx[ULVM_NET_MAX_BUFFER];
    uint32_t rx_len;
    uint8_t tx[ULVM_NET_MAX_BUFFER];
    uint32_t tx_len;
    void *host_data;
} ULVMNetHandle;

struct ULVMNet;

typedef struct ULVMNetOps
{
    int (*open)(void *context, ULVMNetHandle *handle, uint8_t kind,
                uint16_t local_port, const char *remote_host,
                uint16_t remote_port, uint32_t flags);
    void (*poll)(void *context, ULVMNetHandle *handle);
    int (*send)(void *context, ULVMNetHandle *handle, const void *data, uint32_t len);
    int (*recv)(void *context, ULVMNetHandle *handle, void *dest, uint32_t cap);
    int (*accept)(void *context, ULVMNetHandle *listener, ULVMNetHandle *peer);
    int (*option)(void *context, ULVMNetHandle *handle, uint8_t option, uint32_t value);
    int (*http_request)(void *context, ULVMNetHandle *handle, uint8_t method,
                        const char *path, const char *headers,
                        const void *body, uint32_t body_len);
    void (*close)(void *context, ULVMNetHandle *handle);
    int (*peer)(void *context, ULVMNetHandle *handle, char *host,
                size_t host_size, uint16_t *port);
    int (*localip)(void *context, char *address, size_t address_size);
    int (*discovery)(void *context, struct ULVMNet *net, uint8_t flags,
                     uint16_t game_port, uint32_t game_id, const char *name);
    void (*shutdown)(void *context, struct ULVMNet *net);
    void (*service)(void *context, struct ULVMNet *net);
} ULVMNetOps;

typedef struct ULVMNetSession
{
    uint8_t active;
    char name[32];
    char host[ULVM_NET_MAX_HOST];
    uint16_t port;
    uint32_t game_id;
    uint32_t last_seen_ms;
} ULVMNetSession;

typedef struct ULVMNet
{
    ULVMNetHandle handles[ULVM_NET_MAX_HANDLES];
    uint32_t last_error;
    char local_ip[32];
    const ULVMNetOps *ops;
    void *ops_context;
    uint8_t discovery_flags;
    uint16_t discovery_game_port;
    uint32_t discovery_game_id;
    char discovery_name[32];
    ULVMNetSession sessions[ULVM_NET_MAX_SESSIONS];
} ULVMNet;

#define ULVM_AUDIO_LOAD_MAX 68
#define ULVM_AUDIO_SFX_BYTES (4u + 32u * 2u)
#define ULVM_AUDIO_MUSIC_BYTES 4u

enum ULVM_AUDIO_COMMANDS
{
    ULVM_AUDIO_SFX = 1,
    ULVM_AUDIO_MUSIC,
    ULVM_AUDIO_STOP,
    ULVM_AUDIO_PAUSE,
    ULVM_AUDIO_RESUME,
    ULVM_AUDIO_VOLUME,
    ULVM_AUDIO_STATUS
};

enum ULVM_AUDIO_DATA_KINDS
{
    ULVM_AUDIO_DATA_SFX = 0,
    ULVM_AUDIO_DATA_MUSIC = 1
};

#define ULVM_FLAG_NUM 16
#define ULVM_FLAG_SIZE 4
#define ULVM_FLAG_NETWORK 15u

#ifndef ULVM_MEM_SIZE
#define ULVM_MEM_SIZE 64 // in bytes
#warning "ULVM_MEM_SIZE not defined, defaulting to 64 bytes"
#endif

#define ULVM_CALLSTACK_SIZE 16
#define ULVM_VALUESTACK_SIZE 64

struct UnilangVM
{
    uint32_t pc;
    int running;
    uint32_t exit_code; // r0 at OP_EXIT
    int faulted;
    uint32_t fault_pc;
    uint8_t fault_opcode;
    uint32_t *instruction_hits;

    uint8_t mem[ULVM_MEM_SIZE];
    uint8_t flags[(ULVM_FLAG_NUM * ULVM_FLAG_SIZE) / 8];
    uint64_t reg[16];

    uint32_t callstack[ULVM_CALLSTACK_SIZE];
    uint8_t callsp;
    uint32_t valuestack[ULVM_VALUESTACK_SIZE];
    uint8_t valuesp;

    vm_outputchar outputchar;
    vm_readbyte readbyte;
    const uint8_t *rom;      // optional flat bytecode (see ULVM_set_rom); NULL = use readbyte()
    uint32_t rom_size;
    vm_isflagsupported isflagsupported;
    uint8_t graphics_mode;

    vm_setpixel setpixel;
    vm_fillrect_fast fillrect_fast;
    vm_blitrow_fast blitrow_fast;
    vm_getpixel getpixel;
    vm_getkeyboard getkeyboard;
    vm_gettime gettime;
    vm_sleep sleep;
    vm_refreshscreen refreshscreen;
    vm_input input;
    vm_enablegraphics enablegraphics;
    vm_disablegraphics disablegraphics;
    vm_clearscreen clearscreen;
    vm_setpalette setpalette;
    vm_storage save;
    vm_storage load;
    vm_beep beep;
    vm_audio_command audio_command;
    vm_audio_load audio_load;
    ULVMNet *net;

    uint32_t rand_state;
    uint16_t gfx_w, gfx_h;
    int64_t cursor_x, cursor_y;
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

static inline void ULVM_set_graphics_fast(UnilangVM *vm, vm_fillrect_fast fillrect_fast,
                                          vm_blitrow_fast blitrow_fast)
{
    vm->fillrect_fast = fillrect_fast;
    vm->blitrow_fast = blitrow_fast;
}

static inline void ULVM_set_palette(UnilangVM *vm, vm_setpalette setpalette)
{
    vm->setpalette = setpalette;
}

static inline void ULVM_set_storage(UnilangVM *vm, vm_storage save, vm_storage load)
{
    vm->save = save;
    vm->load = load;
}

static inline void ULVM_set_audio(UnilangVM *vm, vm_beep beep,
                                  vm_audio_command audio_command, vm_audio_load audio_load)
{
    vm->beep = beep;
    vm->audio_command = audio_command;
    vm->audio_load = audio_load;
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

static inline void ULVM_set_net(UnilangVM *vm, ULVMNet *net)
{
    vm->net = net;
}

static inline void ULVM_net_set_ops(ULVMNet *net, const ULVMNetOps *ops, void *context)
{
    if (!net) return;
    net->ops = ops;
    net->ops_context = context;
}

static inline void ULVM_net_init(ULVMNet *net)
{
    if (!net)
        return;
    memset(net, 0, sizeof(*net));
    snprintf(net->local_ip, sizeof(net->local_ip), "127.0.0.1");
}

static inline const ULVMNetHandle *ulvm_net_get_handle(const ULVMNet *net, int handle)
{
    if (!net || handle < 0 || (uint32_t)handle >= ULVM_NET_MAX_HANDLES)
        return NULL;
    return &net->handles[handle];
}

static inline int ULVM_net_open(ULVMNet *net, uint8_t kind, uint16_t local_port,
                                const char *remote_host, uint16_t remote_port, uint32_t flags)
{
    if (!net)
        return -1;
    if (kind < ULVM_NET_KIND_TCP || kind > ULVM_NET_KIND_HTTP)
    {
        net->last_error = ULVM_NET_ERR_BAD_KIND;
        return -1;
    }

    if (!net->ops && kind != ULVM_NET_KIND_UDP)
    {
        net->last_error = ULVM_NET_ERR_UNSUPPORTED;
        return -1;
    }

    for (uint32_t i = 0; i < ULVM_NET_MAX_HANDLES; ++i)
    {
        ULVMNetHandle *slot = &net->handles[i];
        if (slot->active)
            continue;
        memset(slot, 0, sizeof(*slot));
        slot->active = 1;
        slot->kind = kind;
        slot->flags = (uint8_t)flags;
        slot->local_port = local_port;
        slot->remote_port = remote_port;
        slot->status = ULVM_NET_STATUS_READY;
        if (remote_host && *remote_host)
        {
            if (strlen(remote_host) >= sizeof(slot->peer_host))
            {
                memset(slot, 0, sizeof(*slot));
                net->last_error = ULVM_NET_ERR_NO_BUFFER;
                return -1;
            }
            strncpy((char *)slot->peer_host, remote_host, sizeof(slot->peer_host) - 1);
            slot->peer_host[sizeof(slot->peer_host) - 1] = '\0';
        }
        if (net->ops)
        {
            if (!net->ops->open || !net->ops->open(net->ops_context, slot, kind,
                                                   local_port, remote_host,
                                                   remote_port, flags))
            {
                net->last_error = slot->last_error ? slot->last_error : ULVM_NET_ERR_IO;
                memset(slot, 0, sizeof(*slot));
                return -1;
            }
        }
        net->last_error = ULVM_NET_ERR_NONE;
        return (int)i;
    }

    net->last_error = ULVM_NET_ERR_LIMIT;
    return -1;
}

static inline uint32_t ULVM_net_avail(const ULVMNet *net, int handle)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (slot && slot->active && net->ops && net->ops->poll)
        net->ops->poll(net->ops_context, slot);
    if (slot && slot->last_error && net) ((ULVMNet *)net)->last_error = slot->last_error;
    if (slot && slot->active && slot->frame_size)
    {
        if (slot->rx_len < 2) return 0;
        uint32_t frame_len = ((uint32_t)slot->rx[0] << 8) | slot->rx[1];
        if (frame_len > slot->frame_size || frame_len > ULVM_NET_MAX_TRANSFER)
        {
            slot->last_error = ULVM_NET_ERR_NO_BUFFER;
            slot->status = ULVM_NET_STATUS_CLOSED;
            slot->rx_len = 0;
            if (net) ((ULVMNet *)net)->last_error = ULVM_NET_ERR_NO_BUFFER;
            if (net && net->ops && net->ops->close)
                net->ops->close(net->ops_context, slot);
            return 0;
        }
        return slot->rx_len >= frame_len + 2u ? frame_len : 0u;
    }
    return slot ? slot->rx_len : 0u;
}

static inline int ULVM_net_close(ULVMNet *net, int handle)
{
    if (!net)
        return -1;
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!slot || !slot->active)
    {
        net->last_error = ULVM_NET_ERR_INVALID_HANDLE;
        return -1;
    }
    if (net->ops && net->ops->close)
        net->ops->close(net->ops_context, slot);
    slot->active = 0;
    slot->status = ULVM_NET_STATUS_CLOSED;
    slot->rx_len = 0;
    slot->tx_len = 0;
    slot->last_error = ULVM_NET_ERR_NONE;
    net->last_error = ULVM_NET_ERR_NONE;
    return 1;
}

static inline void ULVM_net_shutdown(ULVMNet *net)
{
    if (!net) return;
    for (uint32_t i = 0; i < ULVM_NET_MAX_HANDLES; ++i)
        if (net->handles[i].active)
            ULVM_net_close(net, (int)i);
    if (net->ops && net->ops->shutdown)
        net->ops->shutdown(net->ops_context, net);
}

static inline int ULVM_net_discovery(ULVMNet *net, uint8_t flags, uint16_t game_port,
                                     uint32_t game_id, const char *name)
{
    if (!net || !net->ops || !net->ops->discovery)
    { if (net) net->last_error = ULVM_NET_ERR_UNSUPPORTED; return -1; }
    if (flags & (uint8_t)~(ULVM_NET_DISCOVERY_LISTEN | ULVM_NET_DISCOVERY_ANNOUNCE))
    { net->last_error = ULVM_NET_ERR_BAD_KIND; return -1; }
    if ((flags & ULVM_NET_DISCOVERY_ANNOUNCE) && game_port == 0)
    { net->last_error = ULVM_NET_ERR_BAD_KIND; return -1; }
    if ((flags & ULVM_NET_DISCOVERY_ANNOUNCE) && (!name || strlen(name) >= sizeof(net->discovery_name)))
    { net->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
    int ok = net->ops->discovery(net->ops_context, net, flags, game_port, game_id, name ? name : "");
    if (ok <= 0)
    { net->last_error = ULVM_NET_ERR_UNSUPPORTED; return -1; }
    net->discovery_flags = flags;
    net->discovery_game_port = game_port;
    net->discovery_game_id = game_id;
    snprintf(net->discovery_name, sizeof(net->discovery_name), "%s", name ? name : "");
    net->last_error = ok == 2 ? ULVM_NET_ERR_UNSUPPORTED : ULVM_NET_ERR_NONE;
    return 1;
}

static inline uint32_t ULVM_net_discovery_count(ULVMNet *net)
{
    if (!net || !net->ops || !net->ops->discovery)
    { if (net) net->last_error = ULVM_NET_ERR_UNSUPPORTED; return UINT32_MAX; }
    net->ops->discovery(net->ops_context, net, net->discovery_flags,
                        net->discovery_game_port, net->discovery_game_id,
                        net->discovery_name);
    uint32_t count = 0;
    for (uint32_t i = 0; i < ULVM_NET_MAX_SESSIONS; ++i)
        if (net->sessions[i].active) ++count;
    return count;
}

static inline int ULVM_net_discovery_get(ULVMNet *net, uint32_t index,
                                         ULVMNetSession *session)
{
    if (!net || !session) return -1;
    if (!net->ops || !net->ops->discovery)
    { net->last_error = ULVM_NET_ERR_UNSUPPORTED; return -1; }
    (void)ULVM_net_discovery_count(net);
    for (uint32_t i = 0; i < ULVM_NET_MAX_SESSIONS; ++i)
    {
        if (!net->sessions[i].active) continue;
        if (index-- == 0) { *session = net->sessions[i]; return 1; }
    }
    return 0;
}

static inline int ULVM_net_status(const ULVMNet *net, int handle)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (slot && slot->active && net->ops && net->ops->poll)
        net->ops->poll(net->ops_context, slot);
    if (slot && slot->last_error && net) ((ULVMNet *)net)->last_error = slot->last_error;
    return slot ? (int)slot->status : ULVM_NET_STATUS_CLOSED;
}

static inline uint32_t ULVM_net_lasterror(const ULVMNet *net)
{
    return net ? net->last_error : ULVM_NET_ERR_NONE;
}

static inline int ULVM_net_http_request(ULVMNet *net, int handle, uint8_t method,
                                        const char *path, const char *headers,
                                        const void *body, uint32_t body_len)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!slot || !slot->active)
    { if (net) net->last_error = ULVM_NET_ERR_INVALID_HANDLE; return -1; }
    if (slot->kind != ULVM_NET_KIND_HTTP || !net->ops || !net->ops->http_request)
    { net->last_error = ULVM_NET_ERR_UNSUPPORTED; return -1; }
    if (!path || !headers || (!body && body_len) || body_len > ULVM_NET_MAX_TRANSFER)
    { net->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
    int rc = net->ops->http_request(net->ops_context, slot, method, path, headers, body, body_len);
    net->last_error = rc > 0 ? ULVM_NET_ERR_NONE :
                      (slot->last_error ? slot->last_error : ULVM_NET_ERR_IO);
    return rc > 0 ? 1 : -1;
}

static inline uint32_t ULVM_net_http_status(const ULVMNet *net, int handle)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!slot || !slot->active || slot->kind != ULVM_NET_KIND_HTTP) return 0u;
    if (net->ops && net->ops->poll) net->ops->poll(net->ops_context, slot);
    if (slot->last_error && net) ((ULVMNet *)net)->last_error = slot->last_error;
    return slot->http_status;
}

static inline const char *ULVM_net_localip(ULVMNet *net)
{
    if (net && net->ops && net->ops->localip)
    {
        if (!net->ops->localip(net->ops_context, net->local_ip, sizeof(net->local_ip)))
            net->last_error = ULVM_NET_ERR_IO;
    }
    return net ? net->local_ip : "0.0.0.0";
}

static inline uint32_t ULVM_net_localport(const ULVMNet *net, int handle)
{
    const ULVMNetHandle *slot = ulvm_net_get_handle(net, handle);
    return slot && slot->active ? slot->local_port : UINT32_MAX;
}

static inline int ULVM_net_peer(const ULVMNet *net, int handle, char *host, size_t host_size,
                               uint16_t *port)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!slot || !slot->active)
        return -1;
    if (net->ops && net->ops->peer)
        return net->ops->peer(net->ops_context, slot, host, host_size, port);
    if (host && host_size > 0)
    {
        snprintf(host, host_size, "%s", (const char *)slot->peer_host);
    }
    if (port)
        *port = slot->remote_port;
    return 1;
}

static inline int ULVM_net_send(ULVMNet *net, int handle, const void *data, uint32_t len)
{
    if (!net)
        return -1;
    if (!data || len == 0)
    { net->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
    ULVMNetHandle *sender = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!sender || !sender->active)
    {
        net->last_error = ULVM_NET_ERR_INVALID_HANDLE;
        return -1;
    }

    if (len > ULVM_NET_MAX_BUFFER)
    {
        sender->last_error = ULVM_NET_ERR_NO_BUFFER;
        net->last_error = ULVM_NET_ERR_NO_BUFFER;
        return -1;
    }

    if (net->ops)
    {
        int sent = net->ops->send ? net->ops->send(net->ops_context, sender, data, len) : -1;
        if (sent >= 0)
            sender->last_error = net->last_error = ULVM_NET_ERR_NONE;
        else
        {
            if (!sender->last_error) sender->last_error = ULVM_NET_ERR_IO;
            net->last_error = sender->last_error;
        }
        return sent;
    }

    for (uint32_t i = 0; i < ULVM_NET_MAX_HANDLES; ++i)
    {
        ULVMNetHandle *peer = &net->handles[i];
        if (!peer->active || peer == sender)
            continue;
        if (peer->kind != sender->kind)
            continue;
        if (peer->local_port != sender->remote_port)
            continue;
        if (peer->local_port == 0 || sender->local_port == 0)
            continue;
        if (peer->rx_len + len > ULVM_NET_MAX_BUFFER)
        {
            sender->last_error = ULVM_NET_ERR_NO_BUFFER;
            net->last_error = ULVM_NET_ERR_NO_BUFFER;
            return -1;
        }
        memcpy(peer->rx + peer->rx_len, data, len);
        peer->rx_len += len;
        peer->status = ULVM_NET_STATUS_READY;
        sender->tx_len = len;
        memcpy(sender->tx, data, len);
        sender->status = ULVM_NET_STATUS_READY;
        sender->last_error = net->last_error = ULVM_NET_ERR_NONE;
        return (int)len;
    }

    sender->last_error = net->last_error = ULVM_NET_ERR_IO;
    return -1;
}

static inline int ULVM_net_recv(ULVMNet *net, int handle, void *dest, uint32_t cap)
{
    if (!net)
        return 0;
    if (!dest || cap == 0)
    { net->last_error = ULVM_NET_ERR_NO_BUFFER; return 0; }
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!slot || !slot->active)
    {
        net->last_error = ULVM_NET_ERR_INVALID_HANDLE;
        return -1;
    }

    if (slot->frame_size)
    {
        if (net->ops && net->ops->poll) net->ops->poll(net->ops_context, slot);
        if (slot->last_error)
        { net->last_error = slot->last_error; return -1; }
        if (slot->rx_len < 2)
        { slot->last_error = net->last_error = ULVM_NET_ERR_NONE; return 0; }
        uint32_t frame_len = ((uint32_t)slot->rx[0] << 8) | slot->rx[1];
        if (frame_len > slot->frame_size || frame_len > ULVM_NET_MAX_TRANSFER)
        {
            slot->last_error = net->last_error = ULVM_NET_ERR_NO_BUFFER;
            slot->status = ULVM_NET_STATUS_CLOSED;
            slot->rx_len = 0;
            if (net->ops && net->ops->close) net->ops->close(net->ops_context, slot);
            return -1;
        }
        if (slot->rx_len < frame_len + 2u)
        { slot->last_error = net->last_error = ULVM_NET_ERR_NONE; return 0; }
        if (cap < frame_len)
        { slot->last_error = net->last_error = ULVM_NET_ERR_NO_BUFFER; return -1; }
        memcpy(dest, slot->rx + 2, frame_len);
        memmove(slot->rx, slot->rx + frame_len + 2u, slot->rx_len - frame_len - 2u);
        slot->rx_len -= frame_len + 2u;
        if (!slot->rx_len && slot->kind != ULVM_NET_KIND_UDP && slot->status == ULVM_NET_STATUS_READY)
            slot->status = ULVM_NET_STATUS_CONNECTED;
        slot->last_error = net->last_error = ULVM_NET_ERR_NONE;
        return (int)frame_len;
    }

    if (net->ops)
    {
        int got = net->ops->recv ? net->ops->recv(net->ops_context, slot, dest, cap) : -1;
        if (got >= 0)
            slot->last_error = net->last_error = ULVM_NET_ERR_NONE;
        else
        {
            if (!slot->last_error) slot->last_error = ULVM_NET_ERR_IO;
            net->last_error = slot->last_error;
        }
        return got;
    }

    if (slot->rx_len == 0)
    { slot->last_error = net->last_error = ULVM_NET_ERR_NONE;
        return 0;
    }

    uint32_t copied = (slot->rx_len < cap) ? slot->rx_len : cap;
    memcpy(dest, slot->rx, copied);
    memmove(slot->rx, slot->rx + copied, slot->rx_len - copied);
    slot->rx_len -= copied;
    slot->status = ULVM_NET_STATUS_READY;
    slot->last_error = net->last_error = ULVM_NET_ERR_NONE;
    return (int)copied;
}

static inline int ULVM_net_listen(ULVMNet *net, uint8_t kind, uint16_t port)
{
    return ULVM_net_open(net, kind, port, NULL, 0, ULVM_NET_OPEN_LISTEN);
}

static inline int ULVM_net_accept(ULVMNet *net, int handle)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!net || !slot || !slot->active)
    {
        if (net) net->last_error = ULVM_NET_ERR_INVALID_HANDLE;
        return -1;
    }
    if (!net->ops || !net->ops->accept)
    {
        net->last_error = ULVM_NET_ERR_UNSUPPORTED;
        return -1;
    }
    for (uint32_t i = 0; i < ULVM_NET_MAX_HANDLES; ++i)
    {
        ULVMNetHandle *peer = &net->handles[i];
        if (peer->active) continue;
        memset(peer, 0, sizeof(*peer));
        int accepted = net->ops->accept(net->ops_context, slot, peer);
        if (accepted > 0)
        {
            peer->active = 1;
            net->last_error = ULVM_NET_ERR_NONE;
            return (int)i;
        }
        memset(peer, 0, sizeof(*peer));
        if (accepted < 0)
            net->last_error = slot->last_error ? slot->last_error : ULVM_NET_ERR_IO;
        else
            net->last_error = ULVM_NET_ERR_NONE;
        return -1;
    }
    net->last_error = ULVM_NET_ERR_LIMIT;
    return -1;
}

static inline int ULVM_net_option(ULVMNet *net, int handle, uint8_t option, uint32_t value)
{
    ULVMNetHandle *slot = (ULVMNetHandle *)ulvm_net_get_handle(net, handle);
    if (!net || !slot || !slot->active)
    {
        if (net) net->last_error = ULVM_NET_ERR_INVALID_HANDLE;
        return -1;
    }
    (void)value;
    if (net->ops)
    {
        int ok = net->ops->option && net->ops->option(net->ops_context, slot, option, value);
        net->last_error = ok ? ULVM_NET_ERR_NONE :
                          (slot->last_error ? slot->last_error : ULVM_NET_ERR_UNSUPPORTED);
        return ok ? 1 : -1;
    }
    if (slot->kind != ULVM_NET_KIND_UDP || option != ULVM_NET_OPT_UDP_BROADCAST)
    {
        net->last_error = ULVM_NET_ERR_UNSUPPORTED;
        return -1;
    }
    net->last_error = ULVM_NET_ERR_UNSUPPORTED;
    return -1;
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

#define ULVM_NEXT8BYTES() ((((uint64_t)ULVM_NEXT4BYTES()) << 32) | ULVM_NEXT4BYTES())

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
#define ULVM_R32(i) ((uint32_t)ULVM_R(i))

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
    return format >= 1 && format <= 10;
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
    // palette modes report colorsize = 0x80 | bits per pixel (2 or 8); the host maps indices through setpalette()
    case 1: *wx = 64;  *wy = 64; *colorsize = 1; break;
    case 2: *wx = 64;  *wy = 64; *colorsize = 4; break;
    case 3: *wx = 64;  *wy = 64; *colorsize = 0x82; break;
    case 4: *wx = 64;  *wy = 64; *colorsize = 8; break;
    case 5: *wx = 64;  *wy = 64; *colorsize = 0x88; break;
    case 6: *wx = 128; *wy = 64; *colorsize = 1; break;
    case 7: *wx = 128; *wy = 64; *colorsize = 4; break;
    case 8: *wx = 128; *wy = 64; *colorsize = 0x82; break;
    case 9: *wx = 128; *wy = 64; *colorsize = 8; break;
    case 10: *wx = 128; *wy = 64; *colorsize = 0x88; break;
    case 11: *wx = 128; *wy = 128; *colorsize = 16; break;
    case 12: *wx = 256; *wy = 128; *colorsize = 16; break;
    default: *wx = 0; *wy = 0; *colorsize = 0; break;
    }
}

static inline int32_t ulvm_gw(UnilangVM *vm);
static inline int32_t ulvm_gh(UnilangVM *vm);

// Draws one glyph at a signed position; pixels outside the screen are skipped, so glyphs can be partly off any edge.
static inline void ulvm_drawchar(UnilangVM *vm, int64_t x, int64_t y, uint16_t color, uint8_t c)
{
    if (!vm->setpixel)
        return;
    if (c < 0x20 || c > 0x7F)
        c = 0x20;
    const uint8_t *glyph = ulvm_font4x6_basic[c - 0x20];
    int32_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    for (int row = 0; row < 6; row++)
    {
        int64_t py = y + row;
        if (py < 0 || py >= maxy)
            continue;
        uint8_t byte = READ_FLASH_BYTE(&glyph[row / 2]);  // the font lives in PROGMEM on AVR
        uint8_t nibble = (row % 2 == 0) ? ((byte >> 4) & 0xF) : (byte & 0xF);
        for (int col = 0; col < 4; col++)
        {
            int64_t px = x + col;
            if (px < 0 || px >= maxx)
                continue;
            if (nibble & (1 << col))
                vm->setpixel((uint8_t)px, (uint8_t)py, color);
        }
    }
}

static inline void ulvm_drawstring(UnilangVM *vm, uint16_t color, char c)
{
    if (c == '\n')
    {
        vm->cursor_x = 0;
        vm->cursor_y += ULVM_GLYPH_H;
        if (vm->cursor_y + 6 > vm->gfx_h)
        {
            vm->cursor_x = 0;
            vm->cursor_y = 0;
        }
    }
    else
    {
        ulvm_drawchar(vm, vm->cursor_x, vm->cursor_y, color, (uint8_t)c);
        vm->cursor_x += ULVM_GLYPH_W;
        // wrap only once the next glyph no longer fits; a single line placed partly off the
        // bottom or left edge keeps its position and is simply clipped
        if (vm->cursor_x + 4 > vm->gfx_w && vm->cursor_x > 0)
        {
            vm->cursor_x = 0;
            vm->cursor_y += ULVM_GLYPH_H;
            if (vm->cursor_y + 6 > vm->gfx_h)
            {
                vm->cursor_x = 0;
                vm->cursor_y = 0;
            }
        }
    }
}

static inline void ulvm_drawstring_mem(UnilangVM *vm, int32_t x, int32_t y, uint16_t color,
                                        uint32_t addr, uint32_t len, uint8_t rom,
                                        int32_t *retx, int32_t *rety)
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
            c = i <= UINT32_MAX - addr ? ulvm_rom_byte(vm, addr + i) : 0;
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

static inline uint8_t ulvm_itoa64(UnilangVM *vm, uint64_t v, uint32_t addr)
{
    char tmp[20];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < sizeof(tmp));
    uint32_t available = addr <= (uint32_t)ULVM_MEM_SIZE ? (uint32_t)ULVM_MEM_SIZE - addr : 0;
    for (uint8_t i = 0; i < n; ++i) {
        uint32_t offset = (uint32_t)n - 1u - i;
        if (offset < available) vm->mem[addr + offset] = (uint8_t)tmp[i];
    }
    return n;
}

static inline void ulvm_print_uint64(UnilangVM *vm, uint64_t value, uint8_t is_signed)
{
    char buf[21];
    uint8_t n = 0;
    uint64_t magnitude = value;
    if (is_signed && (int64_t)value < 0) {
        if (vm->outputchar) vm->outputchar('-');
        magnitude = (uint64_t)(0u - value);
    }
    do { buf[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude && n < sizeof(buf));
    while (n && vm->outputchar) vm->outputchar(buf[--n]);
}

static inline int ulvm_print_formatted(UnilangVM *vm, uint32_t addr, uint8_t format,
                                       uint8_t conversion, uint8_t width,
                                       uint8_t precision, uint8_t flags)
{
    uint8_t size = format <= 2 ? 1 : format <= 4 ? 2 : format <= 6 ? 4 : format == 7 ? 4 : 8;
    if (!ulvm_numeric_format_valid(format) || !ULVM_INBOUNDS(addr, size)
        || width > 99 || (precision != UINT8_MAX && precision > 99)
        || (flags & 0xE0) != 0)
        return 0;

    char spec[16] = "%";
    size_t n = 1;
    if (flags & 1) spec[n++] = '-';
    if (flags & 2) spec[n++] = '+';
    if (flags & 4) spec[n++] = ' ';
    if (flags & 8) spec[n++] = '#';
    if (flags & 16) spec[n++] = '0';
    if (width)
        n += (size_t)snprintf(spec + n, sizeof(spec) - n, "%u", (unsigned)width);
    if (precision != UINT8_MAX)
        n += (size_t)snprintf(spec + n, sizeof(spec) - n, ".%u", (unsigned)precision);

    char output[512];
    int written;
    uint32_t raw;
    int64_t signed_value;
    uint64_t unsigned_value;
    double float_value;

    if (conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E'
        || conversion == 'g' || conversion == 'G') {
        if (format != 7 && format != 8)
            return 0;
        spec[n++] = (char)conversion;
        spec[n] = '\0';
        float_value = ulvm_load_numeric(vm, addr, format);
        written = snprintf(output, sizeof(output), spec, float_value);
    } else if (conversion == 'c') {
        if (format > 6)
            return 0;
        spec[n++] = 'c';
        spec[n] = '\0';
        written = snprintf(output, sizeof(output), spec, (int)ulvm_load_memory(vm, addr, size));
    } else if (conversion == 'd' || conversion == 'i' || conversion == 'u'
               || conversion == 'x' || conversion == 'X' || conversion == 'o') {
        if (format == 7 || format == 8)
            return 0;
        if (format >= 9) {
            unsigned_value = ulvm_load_memory64(vm, addr);
            signed_value = (int64_t)unsigned_value;
        } else {
        raw = ulvm_load_memory(vm, addr, size);
        switch (format) {
        case 1: signed_value = (int8_t)raw; break;
        case 2: signed_value = (int64_t)raw; break;
        case 3: signed_value = (int16_t)raw; break;
        case 4: signed_value = (int64_t)raw; break;
        case 5: signed_value = (int32_t)raw; break;
        default: signed_value = (int64_t)raw; break;
        }
        unsigned_value = raw;
        }
        if (conversion == 'd' || conversion == 'i') {
            spec[n++] = 'l';
            spec[n++] = 'l';
            spec[n++] = (char)conversion;
            spec[n] = '\0';
            written = snprintf(output, sizeof(output), spec, (long long)signed_value);
        } else {
            spec[n++] = 'l';
            spec[n++] = 'l';
            spec[n++] = (char)conversion;
            spec[n] = '\0';
            written = snprintf(output, sizeof(output), spec, (unsigned long long)unsigned_value);
        }
    } else {
        return 0;
    }

    if (written < 0 || (size_t)written >= sizeof(output))
        return 0;
    for (int i = 0; i < written; i++)
        vm->outputchar(output[i]);
    return 1;
}

static inline int ulvm_mem_write_string(UnilangVM *vm, uint32_t addr, const char *text, uint32_t max_len)
{
    if (!vm || !text || max_len == 0 || addr >= (uint32_t)ULVM_MEM_SIZE)
        return 0;
    uint32_t out = 0;
    while (out + 1u < max_len && out + 1u < (uint32_t)ULVM_MEM_SIZE - addr)
    {
        char ch = text[out];
        vm->mem[addr + out] = (uint8_t)ch;
        if (ch == '\0')
            return 1;
        out++;
    }
    if (addr + out < (uint32_t)ULVM_MEM_SIZE)
        vm->mem[addr + out] = 0;
    return 1;
}

static inline int ulvm_mem_read_string(UnilangVM *vm, uint32_t addr, char *buf, uint32_t max_len)
{
    if (!vm || !buf || max_len == 0 || addr >= (uint32_t)ULVM_MEM_SIZE)
        return 0;
    uint32_t i = 0;
    while (i + 1u < max_len && i < (uint32_t)ULVM_MEM_SIZE - addr)
    {
        char ch = (char)vm->mem[addr + i];
        buf[i] = ch;
        if (ch == '\0')
            return 1;
        i++;
    }
    buf[0] = '\0';
    return 0;
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

static inline void ulvm_fillrect(UnilangVM *vm, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    if ((!vm->setpixel && !vm->fillrect_fast) || w <= 0 || h <= 0)
        return;
    int64_t x0 = x, y0 = y, x1 = x0 + (int64_t)w, y1 = y0 + (int64_t)h;
    int32_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > maxx) x1 = maxx;
    if (y1 > maxy) y1 = maxy;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (vm->fillrect_fast)
    {
        vm->fillrect_fast((uint8_t)x0, (uint8_t)y0,
                          (uint16_t)(x1 - x0), (uint16_t)(y1 - y0), c);
        return;
    }
    for (int64_t yy = y0; yy < y1; yy++)
        for (int64_t xx = x0; xx < x1; xx++)
            vm->setpixel((uint8_t)xx, (uint8_t)yy, c);
}

static inline int ulvm_clip_line(double *x0, double *y0, double *x1, double *y1,
                                 double xmin, double ymin, double xmax, double ymax)
{
    double dx = *x1 - *x0, dy = *y1 - *y0;
    double t0 = 0.0, t1 = 1.0;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {*x0 - xmin, xmax - *x0, *y0 - ymin, ymax - *y0};
    for (int i = 0; i < 4; i++)
    {
        if (p[i] == 0.0)
        {
            if (q[i] < 0.0)
                return 0;
            continue;
        }
        double t = q[i] / p[i];
        if (p[i] < 0.0)
        {
            if (t > t1)
                return 0;
            if (t > t0)
                t0 = t;
        }
        else
        {
            if (t < t0)
                return 0;
            if (t < t1)
                t1 = t;
        }
    }
    double ox = *x0, oy = *y0;
    *x0 = ox + t0 * dx;
    *y0 = oy + t0 * dy;
    *x1 = ox + t1 * dx;
    *y1 = oy + t1 * dy;
    return 1;
}

static inline void ulvm_drawline(UnilangVM *vm, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    if (!vm->setpixel)
        return;
    int32_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    double cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1;
    if (!ulvm_clip_line(&cx0, &cy0, &cx1, &cy1, 0.0, 0.0,
                        (double)(maxx - 1), (double)(maxy - 1)))
        return;
    x0 = (int32_t)(cx0 + 0.5);
    y0 = (int32_t)(cy0 + 0.5);
    x1 = (int32_t)(cx1 + 0.5);
    y1 = (int32_t)(cy1 + 0.5);
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;)
    {
        vm->setpixel((uint8_t)x0, (uint8_t)y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static inline uint32_t ulvm_isqrt_u64(uint64_t value)
{
    uint32_t low = 0, high = UINT32_MAX;
    while (low < high)
    {
        uint32_t mid = low + (uint32_t)(((uint64_t)high - low + 1) / 2);
        if ((uint64_t)mid * mid <= value)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

static inline void ulvm_fillcircle(UnilangVM *vm, int32_t x, int32_t y, int32_t radius, uint16_t color)
{
    if (radius < 0)
        return;
    int64_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    int64_t y0 = (int64_t)y - radius, y1 = (int64_t)y + radius;
    if (y0 < 0) y0 = 0;
    if (y1 >= maxy) y1 = maxy - 1;
    uint64_t radius2 = (uint64_t)radius * (uint64_t)radius;
    for (int64_t py = y0; py <= y1; ++py)
    {
        int64_t dy = py - y;
        uint32_t dx = ulvm_isqrt_u64(radius2 - (uint64_t)(dy * dy));
        int64_t x0 = (int64_t)x - dx, x1 = (int64_t)x + dx + 1;
        if (x0 < 0) x0 = 0;
        if (x1 > maxx) x1 = maxx;
        if (x0 < x1)
            ulvm_fillrect(vm, (int32_t)x0, (int32_t)py, (int32_t)(x1 - x0), 1, color);
    }
}

static inline void ulvm_drawrect(UnilangVM *vm, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (w <= 0 || h <= 0)
        return;
    int64_t right = (int64_t)x + w - 1;
    int64_t bottom = (int64_t)y + h - 1;
    int32_t right32 = right > INT32_MAX ? INT32_MAX : right < INT32_MIN ? INT32_MIN : (int32_t)right;
    int32_t bottom32 = bottom > INT32_MAX ? INT32_MAX : bottom < INT32_MIN ? INT32_MIN : (int32_t)bottom;
    ulvm_drawline(vm, x, y, right32, y, color);
    if (h > 1)
        ulvm_drawline(vm, x, bottom32, right32, bottom32, color);
    int64_t inner_top = (int64_t)y + 1, inner_bottom = bottom - 1;
    if (inner_top <= inner_bottom)
    {
        int32_t top32 = inner_top > INT32_MAX ? INT32_MAX : (int32_t)inner_top;
        int32_t bottom_inner32 = inner_bottom < INT32_MIN ? INT32_MIN
            : inner_bottom > INT32_MAX ? INT32_MAX : (int32_t)inner_bottom;
        ulvm_drawline(vm, x, top32, x, bottom_inner32, color);
        if (w > 1)
            ulvm_drawline(vm, right32, top32, right32, bottom_inner32, color);
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
    if (!vm->setpixel && !vm->blitrow_fast)
        return;
    int32_t x = (int32_t)ulvm_load_memory(vm, d + 0, 4), y = (int32_t)ulvm_load_memory(vm, d + 4, 4);
    int32_t w = (int32_t)ulvm_load_memory(vm, d + 8, 4), h = (int32_t)ulvm_load_memory(vm, d + 12, 4);
    uint32_t src = ulvm_load_memory(vm, d + 16, 4);
    int32_t sx = (int32_t)ulvm_load_memory(vm, d + 20, 4), sy = (int32_t)ulvm_load_memory(vm, d + 24, 4);
    int32_t stride = (int32_t)ulvm_load_memory(vm, d + 28, 4);
    uint32_t fl = ulvm_load_memory(vm, d + 32, 4), pal = ulvm_load_memory(vm, d + 36, 4);
    uint32_t tr = ulvm_load_memory(vm, d + 40, 4);
    if (w <= 0 || h <= 0 || stride <= 0)
        return;
    int64_t maxx = ulvm_gw(vm), maxy = ulvm_gh(vm);
    int64_t r0 = y < 0 ? -(int64_t)y : 0;
    int64_t c0 = x < 0 ? -(int64_t)x : 0;
    int64_t r1 = (int64_t)h < maxy - y ? h : maxy - y;
    int64_t c1 = (int64_t)w < maxx - x ? w : maxx - x;
    if (r1 > h) r1 = h;
    if (c1 > w) c1 = w;
    if (r0 >= r1 || c0 >= c1)
        return;
    uint16_t row_colors[256];
    uint8_t row_opaque[256];
    for (int64_t r = r0; r < r1; r++)
    {
        int64_t py = (int64_t)y + r;
        uint16_t row_count = (uint16_t)(c1 - c0);
        memset(row_opaque, 0, row_count);
        int64_t srow = (fl & 2) ? ((int64_t)h - 1 - r) : r;
        int64_t source_y = (int64_t)sy + srow;
        if (source_y < 0)
            continue;
        for (int64_t c = c0; c < c1; c++)
        {
            int64_t scol = (fl & 1) ? ((int64_t)w - 1 - c) : c;
            int64_t source_x = (int64_t)sx + scol;
            if (source_x < 0 || source_x >= stride)
                continue;
            uint64_t idx = (uint64_t)source_y * (uint32_t)stride + (uint64_t)source_x;
            uint64_t v;
            if (fl & 4)
            {
                uint64_t a = (uint64_t)src + (idx >> 1);
                uint32_t b = (fl & 32)
                    ? (a < (uint32_t)ULVM_MEM_SIZE ? vm->mem[(uint32_t)a] : 0u)
                    : (a <= UINT32_MAX ? (uint32_t)ulvm_rom_byte(vm, (uint32_t)a) : 0u);
                v = (idx & 1) ? (b >> 4) : (b & 15);
            }
            else
            {
                uint64_t a = (uint64_t)src + idx;
                v = (fl & 32)
                    ? (a < (uint32_t)ULVM_MEM_SIZE ? vm->mem[(uint32_t)a] : 0u)
                    : (a <= UINT32_MAX ? (uint32_t)ulvm_rom_byte(vm, (uint32_t)a) : 0u);
            }
            if ((fl & 16) && v == tr)
                continue;
            if (fl & 8)
                v = (uint64_t)pal + v < (uint32_t)ULVM_MEM_SIZE
                    ? vm->mem[(uint32_t)((uint64_t)pal + v)] : v;
            row_colors[c - c0] = (uint16_t)v;
            row_opaque[c - c0] = 1;
        }
        if (vm->blitrow_fast)
            vm->blitrow_fast((uint8_t)((int64_t)x + c0), (uint8_t)py,
                             row_count, row_colors, row_opaque);
        else
            for (uint16_t c = 0; c < row_count; ++c)
                if (row_opaque[c])
                    vm->setpixel((uint8_t)((int64_t)x + c0 + c), (uint8_t)py, row_colors[c]);
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
    [OP_SETPALETTE] = &&ulvm_l_OP_SETPALETTE, \
    [OP_SAVE] = &&ulvm_l_OP_SAVE, \
    [OP_LOAD] = &&ulvm_l_OP_LOAD, \
    [OP_MEMCPY] = &&ulvm_l_OP_MEMCPY, \
    [OP_MEMSET] = &&ulvm_l_OP_MEMSET, \
    [OP_FILL_SCREEN] = &&ulvm_l_OP_FILL_SCREEN, \
    [OP_FILL_CIRCLE] = &&ulvm_l_OP_FILL_CIRCLE, \
    [OP_DRAW_RECT] = &&ulvm_l_OP_DRAW_RECT, \
    [OP_JMPREG] = &&ulvm_l_OP_JMPREG, \
    [OP_CALLREG] = &&ulvm_l_OP_CALLREG, \
    [OP_PUSHR] = &&ulvm_l_OP_PUSHR, \
    [OP_POPR] = &&ulvm_l_OP_POPR, \
    [OP_MINREG] = &&ulvm_l_OP_MINREG, \
    [OP_MAXREG] = &&ulvm_l_OP_MAXREG, \
    [OP_ABSREG] = &&ulvm_l_OP_ABSREG, \
    [OP_MUL64] = &&ulvm_l_OP_MUL64, \
    [OP_BEEP] = &&ulvm_l_OP_BEEP, \
    [OP_AUDIO_COMMAND] = &&ulvm_l_OP_AUDIO_COMMAND, \
    [OP_AUDIO_LOAD] = &&ulvm_l_OP_AUDIO_LOAD, \
    [OP_NET_ACCEPT] = &&ulvm_l_OP_NET_ACCEPT, \
    [OP_NET_AVAIL] = &&ulvm_l_OP_NET_AVAIL, \
    [OP_NET_CLOSE] = &&ulvm_l_OP_NET_CLOSE, \
    [OP_NET_LASTERROR] = &&ulvm_l_OP_NET_LASTERROR, \
    [OP_NET_LOCALIP] = &&ulvm_l_OP_NET_LOCALIP, \
    [OP_NET_OPEN] = &&ulvm_l_OP_NET_OPEN, \
    [OP_NET_OPTION] = &&ulvm_l_OP_NET_OPTION, \
    [OP_NET_PEER] = &&ulvm_l_OP_NET_PEER, \
    [OP_NET_RECV] = &&ulvm_l_OP_NET_RECV, \
    [OP_NET_SEND] = &&ulvm_l_OP_NET_SEND, \
    [OP_NET_STATUS] = &&ulvm_l_OP_NET_STATUS, \
    [OP_NET_HTTP_REQUEST] = &&ulvm_l_OP_NET_HTTP_REQUEST, \
    [OP_NET_HTTP_STATUS] = &&ulvm_l_OP_NET_HTTP_STATUS, \
    [OP_NET_DISCOVERY] = &&ulvm_l_OP_NET_DISCOVERY, \
    [OP_NET_DISCOVERY_COUNT] = &&ulvm_l_OP_NET_DISCOVERY_COUNT, \
    [OP_NET_DISCOVERY_GET] = &&ulvm_l_OP_NET_DISCOVERY_GET, \
    [OP_NET_LOCALPORT] = &&ulvm_l_OP_NET_LOCALPORT, \
    [OP_OUTFMT] = &&ulvm_l_OP_OUTFMT, \
    [OP_SETREG64] = &&ulvm_l_OP_SETREG64, \
    [OP_LOADREG64] = &&ulvm_l_OP_LOADREG64, \
    [OP_STOREREG64] = &&ulvm_l_OP_STOREREG64, \
    [OP_LOADREGI64] = &&ulvm_l_OP_LOADREGI64, \
    [OP_STOREREGI64] = &&ulvm_l_OP_STOREREGI64, \
    [OP_ALU64] = &&ulvm_l_OP_ALU64, \
    [OP_OUTREG64] = &&ulvm_l_OP_OUTREG64, \
    [OP_OUTREGSIGNED64] = &&ulvm_l_OP_OUTREGSIGNED64, \
    [OP_NUMTOSTR64] = &&ulvm_l_OP_NUMTOSTR64, \
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
        if (!vm->running) goto ulvm_out; \
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
    if (vm->net && vm->net->ops && vm->net->ops->service)
        vm->net->ops->service(vm->net->ops_context, vm->net);
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

    ULVM_CASE(OP_FILL_SCREEN)
    {
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        ulvm_fillrect(vm, 0, 0, ulvm_gw(vm), ulvm_gh(vm), (uint16_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_FILL_CIRCLE)
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rr = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        ulvm_fillcircle(vm, (int32_t)ULVM_R(rx), (int32_t)ULVM_R(ry),
                        (int32_t)ULVM_R(rr), (uint16_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_DRAW_RECT)
    {
        uint8_t rx = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rw = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rh = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        ulvm_drawrect(vm, (int32_t)ULVM_R(rx), (int32_t)ULVM_R(ry),
                      (int32_t)ULVM_R(rw), (int32_t)ULVM_R(rh), (uint16_t)ULVM_R(rc));
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
        else if (flag == ULVM_FLAG_NETWORK)
            supported = vm->net != NULL && val == 1;
        else
            supported = vm->isflagsupported ? vm->isflagsupported(flag, val) : 0;
        ULVM_R(r) = supported ? 1u : 0u;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_OPEN)
    {
        uint8_t kind_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t local_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t remote_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t host_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t flags_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();

        if (!vm->net)
        {
            ULVM_R(result_reg) = UINT32_MAX;
            ULVM_NEXT();
        }

        char host_name[ULVM_NET_MAX_HOST];
        host_name[0] = '\0';
        uint32_t host_addr = ULVM_R(host_reg);
        if (host_addr != 0 && (!ULVM_INBOUNDS(host_addr, 1) || !ulvm_mem_read_string(vm, host_addr, host_name, sizeof(host_name))))
        {
            vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
            ULVM_R(result_reg) = UINT32_MAX;
            ULVM_NEXT();
        }

        int handle = ULVM_net_open(vm->net, (uint8_t)ULVM_R(kind_reg), (uint16_t)ULVM_R(local_reg),
                                   host_name[0] ? host_name : NULL, (uint16_t)ULVM_R(remote_reg),
                                   ULVM_R(flags_reg));
        ULVM_R(result_reg) = (uint32_t)handle;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_STATUS)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? (uint32_t)ULVM_net_status(vm->net, (int)ULVM_R(handle_reg)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_SEND)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t addr_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t len_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();

        uint32_t addr = ULVM_R(addr_reg);
        uint32_t len = ULVM_R(len_reg);
        if (!vm->net || !ULVM_INBOUNDS(addr, len))
        {
            if (vm->net) vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
            ULVM_R(result_reg) = UINT32_MAX;
            ULVM_NEXT();
        }
        int sent = ULVM_net_send(vm->net, (int)ULVM_R(handle_reg), &vm->mem[addr], len);
        ULVM_R(result_reg) = (uint32_t)sent;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_RECV)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t addr_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t cap_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();

        uint32_t addr = ULVM_R(addr_reg);
        uint32_t cap = ULVM_R(cap_reg);
        if (!vm->net || !ULVM_INBOUNDS(addr, cap))
        {
            if (vm->net) vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
            ULVM_R(result_reg) = UINT32_MAX;
            ULVM_NEXT();
        }
        int got = ULVM_net_recv(vm->net, (int)ULVM_R(handle_reg), &vm->mem[addr], cap);
        ULVM_R(result_reg) = (uint32_t)got;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_ACCEPT)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? (uint32_t)ULVM_net_accept(vm->net, (int)ULVM_R(handle_reg)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_CLOSE)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? (uint32_t)ULVM_net_close(vm->net, (int)ULVM_R(handle_reg)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_OPTION)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t opt_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t value_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? (uint32_t)ULVM_net_option(vm->net, (int)ULVM_R(handle_reg), (uint8_t)ULVM_R(opt_reg), ULVM_R(value_reg)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_AVAIL)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? ULVM_net_avail(vm->net, (int)ULVM_R(handle_reg)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_LOCALIP)
    {
        uint8_t addr_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_R(addr_reg);
        const char *ip = vm->net ? ULVM_net_localip(vm->net) : "0.0.0.0";
        int ok = vm->net && ULVM_INBOUNDS(addr, (uint32_t)strlen(ip) + 1u);
        if (ok) ulvm_mem_write_string(vm, addr, ip, (uint32_t)strlen(ip) + 1u);
        else if (vm->net) vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
        ULVM_R(result_reg) = ok ? 1u : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_PEER)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t host_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t port_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();

        char hostbuf[ULVM_NET_MAX_HOST];
        uint16_t port = 0;
        int peer_found = vm->net ? ULVM_net_peer(vm->net, (int)ULVM_R(handle_reg), hostbuf, sizeof(hostbuf), &port) : -1;
        int ok = peer_found > 0;
        if (ok && !ULVM_INBOUNDS(ULVM_R(host_reg), (uint32_t)strlen(hostbuf) + 1u)) ok = 0;
        if (ok && ULVM_INBOUNDS(ULVM_R(port_reg), 2))
            ulvm_store_memory(vm, ULVM_R(port_reg), 2, port);
        else if (ok) ok = 0;
        if (ok) ulvm_mem_write_string(vm, ULVM_R(host_reg), hostbuf, sizeof(hostbuf));
        else if (vm->net) vm->net->last_error = peer_found > 0 ? ULVM_NET_ERR_NO_BUFFER : ULVM_NET_ERR_INVALID_HANDLE;
        ULVM_R(result_reg) = ok ? 1u : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_LASTERROR)
    {
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? ULVM_net_lasterror(vm->net) : ULVM_NET_ERR_UNSUPPORTED;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_HTTP_REQUEST)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t method_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t path_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t headers_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t body_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t length_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        uint32_t path_addr = ULVM_R(path_reg), headers_addr = ULVM_R(headers_reg);
        uint32_t body_addr = ULVM_R(body_reg), body_len = ULVM_R(length_reg);
        char path[256], headers[ULVM_NET_MAX_HTTP_HEADERS];
        if (!vm->net || !ulvm_mem_read_string(vm, path_addr, path, sizeof(path)) ||
            !ulvm_mem_read_string(vm, headers_addr, headers, sizeof(headers)) ||
            !ULVM_INBOUNDS(body_addr, body_len) || body_len > ULVM_NET_MAX_TRANSFER)
        {
            if (vm->net) vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
            ULVM_R(result_reg) = UINT32_MAX;
            ULVM_NEXT();
        }
        int rc = ULVM_net_http_request(vm->net, (int)ULVM_R(handle_reg),
                                       (uint8_t)ULVM_R(method_reg), path, headers,
                                       body_len ? &vm->mem[body_addr] : NULL, body_len);
        ULVM_R(result_reg) = (uint32_t)rc;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_HTTP_STATUS)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? ULVM_net_http_status(vm->net, (int)ULVM_R(handle_reg)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_DISCOVERY)
    {
        uint8_t flags_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t port_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t game_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t name_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t flags = (uint8_t)ULVM_R(flags_reg);
        char name[32] = "";
        uint32_t name_addr = ULVM_R(name_reg);
        if ((flags & ULVM_NET_DISCOVERY_ANNOUNCE) && !ulvm_mem_read_string(vm, name_addr, name, sizeof(name)))
        {
            if (vm->net) vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
            ULVM_R(result_reg) = UINT32_MAX;
            ULVM_NEXT();
        }
        int rc = vm->net ? ULVM_net_discovery(vm->net, flags,
                        (uint16_t)ULVM_R(port_reg), ULVM_R(game_reg), name) : -1;
        ULVM_R(result_reg) = (uint32_t)rc;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_DISCOVERY_COUNT)
    {
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net ? ULVM_net_discovery_count(vm->net) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_DISCOVERY_GET)
    {
        uint8_t index_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t name_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t host_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t port_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t game_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVMNetSession session;
        int get_rc = vm->net ? ULVM_net_discovery_get(vm->net, ULVM_R(index_reg), &session) : -1;
        int ok = get_rc > 0;
        int memory_error = 0;
        if (ok && (!ULVM_INBOUNDS(ULVM_R(name_reg), (uint32_t)strlen(session.name) + 1u) ||
                   !ULVM_INBOUNDS(ULVM_R(host_reg), (uint32_t)strlen(session.host) + 1u) ||
                   !ULVM_INBOUNDS(ULVM_R(port_reg), 2) || !ULVM_INBOUNDS(ULVM_R(game_reg), 4)))
        { ok = 0; memory_error = 1; }
        if (ok)
        {
            ulvm_mem_write_string(vm, ULVM_R(name_reg), session.name, sizeof(session.name));
            ulvm_mem_write_string(vm, ULVM_R(host_reg), session.host, sizeof(session.host));
            ulvm_store_memory(vm, ULVM_R(port_reg), 2, session.port);
            ulvm_store_memory(vm, ULVM_R(game_reg), 4, session.game_id);
        }
        else if (vm->net && memory_error) vm->net->last_error = ULVM_NET_ERR_NO_BUFFER;
        else if (vm->net && get_rc == 0) vm->net->last_error = ULVM_NET_ERR_INVALID_HANDLE;
        ULVM_R(result_reg) = ok ? 1u : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NET_LOCALPORT)
    {
        uint8_t handle_reg = (uint8_t)ULVM_NEXTBYTE();
        uint8_t result_reg = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(result_reg) = vm->net
            ? ULVM_net_localport(vm->net, (int)ULVM_R(handle_reg)) : UINT32_MAX;
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

    ULVM_CASE(OP_JMPREG)
    ULVM_CASE(OP_CALLREG)
    {
        uint8_t rtarget = (uint8_t)ULVM_NEXTBYTE();
        uint32_t target = ULVM_R(rtarget);
        if ((rom && target >= rom_size) || (!rom && target == UINT32_MAX))
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            ULVM_NEXT();
        }
        if (opbyte == OP_CALLREG)
        {
            if (vm->callsp >= ULVM_CALLSTACK_SIZE)
            {
                vm->faulted = 1;
                vm->fault_pc = instruction_pc;
                vm->fault_opcode = opbyte;
                vm->running = 0;
                ULVM_NEXT();
            }
            vm->callstack[vm->callsp++] = pc;
        }
        pc = target;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_PUSHR)
    {
        uint8_t src = (uint8_t)ULVM_NEXTBYTE();
        if (vm->valuesp >= ULVM_VALUESTACK_SIZE)
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            ULVM_NEXT();
        }
        vm->valuestack[vm->valuesp++] = ULVM_R(src);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_POPR)
    {
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        if (vm->valuesp == 0)
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            ULVM_NEXT();
        }
        ULVM_R(dst) = vm->valuestack[--vm->valuesp];
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SUBROUTINE)
    {
        uint32_t pos = ULVM_NEXT4BYTES();
        if (vm->callsp >= ULVM_CALLSTACK_SIZE)
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            ULVM_NEXT();
        }
        vm->callstack[vm->callsp++] = pc;
        pc = pos;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_RETURN)
        if (vm->callsp == 0)
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            ULVM_NEXT();
        }
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
        int32_t retx, rety;
        ulvm_drawstring_mem(vm, (int32_t)ULVM_R(regx), (int32_t)ULVM_R(regy), (uint16_t)ULVM_R(regcolor),
                            ULVM_R(regaddr), ULVM_R(reglen), src, &retx, &rety);
        ULVM_R(regreturnx) = (uint32_t)retx;
        ULVM_R(regreturny) = (uint32_t)rety;
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

    ULVM_CASE(OP_OUTFMT)
    {
        uint32_t addr = ULVM_NEXT4BYTES();
        uint8_t format = (uint8_t)ULVM_NEXTBYTE();
        uint8_t conversion = (uint8_t)ULVM_NEXTBYTE();
        uint8_t width = (uint8_t)ULVM_NEXTBYTE();
        uint8_t precision = (uint8_t)ULVM_NEXTBYTE();
        uint8_t flags = (uint8_t)ULVM_NEXTBYTE();
        if (!ulvm_print_formatted(vm, addr, format, conversion, width, precision, flags))
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            break;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_FLOATOP)
    {
        uint8_t operation = (uint8_t)ULVM_NEXTBYTE();
        uint8_t format = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rleft = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rright = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdest = (uint8_t)ULVM_NEXTBYTE();
        if ((format != 7 && format != 8) || operation < 1 || operation > 15
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
        if (operation >= 6 && operation <= 11)
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
            case 12: result = sinf(a); break;
            case 13: result = cosf(a); break;
            case 14: result = sqrtf(a); break;
            case 15: result = atan2f(a, b); break;
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
            case 12: result = sin(left); break;
            case 13: result = cos(left); break;
            case 14: result = sqrt(left); break;
            case 15: result = atan2(left, right); break;
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

    ULVM_CASE(OP_SETREG64)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint64_t value = ULVM_NEXT8BYTES();
        ULVM_R(r) = value;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_LOADREG64)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        ULVM_R(r) = ulvm_load_memory64(vm, addr);
        ULVM_NEXT();
    }
    ULVM_CASE(OP_STOREREG64)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_NEXT4BYTES();
        ulvm_store_memory64(vm, addr, ULVM_R(r));
        ULVM_NEXT();
    }
    ULVM_CASE(OP_LOADREGI64)
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE(), rd = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(rd) = ulvm_load_memory64(vm, ULVM_R32(ra));
        ULVM_NEXT();
    }
    ULVM_CASE(OP_STOREREGI64)
    {
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE(), rv = (uint8_t)ULVM_NEXTBYTE();
        ulvm_store_memory64(vm, ULVM_R32(ra), ULVM_R(rv));
        ULVM_NEXT();
    }
    ULVM_CASE(OP_ALU64)
    {
        uint8_t op = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE(), rb = (uint8_t)ULVM_NEXTBYTE(), rd = (uint8_t)ULVM_NEXTBYTE();
        uint64_t a = ULVM_R(ra), b = ULVM_R(rb), v = 0;
        int write = 1;
        switch (op) {
        case 1: v = a + b; break; case 2: v = a - b; break; case 3: v = a * b; break;
        case 4: if (!b) write = 0; else v = a / b; break;
        case 5: if (!b) write = 0; else v = a % b; break;
        case 6: if (!b) write = 0; else v = (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX) ? a : (uint64_t)((int64_t)a / (int64_t)b); break;
        case 7: if (!b) write = 0; else v = (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX) ? 0 : (uint64_t)((int64_t)a % (int64_t)b); break;
        case 8: v = a & b; break; case 9: v = a | b; break; case 10: v = a ^ b; break;
        case 11: v = ~a; break; case 12: v = 0u - a; break;
        case 13: v = b < 64 ? a << b : 0; break; case 14: v = b < 64 ? a >> b : 0; break;
        case 15: v = b >= 64 ? ((a >> 63) ? UINT64_MAX : 0) : b == 0 ? a : ((a >> b) | ((a >> 63) ? (UINT64_MAX << (64 - b)) : 0)); break;
        case 16: v = a == b; break; case 17: v = a != b; break; case 18: v = a < b; break;
        case 19: v = a > b; break; case 20: v = a <= b; break; case 21: v = a >= b; break;
        case 22: v = (int64_t)a == (int64_t)b; break; case 23: v = (int64_t)a != (int64_t)b; break;
        case 24: v = (int64_t)a < (int64_t)b; break; case 25: v = (int64_t)a > (int64_t)b; break;
        case 26: v = (int64_t)a <= (int64_t)b; break; case 27: v = (int64_t)a >= (int64_t)b; break;
        case 28: v = (uint64_t)(int64_t)(int8_t)a; break; case 29: v = (uint64_t)(int64_t)(int16_t)a; break;
        case 30: v = (uint64_t)(int64_t)(int32_t)a; break; case 31: v = (uint32_t)a; break;
        default: write = 0; break;
        }
        if (write) ULVM_R(rd) = v;
        ULVM_NEXT();
    }
    ULVM_CASE(OP_OUTREG64)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE(); ulvm_print_uint64(vm, ULVM_R(r), 0); ULVM_NEXT();
    }
    ULVM_CASE(OP_OUTREGSIGNED64)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE(); ulvm_print_uint64(vm, ULVM_R(r), 1); ULVM_NEXT();
    }
    ULVM_CASE(OP_NUMTOSTR64)
    {
        uint8_t r = (uint8_t)ULVM_NEXTBYTE(); uint32_t addr = ULVM_NEXT4BYTES(); uint8_t rlen = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(rlen) = ulvm_itoa64(vm, ULVM_R(r), addr); ULVM_NEXT();
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
        ulvm_fillrect(vm, (int32_t)ULVM_R(rx), (int32_t)ULVM_R(ry), (int32_t)ULVM_R(rw), (int32_t)ULVM_R(rh), (uint16_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SETPALETTE)
    {
        uint8_t ri = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        if (vm->setpalette)
            vm->setpalette((uint8_t)ULVM_R(ri), (uint16_t)ULVM_R(rc));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_SAVE)
    ULVM_CASE(OP_LOAD)
    {
        uint8_t raddr = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rlen = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rresult = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_R(raddr), len = ULVM_R(rlen);
        vm_storage storage = opbyte == OP_SAVE ? vm->save : vm->load;
        int ok = storage && ULVM_INBOUNDS(addr, len)
            ? storage(&vm->mem[addr], len) : 0;
        ULVM_R(rresult) = ok ? 1u : 0u;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_MEMCPY)
    {
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rsrc = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rlen = (uint8_t)ULVM_NEXTBYTE();
        uint8_t source = (uint8_t)ULVM_NEXTBYTE();
        uint32_t dst = ULVM_R(rdst), src = ULVM_R(rsrc), len = ULVM_R(rlen);
        if (!ULVM_INBOUNDS(dst, len) || source > 1
            || (!source && !ULVM_INBOUNDS(src, len))
            || (source && vm->rom && ((uint64_t)src + len > vm->rom_size)))
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            break;
        }
        else if (source)
            for (uint32_t i = 0; i < len; ++i)
                vm->mem[dst + i] = ulvm_rom_byte(vm, src + i);
        else
            memmove(&vm->mem[dst], &vm->mem[src], len);
        ULVM_NEXT();
    }

    ULVM_CASE(OP_MEMSET)
    {
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rval = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rlen = (uint8_t)ULVM_NEXTBYTE();
        uint32_t dst = ULVM_R(rdst), len = ULVM_R(rlen);
        if (ULVM_INBOUNDS(dst, len))
            memset(&vm->mem[dst], (uint8_t)ULVM_R(rval), len);
        else
        {
            vm->faulted = 1;
            vm->fault_pc = instruction_pc;
            vm->fault_opcode = opbyte;
            vm->running = 0;
            break;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_MINREG)
    ULVM_CASE(OP_MAXREG)
    {
        uint8_t rleft = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rright = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        uint8_t is_signed = (uint8_t)ULVM_NEXTBYTE();
        uint32_t left = ULVM_R(rleft), right = ULVM_R(rright);
        int take_left = is_signed
            ? (opbyte == OP_MINREG ? (int32_t)left <= (int32_t)right
                                   : (int32_t)left >= (int32_t)right)
            : (opbyte == OP_MINREG ? left <= right : left >= right);
        ULVM_R(rdst) = take_left ? left : right;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_ABSREG)
    {
        uint8_t rsrc = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rdst = (uint8_t)ULVM_NEXTBYTE();
        uint32_t value = ULVM_R(rsrc);
        ULVM_R(rdst) = (value & 0x80000000u) ? 0u - value : value;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_MUL64)
    {
        uint8_t rleft = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rright = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rhigh = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rlow = (uint8_t)ULVM_NEXTBYTE();
        uint8_t is_signed = (uint8_t)ULVM_NEXTBYTE();
        uint64_t product = is_signed
            ? (uint64_t)((int64_t)(int32_t)ULVM_R(rleft) * (int64_t)(int32_t)ULVM_R(rright))
            : (uint64_t)ULVM_R(rleft) * ULVM_R(rright);
        ULVM_R(rhigh) = (uint32_t)(product >> 32);
        ULVM_R(rlow) = (uint32_t)product;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_BEEP)
    {
        uint8_t rfreq = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rms = (uint8_t)ULVM_NEXTBYTE();
        uint8_t mode = ulvm_getflag(vm, 2);
        if ((mode == 1 || mode == 2) && vm->beep)
            vm->beep(ULVM_R(rfreq), ULVM_R(rms));
        ULVM_NEXT();
    }

    ULVM_CASE(OP_AUDIO_COMMAND)
    {
        uint8_t command = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ra = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rb = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rresult = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(rresult) = ulvm_getflag(vm, 2) == 2 && vm->audio_command
            ? vm->audio_command(command, ULVM_R(ra), ULVM_R(rb)) : UINT32_MAX;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_AUDIO_LOAD)
    {
        uint8_t rkind = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rindex = (uint8_t)ULVM_NEXTBYTE();
        uint8_t raddr = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rlen = (uint8_t)ULVM_NEXTBYTE();
        uint8_t source = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rresult = (uint8_t)ULVM_NEXTBYTE();
        uint32_t addr = ULVM_R(raddr), len = ULVM_R(rlen);
        uint8_t data[ULVM_AUDIO_LOAD_MAX];
        uint32_t item_size = 0, item_count = 0;
        int address_ok = len == 0 || addr <= UINT32_MAX - (len - 1u);
        int rom_ok = !vm->rom || source == 0 ||
            (addr <= vm->rom_size && len <= vm->rom_size - addr);
        if (ULVM_R32(rkind) == ULVM_AUDIO_DATA_SFX)
            item_size = ULVM_AUDIO_SFX_BYTES;
        else if (ULVM_R32(rkind) == ULVM_AUDIO_DATA_MUSIC)
            item_size = ULVM_AUDIO_MUSIC_BYTES;
        if (item_size && len && len % item_size == 0)
            item_count = len / item_size;
        int ok = ulvm_getflag(vm, 2) == 2 && vm->audio_load &&
            ULVM_R32(rkind) <= UINT8_MAX && ULVM_R32(rindex) <= UINT8_MAX &&
            ULVM_R32(rindex) < 64u && item_count && item_count <= 64u - ULVM_R32(rindex) &&
            item_size <= sizeof(data) && source <= 1 && address_ok && rom_ok &&
            (source == 1 || ULVM_INBOUNDS(addr, len));
        if (ok)
        {
            for (uint32_t item = 0; item < item_count; item++)
            {
                uint32_t offset = item * item_size;
                for (uint32_t i = 0; i < item_size; i++)
                    data[i] = source ? ulvm_rom_byte(vm, addr + offset + i)
                                     : vm->mem[addr + offset + i];
                if (!vm->audio_load((uint8_t)ULVM_R(rkind),
                                    (uint8_t)(ULVM_R(rindex) + item), data, item_size))
                {
                    ok = 0;
                    break;
                }
            }
        }
        ULVM_R(rresult) = ok ? 1u : 0u;
        ULVM_NEXT();
    }

    ULVM_CASE(OP_DRAWLINE)
    {
        uint8_t rx0 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry0 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rx1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t ry1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t rc = (uint8_t)ULVM_NEXTBYTE();
        ulvm_drawline(vm, (int32_t)ULVM_R(rx0), (int32_t)ULVM_R(ry0), (int32_t)ULVM_R(rx1), (int32_t)ULVM_R(ry1), (uint16_t)ULVM_R(rc));
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
        int take = (opbyte == OP_IFREG || opbyte == OP_IFSUBREG) ? (ULVM_R32(r) != 0) : (ULVM_R32(r) == 0);
        if (take)
        {
            if (opbyte == OP_IFSUBREG || opbyte == OP_IFNOTSUBREG)
            {
                if (vm->callsp >= ULVM_CALLSTACK_SIZE)
                {
                    vm->faulted = 1;
                    vm->fault_pc = instruction_pc;
                    vm->fault_opcode = opbyte;
                    vm->running = 0;
                    ULVM_NEXT();
                }
                vm->callstack[vm->callsp++] = pc;
            }
            pc = pos;
        }
        ULVM_NEXT();
    }

    ULVM_CASE(OP_NOTREG)
    {
        uint8_t r1 = (uint8_t)ULVM_NEXTBYTE();
        uint8_t dst = (uint8_t)ULVM_NEXTBYTE();
        ULVM_R(dst) = (uint32_t)~ULVM_R32(r1);
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
        vm->exit_code = ULVM_R(0);
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
