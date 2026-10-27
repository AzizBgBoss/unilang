# MicroCity

Build with `python compiler.py examples/microcity -o examples/microcity/microcity.ulc`.
Run under the VM with `python main.py -f examples/microcity/microcity.ulc`.
Controls: Up/Down/Left/Right move the cursor; A/B select/build actions; save/load is RAM-only fake state, not EEPROM-backed.
The project is the VM-compatible port of the Arduboy city-builder, using the same ROM-backed tiles and UI layout.
