# Examples

Compile and run the examples from the repository root. The C-interpreter
commands below assume its SDL2 host has been built as described in the root
[README](../README.md#quick-start).

For language references, see the
[C-like language guide](../docs/c-language/overview.md), the
[builtins reference](../docs/c-language/builtins.md), and
the [bytecode instruction reference](../docs/assembly/instruction-reference.md).

| Example | What it demonstrates | Compile |
|---|---|---|
| [Celeste](celeste/README.md) | Scrolling platformer, movement, collision, and RGB332 graphics | `python compiler.py examples/celeste/celeste.c -o examples/celeste/celeste.ulc` |
| [3D cube projection](cube3d/README.md) | Fixed-point 3D rotation, perspective projection, and wireframe drawing | `python compiler.py examples/cube3d/cube3d.c -o examples/cube3d/cube3d.ulc` |
| [Flag 0 value 9 visual demo](flag9/README.md) | RGB332 colors, animated aurora, stars, mountains, and parallax | `python compiler.py examples/flag9/flag9.c -o examples/flag9/flag9.ulc` |
| [Floating-point values](float/README.md) | Signed integers, IEEE-754 `float`/`double`, and conversions | `python compiler.py examples/float/float.c -o examples/float/float.ulc` |
| [Formatted printf](test/printf_format.c) | Integer flags, width/precision, floating-point formatting, and narrowing modifiers | `python compiler.py examples/test/printf_format.c -o printf_format.ulc` |
| [Audio demo](audio/audio.c) | Loads SFX and music records; demonstrates playback, status, beep, volume, pause, and resume | `python compiler.py examples/audio/audio.c -o examples/audio/audio.ulc` |
| Game | Graphics, controls, and game logic | `python compiler.py examples/game/game.c -o examples/game/game.ulc` |
| [MicroCity](microcity/README.md) | City-builder with persistent save/load through the host | `python compiler.py examples/microcity -o examples/microcity/microcity.ulc -D _WIN32=1` |
| [Off-screen drawing](offscreen/offscreen.c) | Exercises clipping for rectangles, lines, and glyphs beyond screen edges | `python compiler.py examples/offscreen/offscreen.c -o examples/offscreen/offscreen.ulc` |
| Pong | A compact graphics and keyboard example | `python compiler.py examples/pong/pong.c -o examples/pong/pong.ulc` |
| [Save counter](save_counter/save_counter.c) | Reads and increments persistent SDL-host save data | `python compiler.py examples/save_counter/save_counter.c -o examples/save_counter/save_counter.ulc` |
| Compiler/VM regressions | Void functions, formatted `printf`, memory operations, math, `goto`, unions, value stack, computed switch dispatch, and clipped graphics | `python tools/regress.py` |

Run a compiled program with the desktop VM, for example:

```sh
C-interpreter/main examples/cube3d/cube3d.ulc
```

Check that all buildable examples compile without overwriting their checked-in
bytecode. The regression command includes the MicroCity project and verifies
that unsupported general variadic functions produce a clear compiler error.

```sh
python tools/regress.py
```
