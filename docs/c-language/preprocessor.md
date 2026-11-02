# Preprocessor and compiler command line

[Index](../README.md) · [C overview](overview.md) ·
[Examples](../../examples/README.md)

## Command line

```text
python compiler.py input.c|input_dir [input.c|input_dir ...]
  [-o output.ulc] [-I dir]... [-D NAME[=value]]... [-O0|-O1|-O2]
```

Examples:

```sh
python compiler.py examples/cube3d/cube3d.c -o /tmp/cube3d.ulc
python compiler.py examples/microcity -o /tmp/microcity.ulc -D _WIN32=1
python compiler.py one.c two.c -I include -D DEBUG=1 -o combined.ulc
```

Files and directories may be mixed as inputs. A directory contributes its
immediate `.c` and `.cpp` files in sorted order; directory traversal is not
recursive. Duplicate source paths are included only once. The default output
is the input basename with `.ulc`, or `combined.ulc` for multiple source files.
`-O0` disables optimization; `-O1` and `-O2` currently both enable it.

The compiler has a built-in `include` directory. `-I` adds include search
directories. `-D NAME` defines `NAME` as `1`; `-D NAME=value` supplies a
replacement value. Options may be written as `-Ipath` / `-DNAME=1` or as a
flag followed by its value.

## Directives

The preprocessor implements:

- `#include "file"`: search beside the including file, then `-I` directories.
- `#include <file>`: search `-I` directories. If a supported standard header
  is not found, it is accepted as built into the compiler and contributes no
  declarations.
- Object-like and function-like `#define`, `#undef`, `#` stringification,
  `##` token pasting, and `__VA_ARGS__` in macros.
- `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, and `defined()`.
- `#pragma once`, `#error`, `#warning`, and `#line` (`#line` is ignored).
- Backslash-newline splicing and `__LINE__` / `__FILE__`.

Conditional expressions support integer constants, character constants,
unknown identifiers as zero, unary `+ - ! ~`, arithmetic, shifts, comparisons,
bitwise/logical operators, parentheses, and `?:`. Division by zero in `#if`
is an error. Integer division truncates toward zero.

Predefined macros include `NULL`, `RAND_MAX`, `CHAR_BIT`, `UINT8_MAX`,
`UINT16_MAX`, `UINT32_MAX`, and `__UNILANG__`. Standard headers treated as
built in include `stdint.h`, `stdbool.h`, `stdio.h`, `stdlib.h`, `stddef.h`,
`string.h`, `limits.h`, and `stdarg.h`.

### Preprocessor limitations and gotchas

- Function-like macro invocations must fit on one (possibly spliced) line.
- Macro names are not expanded inside string or character literals.
- This is a project-specific preprocessor, not a complete implementation of
  every standard preprocessing rule.
- Standard headers accepted as built-ins are not actually parsed or imported;
  their relevant names are provided by compiler support.
- Include nesting is limited to 64 levels. Recursive includes without
  `#pragma once` can hit this limit.
- `#warning` is printed to standard error. `#error` stops preprocessing.

## Diagnostics and source locations

The preprocessor emits line markers so parser errors retain source-file
locations across includes. Unsupported syntax, unknown include files, malformed
directives, and unsupported compiler features terminate compilation with an
error. The compiler writes bytecode only after successful compilation.

For supported declarations and operations, see the C-language pages:
[types](types-and-memory.md), [statements](expressions-and-control-flow.md),
[functions](functions-and-limitations.md), and [builtins](builtins.md).
