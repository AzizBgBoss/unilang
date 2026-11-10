# `.ulc` bytecode format

[Assembly index](README.md) ·
[Instruction reference](instruction-reference.md) ·
[Examples](examples.md)

## File layout

A `.ulc` file is a byte stream. It has no magic header, version field, or
section directory. Instructions begin at byte offset zero and are followed by
any compiler-emitted constant data. The VM begins executing at program
counter `0`; ROM reads use byte offsets into this same image.

The C compiler writes its generated instruction stream followed by constant
arrays, strings, and relocated tables. Branch immediates and ROM references
are absolute byte offsets from the start of the `.ulc` image. A hand-encoded
file must compute those offsets itself.

## Instruction encoding

Each instruction is encoded as:

```text
opcode:u8 operand0 operand1 ...
```

Operands are packed consecutively according to the instruction definition.
There is no alignment or padding between instructions. Multi-byte integers,
addresses, constants, and targets are unsigned big-endian encodings; register
indices, flags, and one-byte selectors occupy one byte.

Text operands used by `print`, `printn`, and `write` are:

```text
length:u16 big-endian | exactly length raw bytes
```

They are not null-terminated. `print` appends a newline; `printn` does not.

### Addresses and values

- A RAM address is a byte offset in the VM's `mem[]`.
- A ROM address is a byte offset in the `.ulc` image.
- Direct load/store operands encode RAM addresses as four bytes.
- Indirect load/store instructions take a register containing a RAM address.
- Jump/call targets are absolute byte offsets in the `.ulc` image.
- `setreg` immediates and ordinary 32-bit integers are big-endian.
- `setreg64` immediates and 64-bit integers are eight-byte big-endian values.
- 64-bit floating values in memory are big-endian bit patterns; `romread64`
  returns high and low 32-bit words separately.

RAM and ROM are separate address spaces. A bytecode offset is not a writable
RAM pointer; use ROM-read instructions for data embedded in the program.

## Register and numeric conventions

Register operands are 0-based indices `0..15`. The C compiler commonly uses
`r14`/`r15` as integer scratch registers and `r13`/`r14`/`r15` for floating
operation address arguments, but hand-written bytecode can use any register.
An instruction's documented destination register may also be one of its
inputs.

Each register stores 64 bits. Existing instructions keep their historical
32-bit behavior: they read the low 32 bits and write a zero-extended 32-bit
result. `movreg` copies all 64 bits, and `gettime` writes all 64 bits. The
value stack and host builtins that take 32-bit values still use the low 32
bits. The wider register file adds 128 bytes to each VM instance and 64-bit
operations can be slow on AVR-class targets. Bytecode using the new opcodes
requires a VM that implements them; older VMs will fault on these opcodes.

The `floatcast` numeric format IDs are:

| ID | Type |
|---:|---|
| 1 | signed 8-bit |
| 2 | unsigned 8-bit |
| 3 | signed 16-bit |
| 4 | unsigned 16-bit |
| 5 | signed 32-bit |
| 6 | unsigned 32-bit |
| 7 | IEEE-754 binary32 |
| 8 | IEEE-754 binary64 |
| 9 | signed 64-bit integer (`outfmt`) |
| 10 | unsigned 64-bit integer (`outfmt`) |

`floatop` uses formats 7 or 8. Its operation selectors are 1 add, 2 subtract,
3 multiply, 4 divide, 5 negate, 6 equality, 7 inequality, 8 less-than,
9 greater-than, 10 less-or-equal, 11 greater-or-equal, 12 sine, 13 cosine,
14 square root, and 15 `atan2(left, right)`. For comparison operations the
result is stored as a 32-bit boolean value.

## `outfmt` encoding

Opcode `0x3B` (`outfmt`) is a low-level formatted numeric output instruction:

```text
3B addr:u32 format:u8 conversion:u8 width:u8 precision:u8 flags:u8
```

The address points to a value in RAM. `format` uses the numeric format IDs
above. `conversion` is the ASCII byte for one of `diuoxXcfFeEgG`. `width` is
0..99. `precision` is 0..99 or `0xFF` for omitted precision. Flag bits are:

| Bit | Meaning |
|---:|---|
| 0 | `-` left alignment |
| 1 | `+` explicit sign |
| 2 | space sign |
| 3 | `#` alternate form |
| 4 | `0` zero padding |

An invalid numeric format, address range, unsupported conversion, reserved
flag bits, or out-of-range width/precision faults the reference VM. The C
compiler also rejects conversion/flag combinations it does not support before
emitting this opcode. Floating formatting uses the host's `snprintf`.

## `blit` descriptor

Opcode `0x26` takes a four-byte RAM address to eleven consecutive big-endian
32-bit words (44 bytes total):

| Word offset | Field | Meaning |
|---:|---|---|
| 0 | `x` | destination left |
| 4 | `y` | destination top |
| 8 | `width` | destination/source region width |
| 12 | `height` | destination/source region height |
| 16 | `source` | ROM or RAM source base address |
| 20 | `source_x` | source left |
| 24 | `source_y` | source top |
| 28 | `stride` | source row width in pixels |
| 32 | `flags` | source layout and transforms below |
| 36 | `palette` | RAM address of a 256-byte palette, if enabled |
| 40 | `transparent` | source value to skip, if enabled |

Flag bits: bit 0 flips horizontally; bit 1 flips vertically; bit 2 reads
packed 4-bit pixels (low nibble first); bit 3 applies the byte palette; bit 4
enables transparent-value skipping; bit 5 selects RAM source (otherwise ROM).
Clipping is applied to destination and source bounds. Invalid dimensions or
stride draw nothing.

## Faults and compatibility

The reference VM records a fault PC and opcode and stops on unknown opcodes,
invalid numeric operations, stack underflow/overflow, and checked invalid
targets/ranges. Optional callbacks may instead be absent; individual
instructions then use their documented fallback/no-op behavior. Always check
host callback availability when targeting another platform.

The complete opcode table is in the
[instruction reference](instruction-reference.md). For byte-level examples,
see [examples](examples.md).
