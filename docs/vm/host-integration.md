# VM host integration

[Documentation index](../README.md) ·
[Assembly overview](../assembly/README.md) ·
[Instruction reference](../assembly/instruction-reference.md) ·
[Compiler builtins](../c-language/builtins.md)

The VM is implemented as a header-only C interpreter in
[`C-interpreter/unilang.h`](../../C-interpreter/unilang.h). The VM core does not
depend on SDL; display, keyboard, timing, storage, and sound are connected
through callbacks.

## Minimal embedding

```c
#define ULVM_MEM_SIZE 8192
#include "C-interpreter/unilang.h"

static void output_char(char c) { /* send one character to a console */ }
static uint8_t read_byte(uint32_t address) { return program[address]; }

int main(void)
{
    UnilangVM vm;
    ULVM_init(&vm, output_char, read_byte);
    ULVM_set_rom(&vm, program, program_size);  /* optional flat-ROM fast path */

    while (!ULVM_run(&vm, 1000)) {
        /* Poll platform events or service a watchdog between instruction batches. */
    }
    return vm.faulted ? 1 : 0;
}
```

`ULVM_init` requires output and bytecode-read callbacks. If the ROM is a
directly addressable byte array, `ULVM_set_rom` can avoid calling `readbyte`
for each fetched byte. Otherwise, leave the flat ROM unset and implement
`readbyte` for flash, SD, or external storage. `ULVM_run(vm, budget)` executes
up to `budget` instructions; `ULVM_handlenextinstruction` is the single-step
alternative.

## Optional callback setters

Install only the features the target supports:

```c
ULVM_set_graphics(&vm, setpixel, getpixel, refreshscreen,
                  enablegraphics, disablegraphics, clearscreen);
ULVM_set_graphics_fast(&vm, fillrect_fast, blitrow_fast); /* optional */
ULVM_set_keyboard(&vm, getkeyboard);
ULVM_set_time(&vm, gettime_ms, sleep_ms);
ULVM_set_input(&vm, input_text);
ULVM_set_flagsupport(&vm, isflagsupported);
ULVM_set_palette(&vm, setpalette);       /* optional */
ULVM_set_storage(&vm, save_block, load_block); /* optional */
ULVM_set_audio(&vm, beep, audio_command, audio_load); /* optional */
```

The callback typedefs and setters in `unilang.h` are authoritative for exact
C signatures. A missing optional callback usually makes that feature a
no-op, return zero, or report failure. Implement meaningful failure behavior
for storage/audio loading, and test the bytecode path on the intended host.

### Graphics callback behavior

- `setpixel(x, y, color)` and optional `getpixel(x, y)` operate in the host's
  active pixel format.
- `refreshscreen` presents the frame; VM execution then invokes
  `clearscreen`.
- `enablegraphics(width, height, colorsize)` and `disablegraphics` are called
  when flag 0 changes. `colorsize` is bits per pixel; palette modes pass
  `0x80 | bits` (`0x82` for 2-bit indexed and `0x88` for 8-bit indexed).
- `ULVM_set_graphics_fast` may provide rectangle and sprite-row bulk paths.
  The VM clips work and falls back to pixel callbacks where appropriate.
  Fast callbacks must interpret colors exactly like the host's normal
  `setpixel`.
- `setpalette(index, color)` receives RGB555.

The SDL host supports graphics flag 0 values 1 through 12. Other hosts decide
what they support and should report it through `isflagsupported`.

### Keyboard and time

Keyboard callback returns the NES-style 8-bit bitmask described in
[builtins](../c-language/builtins.md). `gettime` returns elapsed milliseconds;
resolution and epoch are host-specific. This is a polling timer, not a VM
interrupt. Sleep and input are host callbacks.

### Persistence

Save and load callbacks receive a pointer to a validated VM RAM block and its
byte length. Return nonzero only on success. SDL uses a `.uls` sibling file
with a four-byte big-endian size followed by bytes. The headless host keeps
data in-process. Embedded hosts may use flash, EEPROM, or another storage
medium; define atomicity and lifetime in that host's documentation.

### Audio

`audio_command` receives command IDs: 1 SFX, 2 music, 3 stop, 4 pause,
5 resume, 6 master volume, 7 status. `audio_load` receives one validated
record at a time; a bank-loading opcode is expanded by the VM into per-record
callbacks. SFX records are 68 bytes and music patterns are 4 bytes. `beep`
is separate from the SFX/music commands.

[`C-interpreter/uniaudio.h`](../../C-interpreter/uniaudio.h) implements the
reference four-channel synthesizer at 22050 Hz. It is an optional C header,
not part of the VM's core. The host can implement another sound engine or omit
audio entirely.

## Optional networking

