# C-like language overview

[Documentation index](../README.md) ·
[Types and memory](types-and-memory.md) ·
[Expressions and control flow](expressions-and-control-flow.md) ·
[Functions and limitations](functions-and-limitations.md) ·
[Builtins](builtins.md) ·
[Preprocessor and CLI](preprocessor.md)

## What this language is

`compiler.py` preprocesses and parses a restricted C-like language, then emits
`.ulc` bytecode for the [unilang VM](../assembly/README.md). It uses
[`pycparser`](https://github.com/eliben/pycparser) for parsing. C syntax is
familiar, but the supported subset and machine model are project-specific.
Unsupported constructs fail compilation with a diagnostic; successful parsing
does not imply full ISO C semantics.

The compiler emits bytecode directly. This repository does **not** contain a
separate textual assembler; see the [assembly overview](../assembly/README.md)
for what is and is not meant by “assembly language” here.

## Compile a program

From the repository root:

```sh
python -m pip install pycparser
python compiler.py examples/cube3d/cube3d.c
```

The bytecode is written next to the source as `examples/cube3d/cube3d.ulc`.
Use `-o` only to pick a different name or location.

For the SDL2 desktop host, build it first:

```sh
cc -std=c11 -O2 C-interpreter/main.c -o C-interpreter/main \
  $(pkg-config --cflags --libs sdl2) -lm
C-interpreter/main examples/cube3d/cube3d.ulc
# Add --net after the bytecode path for SDL networking.
C-interpreter/main examples/network/pong_host.ulc --net
```

See the [compiler and preprocessor reference](preprocessor.md) for all
arguments, multiple inputs, and platform-specific notes. The
[example index](../../examples/README.md) lists programs that can be compiled
as smoke tests or used as starting points.

## Program shape

The compiler requires a defined `main` function. It emits global
initialization, compiles the body of `main` last, and appends an implicit halt:

```c
#include <stdint.h>

uint32_t main(void)
{
    uint32_t answer = 40 + 2;
    printf("answer = %u\n", answer);
    return 0;
}
```

`main` may return early. An integer value returned from `main` becomes the
VM's exit code (see below); falling off the end, `return;` and `exit();` give
`0`. Floating-point return values are not converted.
`void main(void)` is accepted as an entry point.

## Topics

- [Types, pointers, arrays, and memory](types-and-memory.md)
- [Expressions, statements, and control flow](expressions-and-control-flow.md)
- [Function inlining, optimization, and unsupported C](functions-and-limitations.md)
- [Compiler builtins](builtins.md)
- [Preprocessor behavior and CLI](preprocessor.md)

## Important model differences

- Pointers are 32-bit byte addresses into the VM's RAM model; constant arrays
  and literals can be ROM-backed and read-only.
- Integer values are at most 32 bits. `double` and `long double` are binary64,
  not integer types.
- Calls to defined functions are inlined at each call site. Recursion and calls
  through function pointers are not supported.
- Most I/O, graphics, audio, time, and persistence behavior is provided by
  optional host callbacks. The same bytecode can therefore behave differently
  on hosts that do not implement those callbacks.

These are intentional constraints; consult the detailed references before
assuming desktop C behavior.
