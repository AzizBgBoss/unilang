# unilang

A custom low-level bytecode ISA (`uniClang`) targeting NDS/GBA-class and
Arduino-class hardware, a Python reference VM, a small C-like compiler that
emits bytecode for it, and a portable C header implementing the same ISA for
embedded targets.

- `main.py` — the Python reference VM (interprets compiled `.ulc` files)
- `compiler.py` — the uniClang compiler (reads C source, `.c`, and emits `.ulc`)
- `preprocessor.py` — the C preprocessor used by the compiler (`#include`, `#define`, `#if`...)
- `C-interpreter/unilang.h` — a header-only C implementation of the VM, for
  Arduino/embedded targets; graphics, keyboard, timing, and I/O are all
  optional host callbacks (see Section 3)
- `C-interpreter/main.c` — an SDL2 example host wiring `unilang.h` up to a
  desktop window
- `examples/` — `.c` sources and compiled `.ulc` bytecode (`game/`, `pong/`,
  `test/`, `microcity/`, `flag6/`)

---

## 1. The unilang machine model

- **Byte-addressable memory.** Every address is a byte offset. Loads/stores
  come in fixed widths (1, 2, or 4 bytes) via dedicated opcodes
  (`loadreg8/16/32`, `storereg8/16/32`, etc.) rather than a generic
  variable-width `mem` operand. Out-of-bounds addresses raise a hard error
  (Python VM) or are bounds-checked and ignored (C VM) — they do not wrap.
- **16 general-purpose 32-bit registers** (`r0`-`r15`) do integer arithmetic,
  logic, and comparison. Floating-point opcodes operate on typed values in
  memory. Memory is byte-addressed, with integer load/store widths of 8/16/32
  bits and 64-bit double values handled by the float/copy opcodes.
- **Memory size** is set via `memsize` in `main.py` (Python VM) or the
  `ULVM_MEM_SIZE` macro, defined before `#include "unilang.h"` (C VM).
- **Flags** (separate from `mem`) configure hardware capabilities, packed as
  4-bit fields:
  - **Flag 0 — graphics**: `0` disabled, `1` mono 64x64, `2` 4-bit grayscale
    64x64, `3` 8-bit color 64x64, `4` mono 128x64, `5` 4-bit grayscale
    128x64, `6` 8-bit color 128x64. The SDL host interprets grayscale values
    as 16 shades and 8-bit color as RGB332.
  - **Flag 1 — keyboard**: `0` disabled, `1` NES-style
    (Up/Down/Left/Right/A/B/Select/Start, 8 bits). Only meaningful once
    graphics are enabled. Bit layout: Up=`1`, Down=`2`, Left=`4`, Right=`8`,
    C=`16`, V=`32`, Backspace=`64`, Enter=`128`.
- **No VM-owned framebuffer in the C interpreter.** `setpixel`/`getpixel`
  forward straight to host callbacks — the host owns however (or whether) it
  stores pixels. The Python VM still keeps its own framebuffer + pygame
  window internally.
- **Call stack**: the Python VM uses a single global return-address
  register (`subroutine`/`return` cannot nest); the C VM (`unilang.h`) has a
  real bounded call stack (`ULVM_CALLSTACK_SIZE`, default 16), so nested
  subroutine calls work there. The compiler avoids relying on either — see
  Section 2.
- **Screen redraw is explicit.** The VM only flips/clears the display when
  the bytecode executes `refreshscreen`; draw a complete frame with
  `setpixel` calls, then call `refreshscreen` once per frame.

### Opcode reference

All multi-byte operands are big-endian. `addr`/`val`/`pos` are 4 bytes,
`reg`/`flag`/`byte` are 1 byte, `text` is a 2-byte length prefix followed by
raw bytes.