Networking state is optional and installed with `ULVM_set_net(&vm, &net)`.
Initialize it with `ULVM_net_init(&net)`. `isflagsupported(15, 1)` reports
whether this pointer is present. The fixed handle capacity is
`ULVM_NET_MAX_HANDLES` (8); payload and receive-buffer capacity is
`ULVM_NET_MAX_BUFFER` (2048). Addresses passed to networking opcodes refer to
VM RAM and are bounds-checked. Network calls are nonblocking at the VM level;
receive copies only bytes already buffered.

Hosts may install native operations with `ULVM_net_set_ops(&net, &ops,
context)`. The operation table covers open, poll, send, receive, accept,
options, HTTP requests, peer/local address queries, discovery, and shutdown.
The context must remain alive until `ULVM_net_shutdown(&net)` closes all
handles and calls the backend shutdown hook. Without an operation table, the
VM retains its deterministic in-memory UDP backend.

The bundled backend in `unilang.h` is an in-memory UDP loopback. The SDL host
uses `C-interpreter/net_sockets.h` for nonblocking TCP, UDP, and HTTP over TCP.
It accepts numeric IPv4/IPv6 addresses only (no DNS or URL schemes). HTTP
GET/POST requests use caller-provided RAM headers and bounded request bodies;
response headers are parsed for status and response bytes are read with
`netrecv`. TCP listeners accept up to the remaining handle-table capacity.
TCP_NODELAY, TCP length-prefix framing, timeouts, and UDP broadcast are
supported. HTTP headers are passed per request; the HTTP-header option ID is
reserved. LAN discovery uses the fixed local broadcast protocol documented in
the [opcode reference](../assembly/instruction-reference.md#optional-networking-90-a0).
The opcode contract, limits, status values, and error codes are in the
[assembly reference](../assembly/instruction-reference.md#optional-networking-90-a0);
compiler calls are listed in [builtins](../c-language/builtins.md#networking).

### Polling and host security

Networking calls return without waiting for a remote peer. After opening TCP,
poll `netstatus` until it is connected or closed. Call `netaccept` on a
listener periodically; check `netlasterror` to distinguish a full handle table
or an I/O failure from no accepted connection. `netsend` may report a partial
write, so retry the remaining bytes. Check `netavail` before `netrecv`; receive
returns only buffered bytes. For framed TCP, `netavail` reports a complete
payload and `netrecv` consumes one frame. Start an HTTP request once, poll
`nethttpstatus`, and read response chunks with `netavail`/`netrecv`.

The VM does not enforce endpoint allowlists or application permissions. A host
controls access by withholding `ULVM_set_net` or installing its own callbacks
that restrict addresses, ports, and kinds. The SDL implementation accepts
numeric addresses without an allowlist. TCP listeners bind all local
interfaces, so only use them on trusted networks or add a host-side firewall
rule. UDP discovery is opt-in, uses local-network broadcast only, and includes
the game ID, advertised port, and public session name; never put secrets in
these fields. The bundled discovery implementation does not use multicast.
The HTTP client sends plaintext and does not support HTTPS/TLS, authentication
helpers, DNS names, or URL parsing; do not send credentials or sensitive data.

Supported endpoint syntax is a numeric IPv4 or IPv6 address supplied separately
from the port. HTTP uses an origin-form path such as `/score`; `http://` and
`https://` URL strings are not accepted. The in-memory reference backend
implements UDP only; the SDL backend implements nonblocking TCP, UDP,
plain-HTTP GET/POST, framing, broadcast discovery, and up to eight total handles.
See the [network examples](../../examples/network/README.md) for runnable
polling patterns and host/join samples.

## Memory and embedded targets

Define `ULVM_MEM_SIZE` before including `unilang.h`. The default is only
64 bytes and emits a compile-time warning; it is intended as a fail-safe
configuration, not a typical application size. The compiler emits a runtime
warning if its estimated program RAM need exceeds the host setting.

The VM checks RAM accesses against `ULVM_MEM_SIZE`. Its default bounded
return-address stack holds 16 entries; the separate value stack holds 64
32-bit entries. Stack overflows and underflows fault the VM.

For AVR-class Arduino targets, keep bytecode in `PROGMEM` and make
`readbyte` use `pgm_read_byte`. ARM-based targets can generally read a `const`
byte array directly. Check the target's flash/RAM model rather than assuming
desktop pointer behavior.

## Desktop and test hosts

- [`C-interpreter/main.c`](../../C-interpreter/main.c) is the SDL2 desktop host.
- [`tools/headless.c`](../../tools/headless.c) runs without SDL and supports
  deterministic tests and WAV audio output.
- [`tools/regress.py`](../../tools/regress.py) compiles the examples as a
  regression suite.

See the root [quick start](../../README.md#quick-start) for build commands.
