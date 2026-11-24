# Compiler builtins

[Index](../README.md) · [C overview](overview.md) ·
[Types and memory](types-and-memory.md) ·
[VM host integration](../../docs/vm/host-integration.md)

The names on this page are compiler-recognized calls, not necessarily
functions from a linked C library. Most hardware features require a compatible
host callback. Functions described as statement-only cannot be used as an
expression.

Named constants for flags, graphics modes, keys and colors, plus the `RGB332`
and `RGB555` macros, are listed in
[predefined constants and macros](predefined-names.md). The example programs use
them, for instance `flag(FLAG_GRAPHICS, GFX_RGB332_128X64)`.

## Flags and platform modes

`flag(index, value)` sets one of the VM's packed 4-bit flags. Both arguments
must be compile-time constants.
`isflagsupported(index, value)` returns whether the host supports a requested
mode; both arguments must be compile-time constants.

### Networking

Networking builtins lower directly to the optional VM opcodes. Arguments are
integer values unless stated otherwise. Handles are signed `int`-like values;
`-1` indicates failure. `netopen(kind, local_port, remote_port, host_addr,
flags)` takes a VM RAM address for a NUL-terminated host string (use a
`char *` pointing into a RAM array). Kinds are `NET_TCP=1`, `NET_UDP=2`, and
`NET_HTTP=3`; zero local port requests a host-selected port.

`netstatus(handle)`, `netsend(handle, buffer_addr, length)`,
`netrecv(handle, buffer_addr, capacity)`, `netaccept(listener)`,
`netclose(handle)`, `netoption(handle, option, value)`,
`netavail(handle)`, `netlocalip(buffer_addr)`,
`netpeer(handle, host_buffer_addr, port_addr)`, and `netlasterror()` are also
available. `netrecv` never waits and returns zero when no bytes are ready.
`netpeer` writes a NUL-terminated host string and a big-endian 16-bit port.
`netlocalport(handle)` reports the actual local port, including a port chosen
by the OS when zero was passed to `netopen`.
`netlocalip`/`netpeer` return 1 on success; close/option return 1 on success.
Open returns a handle. Send and receive return byte counts. Failure is
`0xffffffff` (C `-1`) when networking is unavailable, except `netlasterror`
which returns `UNSUPPORTED` (6). With networking installed, invalid handles
return `0xffffffff` for operations, `CLOSED` for `netstatus`, and zero for
`netavail`. `netlasterror` uses NONE=0, INVALID_HANDLE=1,
BAD_KIND=2, NO_BUFFER=3, TIMEOUT=4, IO=5, UNSUPPORTED=6, LIMIT=7.

The capability flag is 15: `isflagsupported(15, 1)` reports whether the host
installed networking with `ULVM_set_net`; `isflagsupported(15, 0)` is always
true. Limits are 8 handles and 2048 bytes per buffer/transfer. Option IDs are
timeout-ms=1, TCP_NODELAY=2, frame-size=3, HTTP headers=4, and UDP
broadcast=5. `NET_OPEN_LISTEN=1` opens a TCP listener and
`NET_OPEN_BROADCAST=2` enables UDP broadcast at open.

`nethttp(handle, method, path_addr, headers_addr, body_addr, body_length)`
starts an HTTP request on an HTTP handle. `NET_HTTP_GET=1` and
`NET_HTTP_POST=2` select the method. Path and headers are NUL-terminated RAM
strings; the body is a RAM byte range (pass `NULL, 0` for no body).
`nethttpstatus(handle)` returns zero while pending and then the HTTP status
code. Read response bytes using `netavail` and `netrecv`.

LAN discovery is controlled by `netdiscovery(flags, game_port, game_id,
name_addr)`. `NET_DISCOVERY_LISTEN=1` listens and `NET_DISCOVERY_ANNOUNCE=2`
also broadcasts an announcement; pass their bitwise OR to do both, or zero to
stop. Names are NUL-terminated RAM strings, up to 31 bytes. The fixed UDP
discovery port is `NET_DISCOVERY_PORT=45678`. `netdiscoverycount()` returns
the nonexpired session count. `netdiscoveryget(index, name_addr, host_addr,
port_addr, game_id_addr)` writes a session's name, numeric address, big-endian
port, and game ID to RAM; it returns 1 on success.

