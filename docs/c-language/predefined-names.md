# Predefined constants and macros

[Index](../README.md) · [Builtins](builtins.md) ·
[Preprocessor](preprocessor.md)

Programs can use these names without declaring or defining them. They come in
two kinds:

- **Compiler constants** (`KEY_*`, `FLAG_*`, `AUDIO_*`, `GFX_*`, `COLOR_*`, and
  the `NET_*` names from [builtins](builtins.md)) are resolved by the compiler.
  They work anywhere a compile-time constant is accepted, including the
  arguments of `flag()` and `isflagsupported()`. They are not preprocessor
  macros, so `#ifdef KEY_A` and `#if KEY_A` do not see them.
- **Predefined macros** (`RGB332`, `RGB555`, `BIT`, `MIN`, `MAX`, `ABS`, and the
  `INT*_MAX` and `INT*_MIN` limits) are defined by the preprocessor, so they
  also work in `#if` expressions.

A program can still define its own name. A `#define` replaces a predefined
macro, and an `enum` constant replaces a compiler constant of the same name, so
older programs that declare their own `KEY_*` or `RGB555` keep compiling. Note
that a local `RGB332` or `RGB555` with different argument ranges silently wins
over the built-in one.

## Keyboard bits

Values returned by `getkey()` after `flag(FLAG_KEYBOARD, 1)`. Test a key with
`getkey() & KEY_START`, and combine masks with `|`.

| Name | Value | Bit |
|---|---:|---:|
| `KEY_UP` | 1 | `1 << 0` |
| `KEY_DOWN` | 2 | `1 << 1` |
| `KEY_LEFT` | 4 | `1 << 2` |
| `KEY_RIGHT` | 8 | `1 << 3` |
| `KEY_A` | 16 | `1 << 4` |
| `KEY_B` | 32 | `1 << 5` |
| `KEY_SELECT` | 64 | `1 << 6` |
| `KEY_START` | 128 | `1 << 7` |
| `KEY_DPAD` | 15 | all four direction bits |
| `KEY_ANY` | 255 | every button |

`KEY_DPAD` and `KEY_ANY` are masks for "any direction" and "any button", so
`getkey() & KEY_ANY` is nonzero whenever something is held.

```c
uint32_t keys = getkey();
if (keys & KEY_LEFT)  x--;
if (keys & (KEY_A | KEY_START)) confirm();
```

## Flag indices

Pass these as the first argument of `flag()` and `isflagsupported()`.

| Name | Value | Meaning |
|---|---:|---|
| `FLAG_GRAPHICS` | 0 | Graphics mode |
| `FLAG_KEYBOARD` | 1 | NES-style keyboard |
| `FLAG_AUDIO` | 2 | Audio engine |
| `FLAG_NETWORK` | 15 | Networking capability (query only) |

## Audio modes

| Name | Value | Mode |
|---|---:|---|
| `AUDIO_OFF` | 0 | Audio disabled |
| `AUDIO_BEEP` | 1 | `beep` tone only |
| `AUDIO_ENGINE` | 2 | Four-channel SFX/music engine and `beep` |

## Graphics modes

