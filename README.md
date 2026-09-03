# unilang

A custom low-level programming language / bytecode ISA (`uniClang`) targeting
NDS/GBA-class and Arduino-class hardware, plus a small C-like compiler that
emits bytecode for it.

- `main.py` - the unilang bytecode VM (interprets compiled `.ulc` files)
- `compiler.py` - the uniClang compiler (reads `.uc` source and emits `.ulc`)
- `test.ul` - a hand-written example in unilang's own text/assembly form
- `examples/` - source/examples such as `pong.c` and compiled `.ulc` bytecode

---

## 1. The unilang machine model

- **Flat, bit-addressable memory.** Every address is a *bit* offset, and every
  operand that reads/writes memory takes an explicit `size` in bits. There are
  no types at the hardware level - an "int" is an unsigned 32-bit value in the
  C subset, represented with however many bits you say at the ISA level
  it is. Out-of-range addresses wrap around (modulo the total memory size)
  rather than crashing.
- **Memory size** is set in `main.py` (`memsize`, in bits). Change it there if
  you need more/less RAM. The compiler no longer assumes a fixed size - see
  Section 2's "Memory sizing" note.
- **Flags** (`flags` memory, separate from `mem`) configure hardware
  capabilities:
  - **Flag 0 - graphics**: `0` disabled, `1` mono 64x64, `2` 4-shade 64x64,
    `3` 8-bit color 64x64, `4` mono 128x64, `5` 4-shade 128x64, `6` 8-bit
    color 128x64. Enabling graphics reserves that many bits off the *end* of
    memory as a framebuffer.
  - **Flag 1 - keyboard**: `0` disabled, `1` NES-style (Up/Down/Left/Right/
    A/B/Select/Start, 8 bits), reserved just before the framebuffer. The bits
    are ordered Up=`1` (bit 0), Down=`2` (bit 1), Left=`4`, Right=`8`, C=`16`,
    V=`32`, Backspace=`64`, and Enter=`128`. Only usable once graphics are
    enabled.
- **pygame is loaded lazily, and failures are fatal.** `main.py` doesn't
  import `pygame` at startup - only the first time a program actually queries
  or uses flag 0/1 (graphics/keyboard). Programs that never touch graphics
  don't need pygame installed at all. But if a program *does* need it and
  either pygame can't be imported or fails to initialize a display (e.g. no
  display server available), the VM prints a clear error and exits with
  status 1 instead of silently continuing headless.
- **Screen redraw is explicit and clears the framebuffer.** The VM only pumps
  window/input events every instruction; the actual pixel redraw + display flip
  happens *only* when the bytecode executes `refreshscreen`. After the flip,
  the framebuffer is cleared to zero, so programs should draw the complete
  next frame rather than erase the previous one manually. Call it once per
  frame after your pixel writes - doing the redraw automatically on every
  instruction (the old behavior) meant any nontrivial amount of per-frame
  drawing showed up as mostly-black flicker, since the display kept flipping
  mid-draw.
- **No call stack.** `subroutine`/`return` use a single global "return
  address" register, not a stack - so at the raw bytecode level, a subroutine
  cannot itself call another subroutine, and there's no recursion. (The C
  compiler avoids this entirely - see Section 2.)

### Opcode reference

All multi-byte operands are big-endian. `addr`/`val`/`pos`/`count` are 4
bytes, `size`/`flag` are 1 byte, `text` is a 2-byte length prefix followed by
raw UTF-8 bytes.

