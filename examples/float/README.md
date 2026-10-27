# Floating-point and signed-value example

`float.c` exercises IEEE-754 binary32 `float`, binary64 `double`, numeric
conversions, comparisons, arrays, function arguments/returns, signed narrow
integers, signed division/remainder, and signed `%d` output.

Compile and run from the repository root:

```sh
python compiler.py examples/float/float.c -o examples/float/float.ulc
C-interpreter/main examples/float/float.ulc
```

See the root [README](../../README.md#quick-start) for SDL host setup.

Expected output:

```text
signed: -2 -1 -3
signed-width: -5 -4
float: 3 3 6
compare: 1 1 1
array: 3 0
sources: 1 1 0 3
storage: 3 2
increment: 1 1 2
precision: 1 0
truncate: 127 255
ieee: 1 1 0
cast: 4294967289
```
