# Types and memory

[Index](../README.md) · [C overview](overview.md) ·
[Expressions and control flow](expressions-and-control-flow.md) ·
[Builtins](builtins.md)

## Scalar types

The compiler's integer model is fixed-width and byte-addressed. Narrow
integers are stored in whole bytes; they are not bit-packed.

| C-like spelling | Size | Notes |
|---|---:|---|
| `bool`, `_Bool`, `char`, `uint1`, `uint2`, `uint4`, `uint8`, `uint8_t`, `int8_t` | 1 byte | `uint1`/`uint2`/`uint4` are legacy names but still occupy one byte |
| `short`, `unsigned short`, `uint16`, `uint16_t`, `int16_t` | 2 bytes | signedness follows spelling |
| `int`, `unsigned`, `long`, `uint32`, `uint32_t`, `int32_t`, `size_t`, `uintptr_t`, enums | 4 bytes | `long` is 32-bit; `size_t` and `uintptr_t` are unsigned 32-bit |
| `long long`, `int64_t`, `uint64_t` | 8 bytes | Two's-complement signed or unsigned integers in one VM register |
| `float` | 4 bytes | IEEE-754 binary32 |
| `double`, `long double` | 8 bytes | IEEE-754 binary64; `long double` adds no precision |
| pointers | 4 bytes | byte-addressed 32-bit addresses |

`true` and `false` are the integer constants `1` and `0`. `NULL` is
predefined as `0`. Integer registers hold 64 bits; memory values wider than
one byte use big-endian order. Ordinary operations on 8-, 16-, and 32-bit
values retain the 32-bit VM semantics, while operations on 64-bit expressions
use the 64-bit ALU. `mul64` remains available for source compatibility and
still writes a high/low pair.

### Signedness and conversions

Signed integer formats use two's-complement interpretation. Narrow integer
operands are generally promoted for arithmetic; assignments and stores truncate
to the declared storage width. Signed comparisons, division, remainder, and
right shift use signed operations; unsigned right shift is logical.

The VM's float conversions use host `float`/`double` arithmetic. Float-to-
integer values outside the representable range produce zero in this VM; C
defines such conversions as undefined behavior. A host without IEEE binary32
or binary64 support faults if the program requests that format.

## Variables, arrays, and aggregates

Ordinary declarations and initializers are supported:

```c
uint8_t lives = 3;
int16_t offsets[3] = { -1, 0, 1 };
char label[] = "unilang";
```

Array indexing uses element sizes: `offsets[i]` addresses the `i`th
two-byte element. Arrays decay to pointers when passed or used as expressions.
`&object`, `*pointer`, pointer indexing, scaled pointer arithmetic, and pointer
parameters are supported for the implemented types.

`const` scalar values may become compile-time constants. Literal-initialized
constant arrays and strings are placed in bytecode ROM:

```c
const uint8_t rows[4] = { 1, 3, 7, 15 };
const char greeting[] = "hello";
```

ROM data may be read but should not be treated as writable RAM. A pointer
derived from a ROM object does not make that object writable.

Structs and unions are supported, including member access and pointers to
members. Struct fields occupy sequential storage; union members share offset
zero and the union size is its largest member's size. Positional union
initialization initializes its first declared member. Bitfield access is used
by the MicroCity port, but this is not a general guarantee of full C bitfield
layout compatibility.

The compiler supports `typedef`, enums, `sizeof`, casts, and `static` globals
within its subset. Initializer expressions must be supported by the compiler;
constant array initializers are folded at compile time. `{0}` zero-fills
aggregate/array storage.

## RAM size and address model

RAM is a byte array whose capacity is selected by the host's `ULVM_MEM_SIZE`.
The compiler accounts for program variables and emits a startup check. If the
program's estimated requirement exceeds host RAM, it prints a warning but
still runs; addresses can then overlap and behavior is unreliable. Graphics
framebuffers and audio banks are host-owned and do not count as VM RAM.

VM memory access is bounds checked. Out-of-range loads read as zero in the
low-level helper; invalid instructions that explicitly validate a range may
fault or report failure. Do not rely on out-of-range behavior as a C feature.

Two legacy extensions exist:

```c
alloc 8 scratch;
MEMORY[address:length] = value;
```

`alloc N name;` uses the old bit-count spelling but allocates
`ceil(N / 8)` bytes. `MEMORY[...]` syntax is rewritten to internal memory
helpers. Prefer ordinary declarations and pointers for new code.

## Related references

- [Supported operators and control flow](expressions-and-control-flow.md)
- [Compiler builtins and host features](builtins.md)
- [Bytecode memory and registers](../assembly/bytecode-format.md)