| Byte | Name | Operands | Description |
|---|---|---|---|
| 0x00 | `nop` | — | do nothing |
| 0x01 | `flag` | flag, val | set a flag (see above) |
| 0x02 | `isflagsupported` | flag, val, reg | check if a flag value is supported; result → reg |
| 0x03 | `setbyte` | addr, byte | set a single memory byte |
| 0x04 | `write` | addr, text | write raw bytes into memory starting at addr |
| 0x05 | `outc` | addr | print the byte at addr as a character |
| 0x06 | `out` | addr | print the byte at addr as a number |
| 0x07 | `input` | chars, addr | read chars into memory starting at addr |
| 0x08 | `print` | text | print text + newline |
| 0x09 | `printn` | text | print text, no newline |
| 0x0A | `printv` | chars, addr | print chars bytes from memory + newline |
| 0x0B | `printvn` | chars, addr | same, no newline |
| 0x0C | `cur` | — | print current bytecode position (debug) |
| 0x0D | `memory` | — | dump memory (debug) |
| 0x0E | `flags` | — | dump flags (debug) |
| 0x0F | `setpc` | pos | unconditional jump |
| 0x10 | `subroutine` | pos | call |
| 0x11 | `return` | — | return from a subroutine |
| 0x12 | `sleep` | ms | sleep (literal ms, not a memory address) |
| 0x13 | `refreshscreen` | — | flip the display, then clear the framebuffer |
| 0x14-0x16 | `romread8/16/32` | reg_addr, reg_dst | read 1, 2, or 4 bytes from program bytecode/ROM |
| 0x17 | `setpixel` | regx, regy, regc | plot a pixel using x/y/color from registers |
| 0x18 | `getpixel` | regx, regy, regdst | read a pixel's value into a register |
| 0x19 | `getkeyboard` | reg | keyboard state → reg |
| 0x1A | `gettime` | reg | current time (ms) → reg |
| 0x1B | `getmemsize` | reg | total memory size (bytes) → reg |
| 0x1C | `getflag` | flag, reg | flag's current value → reg |
| 0x1D | `randreg` | min, max, reg | random value in `[min, max)` → reg |
| 0x20 | `numtostr` | reg, addr, reglen | convert a register value to decimal text |
| 0x21 | `floatop` | op, format, rleft, rright, rdest | binary32/binary64 arithmetic and comparisons |
| 0x22 | `floatcast` | src_format, dst_format, rsrc, rdst | typed numeric conversion or 64-bit copy |
| 0x23 | `romread64` | reg_addr, reg_hi, reg_lo | read 8 bytes from program bytecode/ROM |
| 0x40 | `setreg` | reg, val | reg = val |
| 0x41 | `movreg` | dst, src | dst = src |
| 0x42-0x44 | `loadreg8/16/32` | reg, addr | reg = mem at addr (zero-extended) |
| 0x45-0x47 | `storereg8/16/32` | reg, addr | mem at addr = reg (truncated) |
| 0x48-0x4C | `addreg`/`subreg`/`mulreg`/`divreg`/`modreg` | r1, r2, dst | dst = r1 OP r2 (div/mod no-op if r2 == 0) |
| 0x4D-0x4F | `addregv`/`subregv`/`mulregv` | r1, val, dst | dst = r1 OP val |
| 0x50 | `compreg` | r1, r2, dst | dst = 0 if equal, 1 if r1 > r2, 2 if r1 < r2 |
| 0x51-0x54 | `ifreg`/`ifnotreg`/`ifsubreg`/`ifnotsubreg` | reg, pos | conditional jump / call |
| 0x55 | `notreg` | r1, dst | dst = ~r1 |
| 0x56-0x5D | `orreg`/`andreg`/`norreg`/`nandreg`/`xorreg`/`xnorreg`/`shlreg`/`shrreg` | r1, r2, dst | bitwise ops |
| 0x60-0x62 | `loadregi8/16/32` | reg_addr, reg_dst | reg_dst = mem at address held in reg[reg_addr] |
| 0x63-0x65 | `storeregi8/16/32` | reg_addr, reg_val | mem at address reg[reg_addr] = reg[reg_val] |
| 0x66-0x6B | `eqreg`/`neqreg`/`ltreg`/`gtreg`/`lereg`/`gereg` | r1, r2, dst | dst = 1 if comparison holds, else 0 |
| 0x6C | `outreg` | reg | print a register's full numeric value |
| 0x6D-0x6F | `sdivreg`/`smodreg`/`sshrreg` | r1, r2, dst | signed division/remainder and arithmetic right shift |
| 0x70 | `outregsigned` | reg | print signed 32-bit register value |
| 0xFF | `exit` | — | halt |