| Byte | Name | Operands | Description |
|---|---|---|---|
| 0x00 | `nop` | - | do nothing |
| 0x01 | `flag` | flag, val | set a flag (see above) |
| 0x02 | `isflagsupported` | flag, val, addr, size | check if a flag value is supported on this platform |
| 0x03 | `mem` | addr, size, val | set memory to an immediate value |
| 0x04 | `setmem` | addr1, size1, addr2, size2 | **pointer copy**: read the pointer stored *at* addr1, read `size1` bits from *that* address, then read the pointer stored at addr2 and write `size2` bits *there* |
| 0x05 | `write` | text, addr, size | write a string into memory, one char per `size`-bit slot |
| 0x06 | `outc` | addr, size | print memory value as a character |
| 0x07 | `out` | addr, size | print memory value as a number |
| 0x08 | `input` | chars, addr, size | read `chars` characters into memory |
| 0x09 | `print` | text | print text + newline |
| 0x0A | `printn` | text | print text, no newline |
| 0x0B | `printv` | chars, addr, size | print `chars` characters from memory + newline |
| 0x0C | `printvn` | chars, addr, size | same, no newline |
| 0x0D | `rand` | min, max, addr, size | random value in `[min, max)` |
| 0x0E | `add` | val, addr1, size1, addr2 | `addr2 = addr1 + val` |
| 0x0F | `addv` | addr1,size1,addr2,size2,addr3,size3 | `addr3 = addr1 + addr2` |
| 0x10 | `sub` | val, addr1, size1, addr2 | `addr2 = addr1 - val` |
| 0x11 | `subv` | ...x3 | `addr3 = addr1 - addr2` |
| 0x12 | `mul` | val, addr1, size1, addr2 | `addr2 = addr1 * val` |
| 0x13 | `mulv` | ...x3 | `addr3 = addr1 * addr2` |
| 0x14 | `div` | val, addr1, size1, addr2 | `addr2 = addr1 / val` |
| 0x15 | `divv` | ...x3 | `addr3 = addr1 / addr2` |
| 0x16 | `mod` | val, addr1, size1, addr2 | `addr2 = addr1 % val` |
| 0x17 | `modv` | ...x3 | `addr3 = addr1 % addr2` |
| 0x18 | `cur` | - | print current bytecode position (debug) |
| 0x19 | `memory` | - | dump memory (debug) |
| 0x1A | `flags` | - | dump flags (debug) |
| 0x1B | `setpc` | pos | unconditional jump |
| 0x1F | `compare` | val, addr1, size1, addr2, size2 | compare an immediate to memory; result (0=eq, 1=val>addr1, 2=val<addr1) is written to **addr2** (your choice), `size2` bits |
| 0x20 | `comparev` | addr1,size1,addr2,size2,addr3,size3 | compare two memory values; result (0=eq, 1=addr1>addr2, 2=addr1<addr2) goes to **addr3**, your choice |
| 0x21 | `isequal` | val, addr1, size1, addr2 | `addr2 (1 bit) = (val == addr1)` |
| 0x22 | `not` | addr1,size1,addr2,size2 | bitwise NOT |
| 0x23-0x2A | `or`/`and`/`nor`/`nand`/`xor`/`xnor`/`shl`/`shr` | addr1,size1,addr2,size2,addr3,size3 | bitwise ops, `addr3 = addr1 OP addr2` |
| 0x2B | `subroutine` | pos | call (see call-stack caveat above) |
| 0x2C | `return` | - | return from a subroutine |
| 0x2D | `if` | addr, size, pos | jump if memory != 0 |
| 0x2E | `ifnot` | addr, size, pos | jump if memory == 0 |
| 0x2F | `ifsubroutine` | addr, size, pos | call if memory != 0 |
| 0x30 | `ifnotsubroutine` | addr, size, pos | call if memory == 0 |
| 0x31 | `sleep` | ms | sleep (`ms` is a literal, not a memory address) |
| 0x32 | `gettime` | addr, size | current time (ms) into memory |
| 0x33 | `getmemsize` | addr, size | total memory size (bits) into memory |
| 0x34 | `refreshscreen` | - | redraw the screen from the current framebuffer and flip the display (see note above - call once per frame, not automatic) |
| 0xFF | `exit` | - | halt |

Run bytecode with: `python main.py -f program.ulc`

---

## 2. The C-to-ULC compiler (`compiler.py`)

```
pip install pycparser
python compiler.py input.uc -o output.ulc
python main.py -f output.ulc
```

It parses a **deliberately small subset of C-like syntax** with `pycparser`
and emits `.ulc` bytecode directly (no separate assembler step).

### Memory sizing

The compiler does **not** hardcode `main.py`'s `memsize`. Every compiled
program starts with a small prologue that calls `getmemsize` to ask the VM
for its actual memory size at runtime, and stores it for later use.
`setpixel()`/`getkey()` use that runtime value (via pointer-indirect
addressing) to locate the framebuffer/keyboard, so they stay correct however
big `memsize` is - nothing needs to be kept in sync between the two files.

The prologue also compares that runtime size against how many bits the
program's own variables (plus the framebuffer/keyboard, if the program uses
graphics) actually need, computed at compile time, and prints a warning at
startup if there isn't enough room:

```
WARNING: this program needs at least 5344 bits of memory, but only 4096 are
available. Increase 'memsize' in main.py.
```

The program still runs after the warning - it just may misbehave, since a
variable and a framebuffer pixel can end up sharing the same address and
silently corrupt each other. If you see this, increase `memsize` in
`main.py`.

### Supported

- `uint32 main() { ... }` - the entry point; its body is compiled last, after
  `exit` is appended.
- Fixed-width unsigned types: `bool`, `uint1`, `uint2`, `uint4`, `uint8`,
  `uint16`, and `uint32`. `bool` is an alias of `uint1`, and `true` / `false`
  are exactly `1` / `0`.
- Declarations may have an initializer: `uint8 x;` or `uint16 x = 5;`
- Assignment: `x = expr;`
- Arithmetic: `+ - * / %` (binary), `x++` / `x--`
- Comparisons: `== != < > <= >=` (used in `if`/`while` conditions)
- Control flow: `if (...) { } else { }`, `while (...) { }`
- Functions: `uint32 add(uint32 a, uint32 b) { return a + b; }` - see caveats below
- `printf("literal text\n");`
- `printf("...%d...\n", expr);` - one or more `%d`, matched left-to-right
  against extra arguments; other format specifiers aren't supported
- `exit();` - halts the program immediately (emits the raw `exit` opcode).
  Like `flag`/`setpixel`, it's a statement only and can't be used as an
  expression/value.
- Comments: `// line comments` and `/* block comments */` are stripped
  before parsing (string/char literals containing `//` or `/*` are left
  alone).
