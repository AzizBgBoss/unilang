# 3D cube projection demo

Three animated, RGB332 wireframe cubes rotate around two axes on the 128x64
display. The demo uses 8-bit fixed-point math and perspective projection:
`screen = center + coordinate * focal_length / depth`. The `project_vertex`
function transforms each 3D cube vertex, then maps it to screen coordinates.
Press Enter to exit.

Compile from the repository root:

```sh
python compiler.py examples/cube3d/cube3d.c -o examples/cube3d/cube3d.ulc
```

Run it with the SDL C interpreter:

```sh
C-interpreter/main examples/cube3d/cube3d.ulc
```

The interpreter must be rebuilt if its source or VM opcodes have changed since
its executable was last compiled.
