# TODO: compile examples/microcity with uniClang

Goal: `python compiler.py examples/microcity -o examples/microcity/microcity.ulc` compiles the whole
MicroCity project (Arduboy city builder, ~25 files, C++ style C) and runs on the unilang VMs.

Rules for every agent:
- DO NOT edit any file in `examples/microcity/` except `MicroCity.cpp` (the platform layer).
- All other changes go in `compiler.py`, `preprocessor.py`, or NEW files (e.g. `include/avr/pgmspace.h`).
- Mark a task done by changing `[ ]` to `[done]`. Add a short note under it if something non-obvious was decided.
- Before touching the compiler, snapshot baseline bytecode of existing examples (task 0) and re-check after each compiler task (no regressions).
- No ISA changes (opcodes in README section 1 are fixed). Emulate missing ops in the compiler.
- Compiler facts: single pass, pycparser (C only, not C++), memory-to-memory codegen using scratch regs r14/r15,
  functions are INLINED per call site (no recursion), only unsigned ints today, `addr_sizes` tracks byte width per address.
  Unsigned model, no structs, no multi-dim arrays, one source file. `main()` is required.
- Test env note: `main.py` (Python VM) is not at repo root. Use `C-interpreter/unilang.h` (headless build, dump framebuffer as ASCII) to run output.

## 0. Baseline
- [done] 0.1 Compile `examples/pong`, `examples/game`, `examples/test`, `pong/`, `helloworld/`, `roulette/` with current compiler, store outputs as `*.ulc.baseline` (outside git, e.g. /tmp) for regression diffing.
  Note: baseline outputs were generated under `/tmp/unilang-baseline` with the current compiler; additional optional example directories are treated as present-only checks.
- [done] 0.2 Write `tools/regress.py` (or shell script) that recompiles all the above and reports byte diffs / compile failures.

## 1. MicroCity.cpp (platform layer, the only game file allowed to change)
- [done] 1.1 Remove `Arduboy2.h`, `EEPROM.h`, `arduboy` object, `setup()`/`loop()`.
- [done] 1.2 `GetInput()`: use `getkey()`; VM bits Up=1 Down=2 Left=4 Right=8 C=16 V=32. Map to `INPUT_UP/DOWN/LEFT/RIGHT/A/B` (see Interface.h).
- [done] 1.3 `PutPixel(x,y,colour)` -> `setpixel(x,y,colour)`.
- [done] 1.4 `DrawBitmap(bmp,x,y,w,h)`: Arduboy format is column-major, 8 vertical pixels per byte, LSB on top; read with `romread` (bmp is a ROM pointer), draw white pixels only with `setpixel`.
- [done] 1.5 `GetPowerGrid()` returns a global `uint8_t PowerGrid[1024]` (replaces Arduboy screen buffer).
- [done] 1.6 FAKE save/load: `SaveCity()` copies `State` bytes into a global RAM buffer `SavedCity[sizeof(GameState)]` and sets `SavedValid=1`; `LoadCity()` returns false if not valid, else copies back and returns true. (RAM only, lost on exit. Comment that unilang has no EEPROM.)
- [done] 1.7 `main()`: `flag(0,4)` (mono 128x64) guarded by `isflagsupported`, `flag(1,1)`, `InitGame()`, then `while(1){ TickGame(); refreshscreen(); sleep(40); }`. Keep `UIState.state` start screen behaviour (InitGame in original also sets up; check original `setup()` order).

## 2. Compiler: project level
- [done] 2.1 CLI: accept multiple inputs or a directory (all `*.cpp` / `*.c`, sorted); `-o` default `<dir>/<dirname>.ulc`.
- [done] 2.2 Preprocess each file separately (own `#pragma once` / include guards state per file is fine, but `#pragma once` must be global per header path), concatenate results into one translation unit. Keep `filename:line` in errors.
- [done] 2.3 Allow `extern T x;` + later definition, repeated prototypes, and duplicate identical typedefs/enums from headers without errors.
- [done] 2.4 `main` may be defined in any file (MicroCity.cpp).
- [done] 2.5 Default include dir `include/` next to compiler.py already exists in `main()`; create it and ensure it is searched for `<...>` includes.

## 3. Compiler: C++ compatibility (pre-parse rewriting in compiler.py/preprocessor.py)
- [done] 3.1 Predefine `nullptr` as 0; treat `.cpp`/`.h` like C.
- [done] 3.2 C++ tag names as types: for `enum Name {..}` and `struct Name {..}` auto-emit `typedef enum Name Name;` (e.g. `BuildingType buildingType`).
- [done] 3.3 Check `inline` functions, `bool`, `true/false`, `static` globals, unnamed enums all parse; fix what does not.
- [done] 3.4 `#include` inside array initializers (TileData.h, Terrain*.inc.h) works; `#if _WIN32` with undefined macro evaluates to 0. Do NOT define `_WIN32` (Defines.h must take the `avr/pgmspace.h` branch).
- [done] 3.5 Make unused `USE_FIXED_MEMORY_FILL` branch (uses goto) never reach the parser (verify `#ifdef` handling).

## 4. Shim headers (new files)
- [done] 4.1 `include/avr/pgmspace.h`: `PROGMEM` empty, `PSTR(x)` = x, `pgm_read_byte(x)` = `romread((x),1)`, `pgm_read_word(x)` = `romread((x),2)`, `pgm_read_ptr(x)` = `romread((x),4)`, `strlen_P` implemented as a small loop function using `romread`.
- [done] 4.2 Confirm ROM pointers are plain ROM byte offsets, so `&arr[i]`, `&info->width`, `p++` all work as arithmetic with no dereference.