`floatop` formats are `7` for IEEE-754 binary32 and `8` for binary64. Its
operation selectors are `1` add, `2` subtract, `3` multiply, `4` divide,
`5` negate, and `6` through `11` for `==`, `!=`, `<`, `>`, `<=`, and `>=`.
`floatcast` format selectors are `1` signed 8-bit, `2` unsigned 8-bit,
`3` signed 16-bit, `4` unsigned 16-bit, `5` signed 32-bit, `6` unsigned
32-bit, `7` float, and `8` double.

Run bytecode with the Python VM: `python main.py -f program.ulc`

---

## 2. The C-to-ULC compiler (`compiler.py`)

```
pip install pycparser
python compiler.py input.c -o output.ulc [-I dir] [-D NAME=value]
python compiler.py examples/microcity -o examples/microcity/microcity.ulc
python main.py -f output.ulc
```

Directory and multi-file inputs are supported as well, so the MicroCity
project can be compiled directly from its source folder.

It parses a **deliberately small subset of C-like syntax** with `pycparser`
and emits `.ulc` bytecode directly (no separate assembler step). Codegen
targets the register ISA above: every arithmetic/logic/comparison op
compiles to load-into-register → op → store-back, using two scratch
registers (r14/r15) that are never assumed to persist across statements.
Floating-point opcodes use address registers r13-r15; registers are not kept
live across statements.

### Types and memory sizing

- Integer values support signed and unsigned 8-, 16-, and 32-bit widths.
  `int`, `long`, and `uint32_t` are 32-bit; `short`/`int16_t` are 16-bit;
  `char`/`int8_t`/`uint8_t` are 8-bit. Narrow integer expressions are
  promoted to 32 bits for arithmetic. Signed division, remainder, comparisons,
  arithmetic right shift, casts, and `%d`/`%i` output use signed semantics.
- `float` is IEEE-754 binary32 (4 bytes); `double` is IEEE-754 binary64
  (8 bytes). Arithmetic, comparisons, assignments, casts, arrays, function
  arguments/returns, and increment/decrement are supported. The C VM host must
  provide IEEE binary32 and binary64; unsupported host formats fault rather
  than silently misinterpret values. Float-to-integer conversions outside the
  representable range produce zero; such conversions are undefined in C.
- Legacy unsigned types `uint1`, `uint2`, and `uint4` remain accepted. `bool`,
  `char`, and 8-bit integers occupy a full byte; 16-bit integers occupy 2 bytes,
  32-bit integers and `float` occupy 4 bytes, and `double` occupies 8.
  `true`/`false` are `1`/`0`.
- The compiler doesn't hardcode the VM's memory size. Every compiled program
  starts with a prologue that calls `getmemsize` at runtime and compares it
  against how many bytes the program's own variables need (computed at
  compile time), printing a warning if there isn't enough room:
  ```
  WARNING: this program needs at least 5344 bytes of memory, but only 4096
  are available. Increase 'memsize' in main.py.
  ```
  The program still runs after the warning — it just may misbehave, since
  variables can end up sharing an address. Graphics/keyboard have their own
  dedicated buffers on both VMs, so only the program's own variables count
  toward this check.
- Declarations may have an initializer: `uint8_t x;` or `uint16_t x = 5;`
- `const` arrays with literal initializers are stored in ROM, not RAM:
  `const uint8_t rows[6] = { 6, 9, 15, 9, 9, 9 };`
- Const strings are null-terminated ROM arrays:
  `const char msg[] = "Hello!";`
