# TODO

## Agreed implementation

- [x] **Clip drawing at screen edges.** Reviewed `ulvm_blit`, `drawchar`, `drawstring`, `fillrect`, and `drawline`. Drawing outside the framebuffer preserves visible pixels and avoids out-of-bounds callbacks. Tests cover each edge plus sprite source/destination clipping.
- [x] **Add VM save/load operations.** Added bytecode operations and callbacks for caller-selected RAM blocks. Address/length bounds are checked, and operations return failure when callbacks or host I/O fail.
- [x] **Define audio flag 2 behavior.** Audio flag values are `0` (off), `1` (`beep`), and `2` (full engine plus `beep`). Host support checks are wired, and audio data lives in host storage rather than VM memory.
- [x] **Implement `uniaudio.h`.** Added a four-channel synthesizer, 64 SFX slots of 32 notes, and 64 four-channel music patterns. It reads PICO-8 binary SFX/music records and supports pitch, waveforms, volume, effects, speed/loops, music markers, channel selection, stop, pause/resume, master volume, status, and `beep`. Custom waveforms are approximated as triangle waves and SFX filter data is ignored.
- [x] **Add audio and persistence to the VM/compiler.** Added callbacks and bytecode for `beep`, audio commands/data loading, and `save`/`load`; added compiler builtins and optimizer operand handling for the new instructions.
- [x] **Connect the SDL host.** Audio is mixed in an SDL audio callback. Persistent `.uls` files contain a 4-byte size followed by saved bytes; file and device failures are reported.
- [x] **Connect the headless host.** Added in-process save/load and `-wav` rendering for synthesized output without SDL.
- [x] **Add examples and explain usage.** Added audio, save-counter, and off-screen drawing examples. README documentation covers flags, builtins, PICO-8 data records, save files, and build/run commands.
- [x] **Verify the complete behavior.** VM tests cover clipped drawing, save/load round trips and failures, audio status/channel selection, music markers, pause/resume/stop, and beep timing. The WAV output was validated at 22050 Hz and measured at 441 Hz for the 440 Hz beep. Hosts build successfully and examples compile/run.

## Graphics flag renumbering follow-up

- [ ] **Restore or recreate `examples/unipoke`.** Its source directory was missing during the earlier pass; if restored, update its graphics flag from `7` to `11` and rebuild its bytecode.
- [ ] **Check MicroCity flag usage.** Search its source for graphics flag values and update any old values to the new numbering; rebuild it if it compiles.
- [ ] **Resolve the `flag6` naming mismatch.** The example now uses graphics flag `9` but retains the `flag6` folder and filenames to avoid breaking references such as `regress.py`. Decide whether to rename it and update every reference if so.
- [ ] **Complete README opcode documentation.** Add the missing opcode-table entries for `fillrect`, `drawline`, `blit`, and `setpalette`, including operands and behavior.
- [ ] **Rebuild checked-in outputs after renumbering.** Rebuild `C-interpreter/main` and recompile every example `.ulc` from its current source so the binaries contain the updated graphics flags.

## Earlier general-purpose wishlist

### Memory and data

- [ ] **Add `memcpy` and `memset` opcodes.** Implement VM instructions that copy a requested byte range or fill it with a byte value, avoiding per-byte bytecode loops. Define operand encoding, validate source/destination ranges before access, document the instructions, and test zero-length, boundary, and invalid ranges.
- [ ] **Add a value stack with `push`/`pop` operations, if needed.** First check whether the existing call stack can serve this purpose; if not, add a separate bounded stack for values. Define its capacity and underflow/overflow behavior, then use it to reduce register spills and support recursive calls without corrupting return addresses. Test nested calls and stack exhaustion.
- [ ] **Add indirect jump and call operations.** Allow bytecode to jump to or call a target held in a register, with the same return behavior as direct calls. Validate target addresses and fault clearly on invalid targets. Update the compiler so `switch` statements and state machines can use computed dispatch instead of a chain of conditional branches.

### Math

- [ ] **Add single-operation `min`, `max`, and `abs`.** Define signedness and behavior for the VM's supported integer widths, add compiler mappings, document them, and test equal values, negative values, and integer limits.
- [ ] **Finish verifying arithmetic right shift (`sar`).** The VM opcode is already present; check that the compiler exposes it with signed semantics, then document and test it rather than adding a duplicate VM opcode.
- [ ] **Add 32×32-bit multiplication with a 64-bit result.** Specify how the high and low words are returned, implement the operation in the VM and compiler, document it, and test signedness and overflow cases used by fixed-point math.
- [ ] **Check `sin`, `cos`, `sqrt`, and `atan2` support.** Inspect existing `OP_FLOATOP` operations first; add compiler/VM support only for missing functions or define lookup-table operations if float operations are not the right fit. Document units, input/output format, and accuracy, and test representative values and edge cases.

### Graphics

- [ ] **Add host-side `fillrect` and `blit` fast paths.** Let hosts draw rectangles and sprite regions in bulk rather than invoking the per-pixel callback for every pixel. Preserve clipping, color formats, and existing drawing results; retain a compatible fallback for hosts without the callbacks and compare output against it.
- [ ] **Add `fillscreen(color)`, `fillcircle`, and `drawrect`.** Define coordinates, dimensions, edge inclusion, and clipping behavior; wire each operation through the VM, compiler, host interface, and README, and add tests for degenerate and partly off-screen shapes.
- [x] **Palette graphics and `setpalette` are already implemented for the new palette modes.** Do not add a duplicate palette feature.

### System

- [ ] **Implement basic `beep(freq, ms)` in audio mode 1.** Generate a tone at the requested frequency for the requested duration on the host, define behavior for zero or out-of-range values, and test its pitch and duration. The full audio-engine work above also makes `beep` available in mode 2.
- [ ] **Implement persistent `save` and `load` for a small data block.** This overlaps the agreed save/load work above; use that implementation and its documented file format rather than creating a second storage mechanism.
- [ ] **Expose `srand(seed)` and add a timer/tick source.** Check the existing VM random-seed helper and expose it as a compiler builtin if missing. Add a host-provided timer or tick facility so programs can measure/frame against elapsed time without relying on `sleep`; document timing resolution and test deterministic seeding separately from elapsed-time behavior.
