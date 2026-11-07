# Expressions and control flow

[Index](../README.md) · [C overview](overview.md) ·
[Types and memory](types-and-memory.md) ·
[Functions and limitations](functions-and-limitations.md)

## Expressions

The compiler supports integer arithmetic `+ - * / %`, bitwise operations
`& | ^ ~`, shifts `<< >>`, comparisons `== != < > <= >=`, logical
`&& || !`, unary `+`/`-`, and the conditional operator `?:`. Assignment,
compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, bitwise and shift
assignments), comma expressions, and pre/post `++` and `--` are implemented.
Parentheses can be used to make grouping explicit.

Logical `&&` and `||` short-circuit. Comparisons produce a boolean-like 0/1
value and can be used in expressions or conditions. Integer right shift is
arithmetic for signed operands and logical for unsigned operands. Float
arithmetic and comparisons are available for `float` and `double`.

Arithmetic is done in 32 bits, following C's integer promotion: operands
narrower than 32 bits (such as `uint8_t`) are promoted to signed `int`, so
`selectx - 1` with `selectx == 0` is `-1`, not 255. Signed `/` and `%` truncate
toward zero and the remainder takes the sign of the dividend, so `-1 % 6` is
`-1` and `-7 / 2` is `-3`, also when folded at compile time. Constant folding
treats its operands as signed 32-bit values, so a folded `/` or `%` on an
unsigned constant above `0x7FFFFFFF` is read as negative. Narrowing happens only
when a value is stored into a narrower variable, so wrapping a `uint8_t` index
still needs the `(x + n - 1) % n` form.

The compiler performs constant folding. Some builtins require compile-time
constants; see [builtins](builtins.md). Pointer arithmetic scales by the
pointee's byte size.

## Statements

Supported control-flow statements include:

```c
if (condition) {
    work();
} else {
    other_work();
}

for (int i = 0; i < 4; ++i) {
    if (i == 2) continue;
    work();
}

while (ready()) {
    step();
}

do {
    step();
} while (ready());
```

`break` exits the nearest loop or switch; `continue` advances the nearest loop.
Blocks provide lexical visibility for local declarations. `goto label;` and
function-scoped labels are supported; duplicate or unknown labels are compile
errors.

## `switch`

Case labels must be compile-time integer constants. `default`, `break`, and C
fallthrough are supported. Statements before the first `case`/`default` label
are rejected, and duplicate case values or multiple `default` labels are
errors.

Dense case ranges (at least three cases, span at most 64 and no more than
twice the number of cases) use a ROM jump table and an indirect jump. Sparse
switches use a comparison chain. Out-of-range values and holes go to
`default`, or the end of the switch if no default exists. Because optimizer
control-flow analysis does not yet model indirect targets, programs containing
a dense switch bypass the optimizer.

Example:

```c
switch (state) {
case 0:
    start();
    break;
case 1:
case 2:
    update();
    break;
default:
    reset();
}
```

## `return` and entry-point behavior

Non-void functions must return a value when a return is used; `void` functions
may use `return;` or fall through. Returning a value from a void function,
omitting a value in a non-void function, or using a void call as an expression
is rejected.

`return` in `main` halts the bytecode program. An integer expression is loaded
into register `r0` before the `exit` opcode, and the VM records it as
`vm.exit_code`. The SDL desktop host returns that value as its process exit
status, so `echo $?` shows it (POSIX shells keep only the low 8 bits). Falling
off the end of `main`, a bare `return;` and `exit();` all give `0`. Bytecode
compiled before this behavior existed does not set `r0` and may report an
arbitrary exit code, so recompile old `.ulc` files.

## Known gaps

This is not a complete C grammar or standard-library implementation. For
unsupported declarations and function behavior, see
[functions and limitations](functions-and-limitations.md). For macro and
conditional compilation behavior, see [preprocessor](preprocessor.md).