- Character constants such as `'a'`, `'0'`, `'\n'` work as 8-bit values.

### Supported

- `uint32_t main() { ... }` — the entry point; its body is compiled last,
  followed by an implicit `exit`.
- Assignment: `x = expr;`
- Arithmetic: `+ - * / %` (binary), `x++` / `x--`
- Bitwise: `& | ^ << >>`
- Logical: `&& ||`
- Comparisons: `== != < > <= >=` — usable directly in `if`/`while`
  conditions, and also as expressions (`uint8_t b = a < c;` works).
- Control flow: `if (...) { } else { }`, `while (...) { }`, `for (...) { }`
- Functions: `uint32_t add(uint32_t a, uint32_t b) { return a + b; }` — see
  caveats below.
- `printf("literal text\n");` and `printf("...%d...\n", expr);` — `%d`, `%i`,
  `%u`, `%c`, `%s`, and `%%` are supported without width/precision modifiers.
  `%d`/`%i` print signed values; floating-point values can be cast to an
  integer for output (direct `%f` formatting is not implemented).
- `exit();` — halts immediately (statement only, not an expression).
- Comments: `// line` and `/* block */`, stripped before parsing
  (string/char literals containing `//`/`/*` are left alone).
- **Preprocessor** (`preprocessor.py`): `#include "file"` (relative to the
  including file, then `-I` dirs), `#include <file>` (`-I` dirs; standard
  headers like `<stdint.h>`/`<stdbool.h>`/`<stdio.h>` are accepted and
  ignored since their contents are built in), `#define`/`#undef` (object-like
  and function-like, `#x`, `##`, `__VA_ARGS__`), `#if`/`#ifdef`/`#ifndef`/
  `#elif`/`#else`/`#endif` with `defined()` and full integer expressions,
  `#pragma once`, `#error`, `__LINE__`/`__FILE__`, and predefined `NULL`,
  `RAND_MAX`, `UINT8_MAX`... Macros aren't expanded inside strings. A
  function-like macro call must fit on one line.
- **C compatibility**: standard type names (`int`, `unsigned`, `short`,
  `long`, `uint8_t`..`uint32_t`, `size_t`, `_Bool`, `void`), `typedef`,
  `enum`, `const` scalars (compile-time constants), pointers (`T *p`, `&x`,
  `*p`, `p[i]`, scaled pointer arithmetic, arrays decaying to pointers,
  pointer parameters), casts, `sizeof`, `?:`, unary `- + ! ~`, short-circuit
  `&&`/`||`, compound assignment (`+= <<= ...`), `++`/`--` on any lvalue,
  the comma operator, `do`/`while`, `switch`/`case`/`default`, `break`,
  `continue`, function prototypes, `static` globals, early `return` from
  `main`, array initializers of any constant expression (`{0}` zero-fills),
  `char s[] = "text"`. Constant expressions are folded at compile time.
- `printf` supports `%d %i %u %c %s %%` (no width/precision); also `puts`,
  `putchar`, and C-style `rand()` (0..32767) besides `rand(min, max)`.
- **Compatibility notes**: the compiler supports a broader C-like subset used
  by the MicroCity project, including struct and
  bitfield access, multi-file directory inputs, AVR-style `pgmspace` shims,
  C++-style tag names, and constant ROM arrays. The compiler still keeps the
  VM's simplified memory model: pointers are byte-addressed, `const` ROM data is
  read-only, and functions are inlined instead of recursively called.
  `alloc N x;` and `MEMORY[a:b]` remain legacy non-C extensions.
