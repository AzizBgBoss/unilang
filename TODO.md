# Documentation work

Approved and completed. The documentation index is [docs/README.md](docs/README.md).

## Proposed navigation

Create a `docs/` folder with a landing page and focused reference pages:

```text
docs/
  README.md
  c-language/
    overview.md
    types-and-memory.md
    expressions-and-control-flow.md
    functions-and-limitations.md
    builtins.md
    preprocessor.md
  assembly/
    README.md
    bytecode-format.md
    instruction-reference.md
    examples.md
  vm/
    host-integration.md
```

`docs/README.md` will be the central table of contents, link related pages
within the docs, and point to the root [README](./README.md) and relevant
working examples. The root README will link to this documentation index rather
than duplicate every detail.

## Planned documentation work

- [x] **Audit the implemented languages before writing.** Cross-check compiler
  behavior against `compiler.py`, `preprocessor.py`, `optimizer.py`, VM opcode
  decoding in `C-interpreter/unilang.h`, and existing tests/examples. Record
  supported behavior separately from C/assembly features that look familiar
  but are not implemented.
- [x] **Document the C-like language overview and types.** Explain source
  structure and entry point, supported declarations and types, byte sizes,
  signedness, pointers and arrays, structs/unions, constants, ROM versus RAM,
  and memory sizing. Include compact compile-ready examples.
- [x] **Document C expressions and statements.** Cover operators, precedence
  where implementation differs or is limited, casts, `sizeof`, initialization,
  scopes, conditionals, loops, `switch` fallthrough, `goto`/labels, and
  `break`/`continue`/`return`. Call out unsupported constructs and meaningful
  behavior differences from standard C.
- [x] **Document functions, compiler limits, and optimization.** Explain
  inlining, argument and return behavior, `void`, recursion rejection,
  function-pointer limitations, variadic-function diagnostics, `-O0`, and
  cases where optimization changes code generation. Provide clear “supported
  / unsupported / workaround” notes without implying full C compliance.
- [x] **Document builtins and platform-facing features.** Give signatures,
  argument constraints, results, side effects, flag dependencies, and small
  examples for graphics, audio, persistence, memory operations, random/time,
  stack operations, numeric helpers, and output functions including the
  supported `printf` formats.
- [x] **Document the preprocessor and compiler CLI.** Describe includes,
  macros, conditional directives, built-in definitions, include/define flags,
  directory inputs, output selection, and diagnostics/limitations, with
  runnable command examples.
- [x] **Document the assembly/bytecode model and file format.** State clearly
  that this repository currently compiles C-like source directly to `.ulc` and
  has no separate textual assembler or assembler grammar. Describe byte order,
  instruction boundaries, operand encodings, ROM data/relocations, registers,
  RAM, flags, and how to inspect/run bytecode. If “assembly language” means a
  new textual assembly syntax, that is a separate feature and will not be
  invented as documentation.
- [x] **Document every VM instruction.** Build a complete opcode reference
  from the VM decoder, including opcode byte, operands and widths, semantics,
  register/memory effects, branches, faults/bounds behavior, and host callback
  requirements. Include reserved/unknown opcode behavior and verify table
  entries against both dispatch implementations.
- [x] **Add annotated bytecode examples and host integration notes.** Show
  minimal program encodings and examples for branches, calls/returns, memory,
  graphics, and audio; explain the VM host callbacks and identify which
  features are optional or host-dependent.
- [x] **Add navigation and verify documentation.** Link the docs index from
  the root README and link relevant examples from each page. Check relative
  links, commands, opcode coverage, and statements against the implementation;
  run existing regressions if any code-adjacent examples are added.

## Delivered structure

See [docs/README.md](docs/README.md) for navigation through the C-like language,
preprocessor/CLI, bytecode format, complete opcode reference, annotated
bytecode, and VM host integration. There is no separate textual assembler in
the repository; the assembly pages explicitly document the implemented
bytecode ISA rather than inventing assembler syntax.