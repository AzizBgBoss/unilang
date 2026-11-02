# Assembly and bytecode

[Documentation index](../README.md) ·
[Bytecode format](bytecode-format.md) ·
[Instruction reference](instruction-reference.md) ·
[Examples](examples.md) · [Host integration](../vm/host-integration.md)

## There is no textual assembler in this repository

The project provides a C-like compiler (`compiler.py`) that writes `.ulc`
bytecode and a C VM (`C-interpreter/unilang.h`) that executes it. There is no
separate assembler executable, source grammar, or label-resolution tool.
Consequently, “assembly” in these docs means the VM instruction set and its
byte encoding, not an implemented textual assembly language.

The bytecode ISA can be hand-encoded, but bytecode is a stream of opcodes and
operands rather than human-readable source. Labels and ROM addresses must be
resolved to byte offsets. See the [format](bytecode-format.md), the
[instruction table](instruction-reference.md), and the
[annotated encodings](examples.md).

## Machine model

- 16 32-bit registers, `r0` through `r15`.
- Byte-addressed VM RAM, sized by the host.
- Program counter is a byte offset into the bytecode ROM.
- Multi-byte immediate operands and integer values are big-endian.
- Integer ALU operations are 32-bit. Floating instructions operate on values
  stored in RAM using explicit numeric-format selectors.
- A bounded return-address stack and a separate bounded 64-entry value stack
  are provided by the reference VM.
- Device features (graphics, keyboard, timing, input, persistence, audio) are
  callbacks supplied by the host.

## Related documentation

- [`.ulc` file layout and operands](bytecode-format.md)
- [Every defined opcode](instruction-reference.md)
- [Examples for jumps, calls, and I/O](examples.md)
- [Callbacks and embedding the VM](../vm/host-integration.md)
- [C-like source language](../c-language/overview.md)