- **Builtins** for hardware access:
  - `flag(index, value);` — raw `flag` opcode. Statement only.
  - `setpixel(x, y, val);` — plots a pixel via the native `setpixel` opcode.
    `x`/`y` may be any supported integer width; `val` is passed as an 8-bit
    pixel value, which carries the RGB332 color in 8-bit graphics mode.
    Statement only.
  - `drawchar(c, x, y[, color])` and
    `drawstring(text, x, y[, color])` — draw glyphs with the optional pixel
    color; the default color is `1` for existing programs.
    In flag 6, colors use the same RGB332 byte values as `setpixel`.
  - `getpixel(x, y)` — reads a pixel back. Returns a value.
  - `getkey()` — reads the 8-bit NES-style keyboard state. Assumes
    `flag(0, ...)` and `flag(1, 1)` were already set. Returns a value.
  - `isflagsupported(flag, val)` — both arguments must be compile-time
    constants. Returns a value.
  - `sleep(ms);` — `ms` must be a constant (the opcode takes a literal).
  - `refreshscreen();` — flips the display and clears the framebuffer for
    the next frame. Call once per frame, after your `setpixel` calls.
  - `input(&name);` — reads external input into a variable.
  - `romread(pos, size)` — expression form of `romread8/16/32/64`. `pos` is a
    ROM byte offset expression; `size` must be a constant `1`, `2`, `4`, or `8`.

### How functions work (and their limits)

Function calls are compiled by **inlining**: each call site gets its own
fresh copy of the callee's parameters, locals, and a return-value slot, with
`return` compiled as "write result, then jump to the end of this inlined
copy." This sidesteps the ISA's limited call-stack support entirely, so:

- Functions can call other functions, including from inside other
  functions — not just from `main()`.
- **Recursion (direct or mutual) is not supported** and is rejected at
  compile time with a clear error, since inlining a recursive call would
  mean infinite code.
- Every call costs bytecode size and memory (a fresh copy of the callee's
  locals), not runtime overhead — heavily-called functions bloat the output
  and eat into the memory budget (see "Memory sizing" above).
- Function parameters shadow same-named variables only for the duration of
  that inlined call.
- **Blocks are properly scoped.** A variable declared inside an
  `if`/`while`/function body is only visible until the closing `}`. This is
  scoping of *visibility* only — the underlying address a variable was
  allocated is never freed/reused, so heavy use of short-lived block-local
  variables still costs memory for the whole program's run.

### Other limitations

- 64-bit integer types (`long long`, `int64_t`, and `uint64_t`), `long double`,
  unions, and direct `printf("%f", value)` formatting are not supported.
- **Variables are never freed.** Every declaration (globals, function-call
  locals, compiler-generated temporaries) permanently owns its address for
  the program's whole lifetime.
- **No `rand`/randomness builtin.** The VM opcode `randreg` (0x1D) exists on
  both interpreters, but `compiler.py` has no `OPS` entry or builtin
  wrapping it, so it's currently unreachable from uniClang source. Use the
  raw opcode directly if you need it (hand-assemble, or add a builtin
  following the pattern of `getkey`/`isflagsupported` in `compiler.py`).

### Example

```c
#define MAX_ITERS 5
#define addfour(x) (x + 4)

uint32_t add(uint32_t a, uint32_t b) {
    return a + b;
}

uint32_t main() {
    if (isflagsupported(0, 1)) {
        flag(0, 1);   // mono 64x64 graphics on
        flag(1, 1);   // NES keyboard on
    }

    uint32_t i = 0;
    uint32_t sum = 0;

    while (i < MAX_ITERS) {
        i = i + 1;
        sum = add(sum, i);
        printf("i = %d, sum = %d\n", i, sum);
    }

    printf("addfour(sum) = %d\n", addfour(sum));

    if (sum > 10) {
        printf("Big sum!\n");
    } else {
        printf("Small sum.\n");
    }

    setpixel(i, 0, 1);
    refreshscreen();

    uint8_t key = getkey();
    if (key != 0) {
        printf("key: %d\n", key);
    }

    if (true) {
        exit();   // stops here; nothing after this runs
    }

    return 0;
}
```

See `examples/pong/pong.c` and `examples/game/game.c` for fuller programs
using functions, graphics, and keyboard input together.

---

## 3. The C interpreter (`C-interpreter/unilang.h`)