The SDL desktop host supports TCP, UDP, HTTP and TCP framing over numeric
IPv4/IPv6 addresses; DNS names and URL strings are rejected. Its HTTP
implementation uses plain HTTP over TCP; HTTPS/TLS is unsupported. HTTP status
is the parsed three-digit code, zero while pending, and zero if no status line
was received. Response headers are buffered separately; response bodies are
buffered up to 2048 bytes. Requests use HTTP/1.0 to avoid chunk-framed bodies.
The in-memory backend supports UDP only. Desktop timeout options
are checked during polling. See [host networking](../vm/host-integration.md#optional-networking)
for details and the [network examples](../../examples/network/README.md).

### Flag 0: graphics

| Value | Mode |
|---:|---|
| 0 | Disable graphics |
| 1 | 1-bit monochrome, 64x64 |
| 2 | 2-bit grayscale, 64x64, four shades |
| 3 | 2-bit indexed palette, 64x64, RGB555 palette |
| 4 | 8-bit RGB332, 64x64 |
| 5 | 8-bit indexed palette, 64x64, RGB555 palette |
| 6 | 1-bit monochrome, 128x64 |
| 7 | 2-bit grayscale, 128x64, four shades |
| 8 | 2-bit indexed palette, 128x64, RGB555 palette |
| 9 | 8-bit RGB332, 128x64 |
| 10 | 8-bit indexed palette, 128x64, RGB555 palette |
| 11 | 16-bit RGB555, 128x128 |
| 12 | 16-bit RGB555, 256x128 |

Use the `setpixel`, `fillrect`, `fillscreen`, `fillcircle`, `drawrect`,
`drawline`, `blit`, `tilemap`, `drawchar`, `drawstring`, `drawnum`, and `getpixel`
operations for drawing. Pixels and shapes are clipped at framebuffer edges.
The host may optionally provide bulk `fillrect`/sprite-row callbacks; the VM
has a per-pixel fallback.

`setpalette(index, rgb555)` is statement-only and applies to palette modes
3, 5, 8, and 10. The argument is a 15-bit RGB555 color. Palette modes store
pixel indices, not direct RGB values. The host initializes a default grayscale
or expanded RGB332 palette when switching into palette mode.

`refreshscreen()` presents the frame and clears the framebuffer for the next
frame. Draw first, then call it once per frame.

### Flag 1: keyboard

Set `flag(1, 1)` to request the NES-style 8-bit keyboard. `getkey()` returns a
bitmask:

| Bit value | Key |
|---:|---|
| 1 | Up |
| 2 | Down |
| 4 | Left |
| 8 | Right |
| 16 | A |
| 32 | B |
| 64 | Select |
| 128 | Start |

Keyboard input is host-dependent and intended to be used with graphics
enabled. The SDL desktop host maps A, B, Select and Start to the C, V,
Backspace and Enter keys.

### Flag 2: audio

| Value | Mode |
|---:|---|
| 0 | Audio disabled |
| 1 | `beep` tone only |
| 2 | Four-channel SFX/music engine and `beep` |

Audio playback is asynchronous and host-owned. A host without audio callbacks
does not play sound.

## Graphics and display

- `setpixel(x, y, color)` — plot one pixel; statement-only.
- `getpixel(x, y)` — return the pixel value; host must provide readback.
- `fillrect(x, y, width, height, color)` — fill a clipped half-open rectangle
  `[x, x+width) x [y, y+height)`. Non-positive dimensions draw nothing.
- `fillscreen(color)` — fill the active framebuffer.
- `fillcircle(x, y, radius, color)` — fill integer pixel centers within the
  radius. Negative radii draw nothing.
- `drawrect(x, y, width, height, color)` — outline with inclusive right and
  bottom edges at `x+width-1` and `y+height-1`; non-positive sizes draw
  nothing.
- `drawchar(character, x, y[, color])` — draw one glyph from the VM's built-in
  4x6 font.
- `drawstring(text, x, y[, color])` — draw a string; supported input is a
  literal or a RAM `char[]`. The VM handles line wrapping.
- `drawnum(number, x, y[, color])`: draw an unsigned 64-bit decimal number with
  `drawchar`, one digit every 6 pixels, with no padding or sign. This is not a
  VM operation: when a program calls `drawnum` without defining it, the
  compiler appends a small C helper that is inlined at each call site. A
  user-defined `drawnum` takes precedence. Statement-only.
- `setpalette(index, rgb555)` — update one palette entry (see flag 0).
- `refreshscreen()` — present then clear.

The optional color argument to `drawchar`/`drawstring`/`drawnum` defaults to 1. In
8-bit RGB332 graphics mode, color is a packed RGB332 byte. In palette modes,
pixel colors are indices; other modes use the selected mode's pixel
representation. Coordinates outside the framebuffer are clipped rather than
wrapped.

`blit` copies a rectangle of pixels from an image (a "sprite sheet") onto the
screen. It is the fast way to draw sprites, tiles and icons.

```c
blit(x, y, width, height, source, source_x, source_y, stride,
     flags, palette, transparent);
```

| Argument | What it means |
|---|---|
| `x`, `y` | Where on the screen the top-left corner of the sprite goes. May be negative or off-screen; the part outside is clipped. |
| `width`, `height` | Size of the rectangle to draw, in pixels. |
| `source` | The image data: a `const` array (stored in ROM) or a RAM array (add `BLIT_RAM` to `flags`). |
| `source_x`, `source_y` | Top-left corner of the rectangle *inside the image*. Use `0, 0` for a standalone sprite; use other values to pick one sprite out of a sheet. |
| `stride` | Width of the **whole image** in pixels, not of the sprite. The VM needs it to find the start of each row. |
| `flags` | Options, combined with `\|`. See the table below. Use `0` for plain 8-bit pixels from ROM. |
| `palette` | A RAM array of 256 bytes that converts source values to screen colors. Only used with `BLIT_PALETTE`; otherwise pass `0`. |
| `transparent` | A source value that is not drawn. Only used with `BLIT_TRANSPARENT`; otherwise pass `0`. |

Flags (constants are predefined, see [predefined names](predefined-names.md)):

| Constant | Value | Effect |
|---|---:|---|
| `BLIT_FLIP_X` | 1 | Mirror left to right. |
| `BLIT_FLIP_Y` | 2 | Mirror top to bottom. |
| `BLIT_4BPP` | 4 | Image has 4 bits per pixel: two pixels per byte, the even pixel in the low nibble. |
| `BLIT_PALETTE` | 8 | Convert every pixel through the `palette` array before drawing. |
| `BLIT_TRANSPARENT` | 16 | Skip pixels whose source value equals `transparent`. |
| `BLIT_RAM` | 32 | `source` is a RAM array instead of ROM. |
| `BLIT_1BPP` | 64 | Image has 1 bit per pixel: eight pixels per byte, **leftmost pixel in the highest bit**. Each row starts on a new byte, so a row takes `(stride + 7) / 8` bytes. This is the same layout as Adafruit GFX `drawBitmap`. |
| `BLIT_2BPP` | 128 | Image has 2 bits per pixel (values 0 to 3): four pixels per byte, **leftmost pixel in the highest bits**. Each row starts on a new byte, so a row takes `(stride + 3) / 4` bytes. |

Without `BLIT_4BPP`, `BLIT_2BPP` or `BLIT_1BPP`, each pixel is one byte. If
several bit depths are combined, `BLIT_1BPP` wins over `BLIT_2BPP`, which wins
over `BLIT_4BPP`.

In 1bpp mode a pixel value is `0` or `1`, and that value is what gets drawn.
That is the right color for monochrome graphics modes. In color modes, either
map it with `BLIT_PALETTE` (put the color for `0` at `palette[0]` and the color
for `1` at `palette[1]`) or use it as an index in a palette mode.

Example: an 8x8 monochrome sprite where the `0` pixels are see-through, so
whatever is already on screen shows behind it.

```c
const uint8_t pawn[8] = {
    0b00011000,
    0b00100100,
    0b00100100,
    0b00011000,
    0b00111100,
    0b01000010,
    0b01111110,
    0b00000000
};

blit(10, 20, 8, 8, pawn, 0, 0, 8, BLIT_1BPP | BLIT_TRANSPARENT, 0, 0);
```

Example: draw the second 8x8 sprite of a sheet that is 16 pixels wide (two
sprites side by side). `stride` is 16 because that is the sheet width, and
`source_x` is 8 to skip the first sprite.

```c
blit(0, 0, 8, 8, sheet, 8, 0, 16, BLIT_1BPP, 0, 0);
```

Rows outside the image, or a `width`, `height` or `stride` that is zero or
negative, draw nothing. Source and destination rectangles are both clipped. The
exact VM descriptor layout is in the
[bytecode format page](../assembly/bytecode-format.md#blit-descriptor).
`blit` cannot XOR with the screen; to invert pixels, read them with `getpixel`
and write them with `setpixel`.

### tilemap

`tilemap` draws a whole scrolling map of tiles in one call. The VM does the
loop, so it is much cheaper than calling `blit` once per tile.

```c
tilemap(map, map_w, map_h, sheet, stride, scroll_x, scroll_y,
        tile_w, tile_h, flags, palette, transparent);
```

| Argument | What it means |
|---|---|
| `map` | One byte per tile (the tile number), row-major, `map_w * map_h` bytes. A `const` array (ROM) or a RAM array (add `TILEMAP_MAP_RAM` to `flags`). |
| `map_w`, `map_h` | Map size in tiles. |
| `sheet` | The tile sheet image: a `const` array (ROM) or a RAM array (add `BLIT_RAM`). |
| `stride` | Width of the whole sheet in pixels. |
| `scroll_x`, `scroll_y` | The map pixel shown at the top-left of the screen. May be negative; the map does not wrap, so uncovered areas keep what was already drawn. |
| `tile_w`, `tile_h` | Size of one tile on screen, in pixels. |
| `flags` | The `blit` flags (`BLIT_1BPP`, `BLIT_2BPP`, `BLIT_4BPP`, `BLIT_PALETTE`, `BLIT_TRANSPARENT`, `BLIT_RAM`, flips), applied to every tile, plus `TILEMAP_MAP_RAM` (256). |
| `palette`, `transparent` | As in `blit`. |

**Cell size.** Every tile occupies a cell in the sheet whose width and height
are rounded up to a multiple of 8. A 7x5 tile uses an 8x8 cell, a 14x14 tile
uses a 16x16 cell. Only the top-left `tile_w` x `tile_h` pixels of a cell are
drawn, the rest is padding. Tile number `n` is in column `n % cols` and row
`n / cols` of the cells, where `cols = stride / cell_width`, so `stride` should
be a multiple of the cell width.
The flip flags mirror each tile on its own, not the whole map.

Only tiles that touch the screen are drawn. Use `BLIT_TRANSPARENT` to let
what is already on the screen show through the transparent pixels.

```c
const uint8_t level[6] = {0, 1, 1, 1, 0, 1};   // 3x2 tiles
// 7x5 tiles from a 1bpp sheet that is 16 pixels wide (two 8x8 cells)
tilemap(level, 3, 2, tiles, 16, camera_x, camera_y, 7, 5,
        BLIT_1BPP | BLIT_TRANSPARENT, 0, 0);
```

The VM descriptor layout is in the
[bytecode format page](../assembly/bytecode-format.md#tilemap-descriptor).

## Memory and stack

- `memcpy(dst, src, length)` — copy bytes to RAM and return `dst`. `src` may be
  a RAM pointer or constant ROM array/string. VM RAM ranges are validated
  before writes; overlapping RAM copies behave like `memmove`.
- `memset(dst, value, length)` — fill RAM bytes and return `dst`. Destination
  range is validated before writes.
- `push(value)` — push a 32-bit value onto the separate 64-entry value stack.
  Statement-only.
- `pop()` — pop and return the most recent 32-bit value. Overflow or underflow
  faults the VM. This is not a C call-frame stack.
- `romread(position, size)` — read a byte, big-endian 16-/32-bit word, or
  64-bit value from bytecode ROM; `size` must be a compile-time constant `1`,
  `2`, `4`, or `8`.

The [VM host reference](../vm/host-integration.md) covers RAM sizing and the
separate return and value stacks.

## Numeric helpers

- `min(a, b)` / `max(a, b)` — integer min/max; signedness follows operand
  types.
- `abs(value)` — 32-bit two's-complement absolute value. `abs(INT32_MIN)`
  remains `INT32_MIN` because it cannot be represented as a positive signed
  32-bit value.
- `mul64(a, b, &high, &low)` — multiply two 32-bit values and write the high
  and low 32-bit words to the output pointers. Signedness is inferred from
  operand types.
- `sin(x)`, `cos(x)`, `sqrt(x)`, `atan2(y, x)` — host C math functions through
  floating-point VM operations. Results are binary64; angles are radians.

## Time, randomness, input, and persistence

- `rand()` returns a value in `0..32767`. The extended `rand(min, max)` returns
  a value in `[min, max)`. If `max <= min` at runtime, the VM's random opcode
  does nothing; the compiler's current lowering leaves the expression result
  equal to `min`. Avoid that range.
- `srand(seed)` seeds the VM's deterministic pseudo-random generator.
- `gettime()` returns host elapsed milliseconds as a `uint64_t` in one value;
  epoch and resolution depend on the host. This is polling, not an interrupt or
  guaranteed frame clock.
- `sleep(milliseconds)` — sleep for a compile-time constant duration; host
  must implement the sleep callback.
- `input(&variable)` — request text input from the host into a variable.
- `save(buffer, length)` / `load(buffer, length)` — ask the host to persist or
  restore a RAM block. Return 1 on success, 0 on failure. In the SDL host, a
  `.uls` file beside the bytecode contains a 4-byte big-endian size followed
  by saved bytes; headless storage lasts only for the process.

## Audio functions

Use `flag(2, 1)` for tones only, `flag(2, 2)` for the SFX/music engine, or
`flag(2, 3)` (`AUDIO_PCM`) for the engine plus PCM clips.

- `beep(frequency_hz, duration_ms)` — start a square-wave tone.
- `audioload(kind, index, data, length)` — load record-aligned sound data into
  host storage. `kind=0` is SFX data (68 bytes per record); `kind=1` is music
  patterns (4 bytes per record). `data` can be a constant ROM array or RAM
  array. The bank must fit in the 64 SFX or 64 pattern slots starting at
  `index`. Returns 1 on success.
- `sfx(id, channel)` — play an SFX on channel `0..3`; channel `-1` selects a
  free channel and replaces channel 0 if all four channels are busy.
- `music(pattern[, fade_ms[, channel_mask]])` — play a music pattern;
  `music(-1[, fade_ms])` stops music. `fade_ms` fades the music in (or out, when
  stopping) over that many milliseconds. `channel_mask` is a 4-bit mask of the
  channels music may use (`1` = channel 0, `7` = channels 0 to 2); the other
  channels stay free for `sfx()`. `0` or omitted means all four channels. Both
  extra arguments must be compile-time constants. A song follows the PICO-8
  pattern flags: loop start (bit 7 of byte 0), loop back (bit 7 of byte 1, jumps
  to the nearest loop-start pattern at or before it, else pattern 0), stop (bit 7
  of byte 2) and channel off (bit 6 of any byte). A pattern lasts until its first
  non-looping channel ends, and when a song stops, its other channels are cut.
- `audiostop()`, `audiopause()`, `audioresume()` — stop, pause, or resume.
- `audiovolume(0..255)` — set master volume.
- `audiostatus(channel)` — return the SFX playing on channel `0..3`, the
  current music pattern for `-1`, or `0xFFFFFFFF` if idle.

### PCM clips

In mode 3 the host can also play raw 8-bit PCM straight from the program ROM.
Samples are unsigned bytes where 128 is silence. The VM keeps no copy of the
clip, so nothing is allocated in RAM.

- `pcmplay(data, size, rate, volume, flags)` — start a clip and return its
  voice number `0..7`, or `255` if it could not be played. `data` must be a
  `const` array (the compiler rejects RAM arrays), `size` is its length in
  bytes, `rate` is the clip's sample rate in Hz, `volume` is `0..255` and
  `flags` is `0` or `PCM_LOOP`.
- `pcmstop(voice)` — stop one voice.
- `pcmvolume(voice, volume)` — change the volume of a playing voice.
- `pcmstatus(voice)` — `1` while the voice plays, `0` when it is free, or
  `0xFFFFFFFF` for an invalid voice.

`rate` can be any integer from 1 to 1,445,046,750, so unusual clip rates
(4000, 11025, 16000, 44100...) need no conversion. The mixer runs at 22050 Hz
and steps through the clip at exactly `rate` samples per second without
interpolation, so very low rates sound steppy and rates above 22050 skip
samples without filtering.

A clip without `PCM_LOOP` frees its voice by itself when it ends. A looping
clip plays until `pcmstop`, `audiostop()` or a replacement. If all voices are
busy, `pcmplay` replaces the voice closest to finishing (looping voices never
finish), so a new sound is never refused for lack of a voice. `audiostop()`
stops every voice and `audiopause()` holds them in place. Voice volumes are
independent of `audiovolume`, and all voices are summed and clamped, so several
loud clips at once can clip.

`pcmplay` returns `255` and the other three functions return `0xFFFFFFFF`
outside mode 3, when the host has no PCM support, or when the host cannot read
the ROM in place. Invalid arguments (a zero `size` or `rate`, a clip past the
end of the ROM) also return `255`.

```c
const uint8_t blip[] = { /* u8 samples */ };

flag(FLAG_AUDIO, AUDIO_PCM);
uint8_t voice = pcmplay(blip, sizeof(blip), 8000, 255, 0);
if (voice != 255 && pcmstatus(voice) == 1)
    pcmvolume(voice, 128);
```

The binary format is PICO-8-compatible for SFX and music records. Each SFX
record contains filter, speed, loop start/end, and 32 little-endian 16-bit
notes. The VM passes records to the host; it does not retain the audio bank in
RAM. The current synth approximates custom waveforms with triangle waves.
The SFX filters are approximations of PICO-8's: byte 64 bit 1 is noiz, bit 2
buzz, bits 3-4 detune (a second oscillator slightly sharp), bits 5-6 reverb (an
echo after 2 or 4 ticks), and bit 7 of the loop bytes 66 and 67 is dampen (a low
pass). Reverb uses about 1.5 KB of RAM per channel; compile it out with
`#define UNIAUDIO_REVERB 0` on small hosts. Slide glides from the previous note,
vibrato is a slow pitch wobble, and the arpeggios step through the 4-note group.
See the [audio example](../../examples/audio/audio.c)
and [audio guide](../../examples/audio/README.md).

## Text output

`printf` is a compiler builtin and has a fixed supported format set:

- Integer: `%d`, `%i`, `%u`, `%o`, `%x`, `%X`.
- Character/string: `%c`, `%s`, `%%`.
- Floating point: `%f`, `%F`, `%e`, `%E`, `%g`, `%G`.
- Numeric flags: `-`, `+`, space, `#`, `0`; literal width and precision are
  limited to 0..99.
- `h` and `hh` narrow integer conversions.
- `ll` supports `%llu`, `%lld`, `%llx`, and `%llX` with 64-bit integer values.

Flag combinations are conversion-specific: `%u`, `%o`, `%x`, and `%X` reject
`+` and space; `%d`, `%i`, and `%u` reject `#`; `%c` accepts only optional
width and `-`. `%s` does not support width, precision, or flags. Dynamic `*`
width/precision, `l` and `z` integer conversions, and unsupported
conversion letters are rejected. Floating formatting uses the host C
library's `snprintf`, so exact rounding and locale behavior can vary by host.
`puts`, `putchar`, and simple VM output builtins are also available.

Builtins whose VM interface accepts 32-bit values use the low 32 bits of a
64-bit argument. This includes `rand`, `srand`, `sleep`, pointer and memory
addresses, drawing coordinates, and the value stack. `min`/`max`/`abs` retain
their 32-bit VM behavior; use ordinary 64-bit expressions for wider arithmetic.

## Examples

- [Audio playback and data loading](../../examples/audio/audio.c)
- [Persistent save counter](../../examples/save_counter/save_counter.c)
- [Off-screen drawing and clipping](../../examples/offscreen/offscreen.c)
- [Math helper regression](../../examples/test/math_integer.c)
- [Formatted output regression](../../examples/test/printf_format.c)
