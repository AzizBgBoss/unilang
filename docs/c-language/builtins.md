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
`drawline`, `blit`, `drawchar`, `drawstring`, `drawnum`, and `getpixel`
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
- `drawnum(number, x, y[, color])`: draw an unsigned 32-bit decimal number with
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

`blit` is the C-level sprite-sheet operation, with signature:

```c
blit(x, y, width, height, source, source_x, source_y, stride,
     flags, palette, transparent);
```

`source` is a RAM byte array/pointer or a constant ROM array. `stride` is the
source row length in pixels/bytes. `flags` selects source mode options such as
RAM versus ROM and palette lookup; see the
[bytecode format page](../assembly/bytecode-format.md#blit-descriptor) for the
VM descriptor layout. Source and destination rectangles are clipped.

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
- `gettime()` returns host elapsed milliseconds as a 32-bit value (wraps after
  about 49.7 days); epoch and resolution depend
  on the host. This is polling, not an interrupt or guaranteed frame clock.
- `gettime64(&high, &low)` stores the elapsed milliseconds as a 64-bit value in
  two `uint32_t` variables, `high` being the upper word and `low` the lower, so
  it does not wrap in practice. It is statement-only and takes two output
  addresses. There is no 64-bit integer type (see
  [types](types-and-memory.md#scalar-types)), so work with the two words. The
  VM extends the host's 32-bit clock by counting its wraps, so call `gettime64`
  or `gettime` at least once every 49.7 days. Elapsed time between two
  `gettime()` samples stays correct with unsigned subtraction while it is under
  49.7 days.
- `sleep(milliseconds)` — sleep for a compile-time constant duration; host
  must implement the sleep callback.
- `input(&variable)` — request text input from the host into a variable.
- `save(buffer, length)` / `load(buffer, length)` — ask the host to persist or
  restore a RAM block. Return 1 on success, 0 on failure. In the SDL host, a
  `.uls` file beside the bytecode contains a 4-byte big-endian size followed
  by saved bytes; headless storage lasts only for the process.

## Audio functions

Use `flag(2, 1)` for tones only or `flag(2, 2)` for the SFX/music engine.

- `beep(frequency_hz, duration_ms)` — start a square-wave tone.
- `audioload(kind, index, data, length)` — load record-aligned sound data into
  host storage. `kind=0` is SFX data (68 bytes per record); `kind=1` is music
  patterns (4 bytes per record). `data` can be a constant ROM array or RAM
  array. The bank must fit in the 64 SFX or 64 pattern slots starting at
  `index`. Returns 1 on success.
- `sfx(id, channel)` — play an SFX on channel `0..3`; channel `-1` selects a
  free channel and replaces channel 0 if all four channels are busy.
- `music(pattern)` — play a music pattern; `music(-1)` stops music.
- `audiostop()`, `audiopause()`, `audioresume()` — stop, pause, or resume.
- `audiovolume(0..255)` — set master volume.
- `audiostatus(channel)` — return the SFX playing on channel `0..3`, the
  current music pattern for `-1`, or `0xFFFFFFFF` if idle.

The binary format is PICO-8-compatible for SFX and music records. Each SFX
record contains filter, speed, loop start/end, and 32 little-endian 16-bit
notes. The VM passes records to the host; it does not retain the audio bank in
RAM. The current synth approximates custom waveforms with triangle waves and
ignores SFX filter data. See the [audio example](../../examples/audio/audio.c)
and [audio guide](../../examples/audio/README.md).

## Text output

`printf` is a compiler builtin and has a fixed supported format set:

- Integer: `%d`, `%i`, `%u`, `%o`, `%x`, `%X`.
- Character/string: `%c`, `%s`, `%%`.
- Floating point: `%f`, `%F`, `%e`, `%E`, `%g`, `%G`.
- Numeric flags: `-`, `+`, space, `#`, `0`; literal width and precision are
  limited to 0..99.
- `h` and `hh` narrow integer conversions.

Flag combinations are conversion-specific: `%u`, `%o`, `%x`, and `%X` reject
`+` and space; `%d`, `%i`, and `%u` reject `#`; `%c` accepts only optional
width and `-`. `%s` does not support width, precision, or flags. Dynamic `*`
width/precision, `ll`, `l`, and `z` integer conversions, and unsupported
conversion letters are rejected. Floating formatting uses the host C
library's `snprintf`, so exact rounding and locale behavior can vary by host.
`puts`, `putchar`, and simple VM output builtins are also available.

## Examples

- [Audio playback and data loading](../../examples/audio/audio.c)
- [Persistent save counter](../../examples/save_counter/save_counter.c)
- [Off-screen drawing and clipping](../../examples/offscreen/offscreen.c)
- [Math helper regression](../../examples/test/math_integer.c)
- [Formatted output regression](../../examples/test/printf_format.c)