A header-only, freestanding implementation of the same ISA, for
Arduino/embedded targets (and desktop hosts, via `main.c`'s SDL2 example).
It never touches any specific graphics/input library directly — everything
that needs the outside world is an optional function pointer, wired up
after `ULVM_init`:

```c
#define ULVM_MEM_SIZE 256      // define before including
#include "unilang.h"

UnilangVM vm;
ULVM_init(&vm, my_outputchar, my_readbyte);   // required
ULVM_set_graphics(&vm, my_setpixel, my_getpixel, my_refreshscreen,
                   my_enablegraphics, my_disablegraphics, my_clearscreen);
ULVM_set_keyboard(&vm, my_getkeyboard);
ULVM_set_time(&vm, my_gettime, my_sleep);
ULVM_set_input(&vm, my_input);
ULVM_set_flagsupport(&vm, my_isflagsupported);

while (!ULVM_handlenextinstruction(&vm)) { /* runs until OP_EXIT */ }
```

- `outputchar`/`readbyte` are the only required callbacks (text output,
  bytecode/ROM fetch).   `readbyte` is also reused for `romread8/16/32/64`
  (absolute-address reads into the program's own bytecode), since on
  ROM-paged targets that isn't necessarily a flat pointer into RAM.
- Any other callback left `NULL` makes that opcode a safe no-op / return-0
  — a program that never touches graphics needs none of the graphics hooks.
- **`enablegraphics(wx, wy, colorsize)` / `disablegraphics()`** fire
  automatically whenever the bytecode's flag 0 value actually *changes* —
  this is how the host knows when to create/destroy a window (or turn on a
  physical display) and at what size, rather than polling. `colorsize` is
  bits-per-pixel (1/4/8) matching the flag-0 mode table.
- The SDL host supports all six graphics modes: 1-bit monochrome, 4-bit
  grayscale, and 8-bit RGB332 color at 64x64 or 128x64.
- **`clearscreen()`** is called automatically right after `refreshscreen()`
  fires, matching the VM's "flip then clear" framebuffer semantics from
  Section 1 — the host doesn't need to remember to clear manually.
- `ULVM_MEM_SIZE` defaults to 64 bytes with a compile-time `#warning` if you
  don't define it yourself.
- All memory access is bounds-checked; a malformed/unknown opcode halts the
  VM (`running = 0`) instead of reading past the end of `mem`.

`C-interpreter/main.c` is a working example wiring all of this to SDL2 for
a desktop build; see it for the concrete callback signatures. For AVR-based
Arduino boards (Uno/Nano/Mega), keep bytecode `const` **and** `PROGMEM`, and
read it back through `pgm_read_byte(&rom[addr])` — a plain `const` array
still gets copied into RAM on those boards otherwise. ARM-based boards
(Due, most ESP32/SAMD) don't need `PROGMEM`; a plain `const` array is fine.

**Speed on weak devices.** Two optional calls make the VM much faster without
using extra RAM beyond two small fields (a pointer and a length):

- `ULVM_set_rom(&vm, rom, size)` hands the VM a directly addressable bytecode
  array (RAM, or `PROGMEM` on AVR, read through `READ_FLASH_BYTE`), so
  instruction fetch no longer calls your `readbyte` callback for every byte.
  `ULVM_init` still needs a `readbyte` function; it is simply unused while a
  flat ROM is set. Skip this call if your bytecode lives somewhere that can't
  be addressed directly (SD card, external flash) and keep using `readbyte`.
- `ULVM_run(&vm, budget)` executes up to `budget` instructions per call
  (returns nonzero once the program has exited), keeping the program counter
  in a local variable. Use it instead of looping on
  `ULVM_handlenextinstruction` (which still works and runs one instruction),
  and do your own housekeeping (event polling, watchdog) between calls.

On a desktop test loop this made a compute-heavy program run about 2.2x
faster. Integer arithmetic remains 32-bit; floating-point operations use the
host's IEEE binary32/binary64 arithmetic. A target without binary64 support
can still run integer and `float` programs, but bytecode requiring `double`
will fault.