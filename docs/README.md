# unilang documentation

Welcome to the reference for unilang's C-like source language, `.ulc` bytecode,
VM, and host integrations. The language is a deliberately bounded C-like
compiler, not a conforming C compiler. Read the limitations pages before
porting a larger C project.

## Start here

- [Quick start and C language map](c-language/overview.md)
- [Types, pointers, arrays, and memory](c-language/types-and-memory.md)
- [Expressions and control flow](c-language/expressions-and-control-flow.md)
- [Functions, optimizer, and limitations](c-language/functions-and-limitations.md)
- [Builtins: graphics, audio, storage, and math](c-language/builtins.md)
- [Preprocessor and compiler command line](c-language/preprocessor.md)

## Bytecode and VM

- [Assembly and bytecode overview](assembly/README.md) — important: there is
  no separate textual assembler in this repository.
- [`.ulc` bytecode format](assembly/bytecode-format.md)
- [Complete instruction reference](assembly/instruction-reference.md)
- [Hand-encoded bytecode examples](assembly/examples.md)
- [VM host integration](vm/host-integration.md)

## Examples and implementation

- [Example programs](../examples/README.md)
- [Compiler implementation](../compiler.py)
- [Preprocessor implementation](../preprocessor.py)
- [VM implementation](../C-interpreter/unilang.h)
- [SDL2 host](../C-interpreter/main.c)
- [Headless host and regression runner](../tools/regress.py)

Use the links on each page to navigate between related concepts. Where the
implementation has a deliberate limitation or a behavior that differs from
standard C, it is called out explicitly rather than assumed to work like a
desktop C compiler.
