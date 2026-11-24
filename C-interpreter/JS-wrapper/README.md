# JS-wrapper

Standalone `unilang.html` that runs unilang bytecode (ULC) in the browser. `../main.c` is compiled unmodified with Emscripten.

## One-time setup (Windows)

Needs git and Python installed.

```
git clone https://github.com/emscripten-core/emsdk.git C:\emsdk
cd C:\emsdk
emsdk install latest
emsdk activate latest --permanent
```

Close and reopen the terminal afterwards. If `emcc` is still not found, run `C:\emsdk\emsdk_env.bat` in that terminal, or set an `EMSDK` environment variable pointing to `C:\emsdk` (build.bat will use it).

## Build

Double click `build.bat` (or run `python build.py`). It produces `unilang.html`, a single file you can open from `file://` or copy to your phone.

## Use

- Load ULC: pick the bytecode file, then press Run.
- Load ULS: optional save file, used on the next Run.
- Download ULS: enabled once the program has a save file.
- On-screen buttons send the keys the VM reads: arrows, C, V, Backspace, Enter.
- The console shows stdout and stderr. Tick "debug" before Run to pass `-debug`.
- Networking (`--net`) is not wired up yet, so programs that need it will not connect.

## Notes

- Output printed without a trailing newline may only appear once the program exits.
- If the build fails on `-sUSE_SDL=2` in a newer emsdk, replace it with `--use-port=sdl2` in build.py.
