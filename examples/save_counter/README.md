# Save counter

Compile and run the sample from the repository root:

```sh
python compiler.py examples/save_counter/save_counter.c -o examples/save_counter/save_counter.ulc
C-interpreter/main examples/save_counter/save_counter.ulc
```

Each run loads a 32-bit counter, increments it, and saves it. The SDL host
stores data in `examples/save_counter/save_counter.uls` beside the bytecode.
The host save format and `save`/`load` return values are documented in the root
README. The headless host uses a temporary in-process save block, so persistent
data across separate runs requires the SDL host.
