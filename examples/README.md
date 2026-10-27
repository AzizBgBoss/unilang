# Examples

Compile and run the examples from the repository root. The C-interpreter
commands below assume its SDL2 host has been built as described in the root
[README](../README.md#quick-start).

| Example | What it demonstrates | Compile |
|---|---|---|
| [Celeste](celeste/README.md) | Scrolling platformer, movement, collision, and RGB332 graphics | `python compiler.py examples/celeste/celeste.c -o examples/celeste/celeste.ulc` |
| [3D cube projection](cube3d/README.md) | Fixed-point 3D rotation, perspective projection, and wireframe drawing | `python compiler.py examples/cube3d/cube3d.c -o examples/cube3d/cube3d.ulc` |
| [Flag 6 visual demo](flag6/README.md) | RGB332 colors, animated aurora, stars, mountains, and parallax | `python compiler.py examples/flag6/flag6.c -o examples/flag6/flag6.ulc` |
| [Floating-point values](float/README.md) | Signed integers, IEEE-754 `float`/`double`, and conversions | `python compiler.py examples/float/float.c -o examples/float/float.ulc` |
| Game | Graphics, controls, and game logic | `python compiler.py examples/game/game.c -o examples/game/game.ulc` |
| MicroCity | Experimental multi-file project and ROM-backed data; source currently hits a compiler limitation | Not currently compilable; see [MicroCity notes](microcity/README.md) |
| Pong | A compact graphics and keyboard example | `python compiler.py examples/pong/pong.c -o examples/pong/pong.ulc` |
| Test | Small compiler/runtime smoke test | `python compiler.py examples/test/test.c -o examples/test/test.ulc` |

Run a compiled program with the desktop VM, for example:

```sh
C-interpreter/main examples/cube3d/cube3d.ulc
```

Check that all buildable examples compile without overwriting their checked-in
bytecode. MicroCity is reported as a known skip until its compiler issue is
resolved.

```sh
python tools/regress.py
```
