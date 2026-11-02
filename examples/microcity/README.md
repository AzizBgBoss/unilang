# MicroCity

## Current status

The source is an experimental multi-file, VM-compatible port of an Arduboy
city-builder. It uses ROM-backed tiles and the VM host's save/load callbacks
to persist the `GameState` structure across runs.

Compile the project from the repository root with the compatibility define:

```sh
python compiler.py examples/microcity -o examples/microcity/microcity.ulc -D _WIN32=1
C-interpreter/main examples/microcity/microcity.ulc
```

In the game, open the toolbar's Save/Load option. Choose Save to write the
current city to the host's `.uls` file, or Load to restore it. A saved city is
also loaded automatically at startup when one exists. On the SDL host, the
save file is next to the `.ulc` file; see the root README for its format.

Controls in the game: Up/Down/Left/Right move the cursor; A and B select or
build.

The `_WIN32` define selects this port's VM `PROGMEM` compatibility macros,
including ROM reads for bitmap, tile, font, and string data; it does not select
a different graphics mode. MicroCity uses graphics flag 0 value `6`
(monochrome 128x64), with keyboard flag 1 value `1`.