- `#define` macros, expanded textually before parsing:
  - Object-like: `#define WIDTH 64`
  - Function-like: `#define addfour(x) (x + 4)`
  - Other preprocessor directives (`#include`, etc.) are still just
    stripped/ignored - there's no real preprocessor, just `#define`.
- **Builtins** for hardware access:
  - `flag(index, value);` - raw `flag` opcode (see flag table in Section 1). Does
    not return a value - used as a statement only.
  - `setpixel(x, y, val);` - plots a pixel, assuming mono 64x64 graphics
    (`flag(0, 1)`). `x`/`y` may be variables of any supported width (they're
    normalized to full width internally, so a `uint8` coordinate is safe);
    `val` is treated as a boolean (0/1). Does not return a value. Keep
    `x`/`y` within `0..63` - the VM wraps out-of-range addresses instead of
    crashing, but a wrapped write still lands somewhere you didn't intend
    (e.g. corrupting the keyboard byte or another variable), so clamp your
    coordinates in C. Internally reuses a small set of scratch addresses
    across every call site instead of allocating fresh ones each time, since
    `setpixel` is often called from inside a helper function that itself
    gets inlined at several places.
  - `getkey()` - reads the 8-bit NES-style keyboard state. **Assumes**
    `flag(0, 1)` and `flag(1, 1)` were already called and remain set.
    Returns a value, so it can be used in expressions, e.g.
    `uint8 k = getkey();`.
  - `isflagsupported(flag, val)` - returns 1 if that flag/value combo is
    supported on this platform, 0 otherwise. Both arguments must be
    constants (matching the raw opcode's compile-time nature). Returns a
    value, e.g. `if (isflagsupported(0, 1)) { ... }`.
  - `sleep(ms);` - pauses execution for `ms` milliseconds. `ms` must be a
    constant (the underlying opcode takes a literal, not a memory read).
  - `refreshscreen();` - redraws the screen from the current framebuffer,
    flips the display, and clears the framebuffer for the next frame. **Call
    this once per frame**, after your `setpixel` calls, if you're doing
    graphics - see the note in Section 1.

### How functions work (and their limits)

Function calls are compiled by **inlining**: each call site gets its own
fresh copy of the callee's parameters, locals, and a return-value slot, with
`return` compiled as "write result, then jump to the end of this inlined
copy". This sidesteps unilang's lack of a real call stack entirely, so:

- Functions can call other functions, including from inside other
  functions - not just from `main()`.
- **Recursion (direct or mutual) is not supported.** Since a recursive call
  would mean inlining forever, the compiler detects it and raises a
  compile-time error instead of hanging or producing broken code.
- Every call costs bytecode size and memory (a fresh copy of the callee's
  locals), not a shared, reusable block - there's no runtime call overhead,
  but heavily-called functions will bloat the output and eat into the
  memory budget (see "Memory sizing" above). This is the main thing to
  watch for: a helper that draws something and gets called several times
  per frame can add up fast.
- Function parameters shadow same-named variables only for the duration of
  that inlined call; the caller's own variables of the same name are
  restored afterward.
- **Blocks are properly scoped.** A variable declared inside an `if`/`while`
  body (or a function body) is only visible until the closing `}` - using it
  afterward is a compile-time error, and a same-named variable in a sibling
  block doesn't collide with it. Note this is scoping of *visibility* only:
  the underlying bit address a variable was allocated is never freed/reused
  (see "Other limitations" below), so heavy use of short-lived block-local
  variables still costs memory for the whole program's run.

### Other limitations

- Only fixed-width unsigned types are supported: `bool` / `uint1` (1 bit),
  `uint2`, `uint4`, `uint8`, `uint16`, and `uint32`. There are no pointers,
  structs, or floats.
- No `for` loops (use `while`), no `&&`/`||` (split into nested `if`s).
- Comparison operators (`< > == ...`) can only be used directly in `if`/
  `while` conditions - there's no way yet to store a comparison's result
  into a variable (e.g. `uint8 x = a < b;` isn't supported).
- **Variables are never freed.** Every declaration (globals, function-call
  locals, compiler-generated temporaries) permanently owns its bit address
  for the whole program's lifetime - there's no address reuse, so memory
  only grows, even though block scoping (above) does limit *where a name is
  visible*. Keep this in mind on memory-constrained targets (the startup
  warning above will tell you if you've overrun what's available).

### Example

```c
#define MAX_ITERS 5
#define addfour(x) (x + 4)

uint32 add(uint32 a, uint32 b) {
    return a + b;
}

uint32 main() {
    if (isflagsupported(0, 1)) {
        flag(0, 1);   // mono 64x64 graphics on
        flag(1, 1);   // NES keyboard on
    }

    uint32 i = 0;
    uint32 sum = 0;

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

    uint8 key = getkey();
    if (key != 0) {
        printf("key: %d\n", key);
    }

    if (true) {
        exit();   // stops here; nothing after this runs
    }

    return 0;
}
```

See `examples/pong.c` for a fuller program using functions, graphics, and
keyboard input together.