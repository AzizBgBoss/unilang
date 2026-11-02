# Hand-encoded bytecode examples

[Assembly index](README.md) ·
[Bytecode format](bytecode-format.md) ·
[Instruction reference](instruction-reference.md)

These are raw encodings, not source for an assembler. Every jump destination
is a byte offset from the beginning of the `.ulc` file. Multi-byte values are
big-endian.

## Halt

```text
FF
```

`0xFF` is `exit`.

## Set a register, conditionally skip, then halt

```text
offset  bytes                         meaning
0000    40 00 00 00 00 01             setreg r0, 1
0006    52 00 00 00 00 12             ifnotreg r0, 0x00000012
000C    40 01 00 00 00 2A             setreg r1, 42
0012    FF                            exit
```

`setreg` is six bytes: opcode, register index, and a four-byte value.
`ifnotreg` is six bytes: opcode, condition register, and four-byte target.
Since `r0` is nonzero, execution falls through to set `r1`; if the initial
value were zero, the jump would skip directly to `exit`.

The same byte stream as a C array:

```c
static const uint8_t program[] = {
    0x40, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x52, 0x00, 0x00, 0x00, 0x00, 0x12,
    0x40, 0x01, 0x00, 0x00, 0x00, 0x2A,
    0xFF
};
```

Load this with `ULVM_set_rom(&vm, program, sizeof(program))` after
initialization; see [host integration](../vm/host-integration.md).

## Call and return

```text
0000    10 00 00 00 07                subroutine 0x00000007
0005    FF                            exit
0006    00                            (unused padding byte)
0007    40 00 00 00 00 2A             setreg r0, 42
000D    11                            return
```

`subroutine` pushes the bytecode address following its five-byte instruction.
`return` restores it. The padding at offset 6 ensures the routine begins at
the specified offset 7. The VM's return stack is bounded and faults on
overflow/underflow; the C compiler's ordinary calls are instead inlined.

## Recommendations

- Prefer the C-like compiler for normal programs; it computes offsets and ROM
  relocations for you.
- Use the [instruction reference](instruction-reference.md) to calculate
  instruction lengths and operand formats.
- Treat addresses as byte offsets. RAM addresses and ROM program offsets are
  separate spaces.
- Validate hand-written streams with the headless host and VM fault metadata
  before running on hardware.
