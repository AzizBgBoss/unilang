# MicroCity

## Current status

The source is an experimental multi-file, VM-compatible port of an Arduboy
city-builder. It uses ROM-backed tiles and keeps save data in RAM for the
duration of a run; the VM does not provide EEPROM persistence.

The current compiler fails while generating a struct-member lvalue in this
project, so the source does not compile yet. The checked-in
`microcity.ulc` is retained as a reference artifact and may not match the
current sources.

Controls in the game: Up/Down/Left/Right move the cursor; A and B select or
build.

See the root [README](../../README.md#quick-start) for compiler and VM setup.