Values for `flag(FLAG_GRAPHICS, mode)`. See
[flag 0](builtins.md#flag-0-graphics) for the details of each mode.

| Name | Value | Mode |
|---|---:|---|
| `GFX_OFF` | 0 | Graphics disabled |
| `GFX_MONO_64X64` | 1 | 1-bit monochrome, 64x64 |
| `GFX_GRAY_64X64` | 2 | 2-bit grayscale, 64x64 |
| `GFX_PAL2_64X64` | 3 | 2-bit palette, 64x64 |
| `GFX_RGB332_64X64` | 4 | 8-bit RGB332, 64x64 |
| `GFX_PAL8_64X64` | 5 | 8-bit palette, 64x64 |
| `GFX_MONO_128X64` | 6 | 1-bit monochrome, 128x64 |
| `GFX_GRAY_128X64` | 7 | 2-bit grayscale, 128x64 |
| `GFX_PAL2_128X64` | 8 | 2-bit palette, 128x64 |
| `GFX_RGB332_128X64` | 9 | 8-bit RGB332, 128x64 |
| `GFX_PAL8_128X64` | 10 | 8-bit palette, 128x64 |
| `GFX_RGB555_128X128` | 11 | 16-bit RGB555, 128x128 |
| `GFX_RGB555_256X128` | 12 | 16-bit RGB555, 256x128 |

```c
if (!isflagsupported(FLAG_GRAPHICS, GFX_RGB332_128X64) ||
    !isflagsupported(FLAG_KEYBOARD, 1)) {
    printf("Needs 128x64 color graphics and a keyboard.\n");
    exit();
}
flag(FLAG_GRAPHICS, GFX_RGB332_128X64);
flag(FLAG_KEYBOARD, 1);
```

## Color macros

The arguments are 8-bit channels (0 to 255). When all three are constants the
result folds to a constant, so the macros are safe in array initializers. Extra
low bits are truncated, not rounded.

| Macro | Result |
|---|---|
| `RGB332(r, g, b)` | Packed byte `RRRGGGBB`, for the RGB332 modes |
| `RGB555(r, g, b)` | 15-bit value `0RRRRRGGGGGBBBBB`, for `setpalette()` and the RGB555 modes |

```c
setpalette(0, RGB555(255, 0, 0));      // palette modes
fillscreen(RGB332(255, 128, 0));       // RGB332 modes
```

## Color names

`COLOR_*` are RGB332 byte values. They are correct only in the RGB332 modes
(`GFX_RGB332_64X64` and `GFX_RGB332_128X64`).

| Name | Value | Name | Value |
|---|---:|---|---:|
| `COLOR_BLACK` | 0x00 | `COLOR_YELLOW` | 0xFC |
| `COLOR_WHITE` | 0xFF | `COLOR_CYAN` | 0x1F |
| `COLOR_GRAY` | 0x92 | `COLOR_MAGENTA` | 0xE3 |
| `COLOR_RED` | 0xE0 | `COLOR_ORANGE` | 0xF0 |
| `COLOR_GREEN` | 0x1C | | |
| `COLOR_BLUE` | 0x03 | | |

In the other modes the same numbers mean something else:

- Monochrome modes expect `0` or `1`, so `COLOR_WHITE` (255) only works where
  the host treats any nonzero value as on.
- Palette modes take palette indices, so color names have no meaning there.
- RGB555 modes (11 and 12) take a 15-bit color, so use the `COLOR555_*` names
  below or `RGB555(r, g, b)`.

`COLOR555_*` are the same ten colors as 15-bit RGB555 values, for the RGB555
modes and for `setpalette()`.

| Name | Value | Name | Value |
|---|---:|---|---:|
| `COLOR555_BLACK` | 0x0000 | `COLOR555_YELLOW` | 0x7FE0 |
| `COLOR555_WHITE` | 0x7FFF | `COLOR555_CYAN` | 0x03FF |
| `COLOR555_GRAY` | 0x4210 | `COLOR555_MAGENTA` | 0x7C1F |
| `COLOR555_RED` | 0x7C00 | `COLOR555_ORANGE` | 0x7E00 |
| `COLOR555_GREEN` | 0x03E0 | | |
| `COLOR555_BLUE` | 0x001F | | |

## Other macros

| Name | Value |
|---|---|
| `BIT(n)` | `1 << n` |
| `MIN(a, b)` | the smaller of `a` and `b` |
| `MAX(a, b)` | the larger of `a` and `b` |
| `ABS(x)` | `x` for non-negative values, `-x` when bit 31 is set |
| `INT8_MAX`, `INT16_MAX`, `INT32_MAX` | 127, 32767, 2147483647 |
| `INT8_MIN`, `INT16_MIN`, `INT32_MIN` | -128, -32768, -2147483648 |

`MIN`, `MAX` and `ABS` are plain macros, so each argument is evaluated twice:
do not pass an expression with side effects such as `MAX(i++, 3)`.

Values are 32-bit and comparisons follow the operand types:

- With `int32_t` variables, `MIN` and `MAX` compare as signed numbers.
- When both operands are constants, a negative one is treated as an unsigned
  value, so `MAX(-3, 5)` is `-3`. Put negative operands in a signed variable
  first.
- `ABS` tests bit 31, so it works on negative constants and signed variables
  alike. Unsigned values of 2147483648 or more count as negative.
- Likewise `INT8_MIN`, `INT16_MIN` and `INT32_MIN` are 0xFFFFFF80, 0xFFFF8000
  and 0x80000000 as raw values. Compare them by equality or store them in an
  `int32_t` before ordering them.

`NULL`, `RAND_MAX`, `CHAR_BIT`, `UINT8_MAX`, `UINT16_MAX` and `UINT32_MAX` are
also predefined; see the [preprocessor page](preprocessor.md).

## Compile-time arguments to isflagsupported

`isflagsupported(flag, value)` accepts any compile-time constant expression for
both arguments, so the names on this page work in it exactly as they do in
`flag()`. Arguments that are not constants are still rejected.