## 5. Compiler: signed integers (needed: `n >= 0` loops, `screenX >= 0`, negative scroll, `-val`)
- [done] 5.1 Track signedness per address (`addr_signed`) next to `addr_sizes`; int/int8_t/int16_t/int32_t/short/signed char are signed.
- [done] 5.2 Sign-extend on load for signed 8/16-bit (xor/sub trick with constants, or shift left/right) and truncate on store.
- [done] 5.3 Expression typing: integer promotion to 32-bit signed; result unsigned only if an operand is uint32/size_t-like (4-byte unsigned). Temps carry the flag.
- [done] 5.4 Signed `< > <= >=`: flip sign bit of both operands (xor 0x80000000) then use unsigned compare ops.
- [done] 5.5 Signed `/` and `%`: abs both, unsigned div/mod, fix sign (C truncation semantics). Signed `>>`: arithmetic shift emulation. Unary `-` and `~` fine.
- [done] 5.6 Casts between signed/unsigned and widths, compound assignments, `++/--` on signed.
- [done] 5.7 `printf %d` prints signed values correctly; `%u` unsigned.
- [done] 5.8 Update README (remove "signed treated as unsigned" caveat). Regression: all baselines still compile and unsigned-only programs produce identical output.

## 6. Compiler: structs and bitfields
- [done] 6.1 Parse/register `struct`/`typedef struct {..}` (named and anonymous): field offsets, sizes, no padding/alignment (packed), nested arrays as members.
- [done] 6.2 Bitfields GCC-style: a field never straddles its declared type unit (1 byte for uint8_t/bool). Read = load byte, shift, mask. Write = read-modify-write. Include in `sizeof`. (`Building` becomes 4 bytes.)
- [done] 6.3 Member access `.` and `->` as rvalue and lvalue, including compound assign and `++/--` (e.g. `building->onFire--`, `State.money -= cost`).
- [done] 6.4 Struct-typed globals, arrays of structs (`State.buildings[n].type`), pointers to structs, `&State.buildings[n]`, pointer comparison (`building != otherBuilding`), `sizeof(GameState)`.
- [done] 6.5 Pointer arithmetic and indexing scaled by struct size.
- [done] 6.6 Struct-typed parameters by pointer only (by-value struct params/returns may stay unsupported with a clear error).
- [done] 6.7 `const struct` ROM arrays with brace initializers (`BuildingMetaData[]`, nested braces, 2/1/1/1 byte fields) emitted into ROM with correct field widths; `&info->width` on a ROM pointer = base + field offset.

## 7. Compiler: arrays, ROM data, statics
- [done] 7.1 Multi-dimensional arrays (RAM and ROM): `font4x6[96][2]`, `&font4x6[index][1]`, row-major, dims from initializer where first dim omitted.
- [done] 7.2 ROM arrays whose initializers are addresses of other ROM symbols (`ToolbarStrings`, `MonthStrings`): needs link-time patching of ROM data (labels/relocations).
- [done] 7.3 Functions returning ROM pointers (`GetTerrainData`, `GetBuildingInfo`) and pointer locals/params of ROM arrays; `const char* const` arrays.
- [done] 7.4 Function-static locals (`static uint16_t randVal = 0xABC;` in `GetRand`): allocate ONCE (not per inlined copy), initialize once at program start.
- [done] 7.5 Global array/char-array initializers of any constant expression, `{0}` zero fill, `char buffer[MAX_DIGITS]` locals, `uint8_t*` cast from `&State` (`(uint8_t*)(&State)`), pointer increment loops.
- [done] 7.6 `else switch (...)`, `case` blocks with braces and local decls, `default:` before `case`, fallthrough stacking (`default: case 0:`). Check existing Switch codegen handles `default` not last.
- [done] 7.7 `uint8_t cursorX, cursorY;` locals passed as `&cursorX` to pointer params (already claimed supported; verify with struct/bitfield mix).
- [done] 7.8 Unused-parameter empty inline functions, forward calls to functions defined later or only prototyped in headers (`StartRandomFire`).

## 8. Build, run, verify
- [done] 8.1 `python compiler.py examples/microcity` compiles with zero errors.
- [done] 8.2 Report required memory (compiler already warns); document recommended `ULVM_MEM_SIZE`/`memsize` for MicroCity. If inlining blows memory/size, add optimization (reuse temps, share locals of non-overlapping inlined calls) and note it here.
- [done] 8.3 Headless run with `C-interpreter/unilang.h` (stub callbacks, ASCII framebuffer dump): verify start screen draws, press keys via scripted input, verify New City map and tiles render, UI text draws, a few frames simulate without hang.
- [done] 8.4 Performance sanity: frames per second on C VM; note if Python VM is unusable.
- [done] 8.5 Re-run `tools/regress.py`; all old examples unchanged or intentionally improved.
- [done] 8.6 Update README: new features (structs, bitfields, signed, multi-dim arrays, multi-file, C++ compat, shim include dir), add microcity to examples list, compile command.
- [done] 8.7 Add `examples/microcity/README.md` (3-5 lines: how to build and run, controls mapping, save/load is RAM-only fake).
