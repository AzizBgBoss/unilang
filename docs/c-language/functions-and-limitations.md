# Functions, optimization, and limitations

[Index](../README.md) · [C overview](overview.md) ·
[Expressions and control flow](expressions-and-control-flow.md) ·
[Builtins](builtins.md)

## Function calls are inlined

Calls to defined functions are expanded by the compiler at each call site.
Every inlined copy receives its own parameter, local, and result storage.
`return` writes the result and jumps to the end of that inlined copy. This
supports nested calls between named functions without relying on the VM call
stack:

```c
int twice(int x) { return x * 2; }
int add_one(int x) { return twice(x) + 1; }
int main(void) { return add_one(20); }
```

The tradeoff is that each call increases bytecode size and compile-time RAM
allocation. Functions are not general runtime subroutines from the C compiler.
Function prototypes are accepted, but a call must resolve to a function
definition included among the compiler inputs.

### Unsupported function features

- Direct and mutual recursion are rejected to prevent infinite inlining.
- Calls through function pointers are not implemented. Calls must name a
  function definition the compiler knows.
- General variadic functions are rejected. `printf` is a special fixed-format
  compiler builtin, not the C varargs ABI.
- Function pointers and 64-bit integer arguments/returns are unsupported.

The VM itself has bounded `subroutine`/`return` and indirect-call opcodes, but
that does not mean the C compiler uses them for ordinary function calls.
Details are in the [bytecode reference](../assembly/instruction-reference.md).

## Optimizer

Optimization is enabled by default. `-O0` disables the optimizer; `-O1` and
`-O2` both enable the current optimizer. It performs control-flow cleanup,
constant propagation, store/load forwarding, dead temporary removal, register
promotion, instruction folding, loop rotation, and RAM lifetime repacking.
Repacking never places a variable at RAM address `0`, because pointer
arguments such as the `netopen` host string treat address `0` as NULL. It also
treats `exit` as reading `r0`, so `main`'s return value is preserved.

Variables whose address escapes are kept in memory. Globals and address-taken
objects have more constrained placement. With `-O0`, declarations are not
repacked by the optimizer, so memory use can increase.

Programs that compile dense `switch` dispatch use indirect jump targets and
currently skip optimization because the optimizer does not model those targets.
Use [the differential fuzz tool](../../tools/fuzz_opt.py) when investigating
optimizer changes.

## C compatibility is intentionally incomplete

Implemented constructs and types are listed in
[types and memory](types-and-memory.md) and
[expressions and control flow](expressions-and-control-flow.md). Common
unsupported features include:

- recursive calls, indirect function calls, and general variadic functions;
- `long long`, `int64_t`, and `uint64_t`;
- a full C standard library or platform ABI;
- arbitrary `printf` conversions and dynamic `*` width/precision;
- writable ROM objects and a general heap allocator.

`long double` aliases binary64. `uint1`, `uint2`, and `uint4` are byte-sized
legacy aliases, not packed bitfields. Some C++-style constructs used by the
MicroCity example are compatibility accommodations, not a promise of general
C++ support.

When porting code, compile frequently and treat compiler diagnostics as the
authoritative supported-subset boundary.
