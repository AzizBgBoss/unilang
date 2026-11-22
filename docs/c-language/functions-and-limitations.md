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
allocation. Two exceptions compile a function once as a real VM subroutine
instead: [recursive functions](#recursive-functions) always, and large
repeatedly called functions when [`-Os`](#size-optimization--os) is given.
Function prototypes are accepted, but a call must resolve to a function
definition included among the compiler inputs.

### Unsupported function features

- Calls through function pointers are not implemented. Calls must name a
  function definition the compiler knows.
- General variadic functions are rejected. `printf` is a special fixed-format
  compiler builtin, not the C varargs ABI.
- Function pointers and 64-bit integer arguments/returns are unsupported.

## Recursive functions

Direct and mutual recursion are supported. The compiler finds every function
that can reach itself through calls and compiles it once, as a real subroutine
(`subroutine` / `return` opcodes), instead of inlining it. Other functions
called from a recursive function are still inlined as usual.

```c
uint32_t fact(uint32_t n) { if (n < 2) return 1; return n * fact(n - 1); }
```

How it works:

- Parameters and the return value of a recursive function live at fixed,
  permanent RAM addresses. The caller copies arguments there before the call
  and reads the result afterwards.
- At a recursive call site the caller saves its own parameters and live locals
  on the VM value stack with `pushr` and restores them with `popr` after the
  call (caller-saves frame). The frame is saved in 4, 2, or 1 byte chunks, one
  value-stack entry per chunk, so a function with large local arrays or
  structs costs many entries per level.
- Each nested call also uses one return-address entry.

Limits to keep in mind:

- The default VM has a 16-entry return-address stack
  (`ULVM_CALLSTACK_SIZE`) and a 1024-entry value stack
  (`ULVM_VALUESTACK_SIZE`), both defined in `C-interpreter/unilang.h`.
  Overflow faults the VM. The return-address stack is shared with every other
  active subroutine call, including functions shared by `-Os`, so the usable
  recursion depth is below 16.
- Measure the frame cost before relying on deep recursion. For example, a
  chess search function that takes a 68-byte position struct as a local saves
  about 40 value-stack entries per level.
- Recursive functions cannot take or return 64-bit integers, like any other
  function.

## Size optimization (`-Os`)

Because every inlined call duplicates the callee's body, large programs can
exceed what a small target can hold. Passing `-Os` to `compiler.py` also
compiles a non-recursive function once, as a subroutine using the same
mechanism as recursion, when both conditions hold:

- it is called from at least two call sites, and
- its body has at least 60 syntax-tree nodes.

Smaller or single-use functions stay inlined. `-Os` can be given alongside the
`-O` levels, for example `python compiler.py game.c -Os`.

Tradeoffs:

- Shared functions get permanent parameter and return slots, so the peak RAM
  estimate can go up even though bytecode goes down.
- Each call costs extra copy and stack instructions at run time.
- Shared functions use return-address stack entries (16 by default), so deeply
  nested chains of shared functions are limited.

Measured example (a roughly 1,700-line chess program for the 64x64 display):

| Build | Bytecode | RAM estimate |
|---|---:|---:|
| `-O0` | 379,377 B | not measured |
| default (`-O1`/`-O2`) | 182,712 B | 1,143 B |
| `-Os` | 73,067 B | 1,663 B |
| `-Os` after replacing five hand-unrolled search functions with one recursive function | 50,896 B | 1,102 B |

### Reducing size by hand

- Try `-Os` first. It needs no source changes.
- Hand-unrolled copies of the same function (for example `search0` to
  `search4` to avoid recursion) are no longer needed. One recursive function
  is much smaller. Check the stack depth as described above.
- Without `-Os`, a function called from many sites is duplicated at each one.
  Calling it from fewer places, or restructuring to loop over cases, also
  helps.
- Encoding explains why bytecode grows quickly: most instructions that touch
  RAM or jump carry 4-byte absolute addresses, so `setreg`, `loadreg32` and
  `storereg32` are 6 bytes and a fused compare-and-jump with an immediate
  (`jcmpv`) is 11. See the [bytecode format](../assembly/bytecode-format.md).

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

- indirect function calls and general variadic functions;
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
