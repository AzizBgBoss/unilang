# VM instruction reference

[Assembly index](README.md) ·
[Bytecode encoding](bytecode-format.md) ·
[Annotated examples](examples.md)

Operands are listed in bytecode order. `u8`, `u16`, and `u32` are unsigned
big-endian immediates; `reg` is a one-byte register index (`0..15`); `addr` is
a four-byte RAM address; `target` is a four-byte absolute bytecode offset.
`text` is a two-byte byte length followed by that many raw bytes. See the
[format page](bytecode-format.md) for floating formats, `outfmt`, and the
`blit` descriptor.

## System, control, and output

| Opcode | Mnemonic | Operands | Behavior |
|---:|---|---|---|
| `00` | `nop` | — | Do nothing. |
| `01` | `flag` | `flag:u8, value:u8` | Set a packed VM flag; flag 0 changes trigger graphics callbacks. |
| `02` | `isflagsupported` | `flag:u8, value:u8, dst:reg` | Write host support result to `dst`; value 0 is always supported. |
| `03` | `setbyte` | `addr:u32, value:u8` | Store one byte in RAM. |
| `04` | `write` | `addr:u32, text` | Copy immediate raw text bytes into RAM. |
| `05` | `outc` | `addr:u32` | Output one byte at RAM address as a character. |
| `06` | `out` | `addr:u32` | Output one byte at RAM address as an unsigned number. |
| `07` | `input` | `max_chars:u32, addr:u32` | Ask host for input into RAM; host callback is optional. |
| `08` | `print` | `text` | Output immediate text and a newline. |
| `09` | `printn` | `text` | Output immediate text without a newline. |
| `0A` | `printv` | `length:u32, addr:u32` | Output RAM bytes as characters, then newline. |
| `0B` | `printvn` | `length:u32, addr:u32` | Output RAM bytes as characters, no newline. |
| `0C` | `cur` | — | Debug: print the current instruction address. |
| `0D` | `memory` | — | Debug: dump VM RAM. |
| `0E` | `flags` | — | Debug: dump VM flags. |
| `0F` | `setpc` | `target:u32` | Unconditional absolute jump. |
| `10` | `subroutine` | `target:u32` | Push return PC and call absolute target; bounded return stack. |
| `11` | `return` | — | Pop return PC; faults on underflow. |
| `12` | `sleep` | `ms:u32` | Sleep through optional host callback. |
| `13` | `refreshscreen` | — | Present framebuffer then invoke clear-screen callback. |
| `14` | `romread8` | `src:reg, dst:reg` | Read one ROM byte at offset in `src`. |
| `15` | `romread16` | `src:reg, dst:reg` | Read big-endian 16-bit ROM value. |
| `16` | `romread32` | `src:reg, dst:reg` | Read big-endian 32-bit ROM value. |
| `17` | `setpixel` | `x:reg, y:reg, color:reg` | Draw one pixel through host callback. |
| `18` | `getpixel` | `x:reg, y:reg, dst:reg` | Read one pixel through host callback. |
| `19` | `getkeyboard` | `dst:reg` | Read host keyboard bitmask. |
| `1A` | `gettime` | `dst:reg` | Read host elapsed milliseconds. |
| `1B` | `getmemsize` | `dst:reg` | Write VM RAM capacity. |
| `1C` | `getflag` | `flag:u8, dst:reg` | Read packed flag value. |
| `1D` | `randreg` | `min:reg, max:reg, dst:reg` | Random integer in `[min, max)`; VM PRNG. |
| `1E` | `srandreg` | `seed:reg` | Seed VM PRNG. |
| `1F` | `drawstring` | `x:reg, y:reg, color:reg, addr:reg, length:reg, source:u8, out_x:reg, out_y:reg` | Draw 4x6 glyphs from RAM (`source=0`) or ROM (`source=1`); returns cursor. |
| `20` | `numtostr` | `value:reg, addr:u32, out_length:reg` | Convert unsigned register value to decimal RAM text. |
| `21` | `floatop` | `operation:u8, format:u8, left:reg, right:reg, dst:reg` | Binary32/binary64 arithmetic, comparisons, and math functions. |
| `22` | `floatcast` | `src_format:u8, dst_format:u8, src:reg, dst:reg` | Convert/copy numeric values at RAM addresses held in registers. |
| `23` | `romread64` | `src:reg, high:reg, low:reg` | Read 8-byte big-endian ROM value into two 32-bit words. |

`floatop` format is 7 (binary32) or 8 (binary64). Operation IDs are 1 add,
2 subtract, 3 multiply, 4 divide, 5 negate, 6 equality, 7 inequality,
8 less-than, 9 greater-than, 10 less-or-equal, 11 greater-or-equal, 12 sine,
13 cosine, 14 square root, and 15 `atan2(left, right)`. `floatcast` numeric
format IDs are described in [bytecode format](bytecode-format.md).

## Graphics, storage, and audio

| Opcode | Mnemonic | Operands | Behavior |
|---:|---|---|---|
| `24` | `fillrect` | `x:reg, y:reg, width:reg, height:reg, color:reg` | Fill a clipped half-open rectangle; optional host fast path. |
| `25` | `drawline` | `x0:reg, y0:reg, x1:reg, y1:reg, color:reg` | Draw clipped Bresenham line. |
| `26` | `blit` | `descriptor_addr:u32` | Blit a clipped sprite using the 11-word descriptor. |
| `27` | `setpalette` | `index:reg, rgb555:reg` | Set palette entry through optional callback. |
| `28` | `save` | `addr:reg, length:reg, result:reg` | Host-save a validated RAM block; result is 1 on success. |
| `29` | `load` | `addr:reg, length:reg, result:reg` | Host-load a validated RAM block; result is 1 on success. |
| `2A` | `beep` | `frequency:reg, duration_ms:reg` | Start host tone. |
| `2B` | `audio_command` | `command:u8, a:reg, b:reg, result:reg` | Send audio command to host and store result. |
| `2C` | `audio_load` | `kind:reg, index:reg, addr:reg, length:reg, source:u8, result:reg` | Copy sound records from RAM (`0`) or ROM (`1`) into host. |
| `2D` | `memcpy` | `dst:reg, src:reg, length:reg, source:u8` | Copy RAM (`0`) or ROM (`1`) bytes to validated RAM destination. RAM overlap uses memmove semantics. |
| `2E` | `memset` | `dst:reg, value:reg, length:reg` | Fill validated RAM byte range. |
| `2F` | `fillscreen` | `color:reg` | Fill active framebuffer. |
| `30` | — | — | Reserved/undefined; faults as unknown opcode. |
| `31` | `fillcircle` | `x:reg, y:reg, radius:reg, color:reg` | Fill clipped integer circle. |
| `32` | `drawrect` | `x:reg, y:reg, width:reg, height:reg, color:reg` | Draw clipped outline; right/bottom edges are inclusive. |
| `33` | `minreg` | `left:reg, right:reg, dst:reg, signed:u8` | Select minimum; signed is nonzero for signed comparison. |
| `34` | `maxreg` | `left:reg, right:reg, dst:reg, signed:u8` | Select maximum; signed is nonzero for signed comparison. |
| `35` | `absreg` | `src:reg, dst:reg` | 32-bit two's-complement absolute value. |
| `36` | `mul64` | `left:reg, right:reg, high:reg, low:reg, signed:u8` | 32x32 multiply; write high and low words. |
| `37` | `jmpreg` | `target:reg` | Jump to absolute bytecode offset in register. Invalid target faults. |
| `38` | `callreg` | `target:reg` | Bounded indirect call; push return PC. |
| `39` | `pushr` | `src:reg` | Push 32-bit value onto 64-entry value stack; overflow faults. |
| `3A` | `popr` | `dst:reg` | Pop value-stack word; underflow faults. |
| `3B` | `outfmt` | `addr:u32, format:u8, conversion:u8, width:u8, precision:u8, flags:u8` | Formatted numeric output; see [format page](bytecode-format.md#outfmt-encoding). |

`audio_command` command IDs are 1 `sfx`, 2 `music`, 3 `stop`, 4 `pause`,
5 `resume`, 6 `volume`, and 7 `status`. Audio callback details and SFX/music
record sizes are in [host integration](../vm/host-integration.md) and
[builtins](../c-language/builtins.md).

## Register and indirect-memory operations

| Opcode | Mnemonic | Operands | Behavior |
|---:|---|---|---|
| `40` | `setreg` | `dst:reg, value:u32` | Set register to immediate. |
| `41` | `movreg` | `dst:reg, src:reg` | Copy register. |
| `42`-`44` | `loadreg8/16/32` | `dst:reg, addr:u32` | Load big-endian RAM value; zero-extend. |
| `45`-`47` | `storereg8/16/32` | `src:reg, addr:u32` | Store low 8/16/32 bits big-endian. |
| `48`-`4C` | `addreg/subreg/mulreg/divreg/modreg` | `left:reg, right:reg, dst:reg` | 32-bit arithmetic wraps modulo 2^32; div/mod by zero leave destination unchanged. |
| `4D`-`4F` | `addregv/subregv/mulregv` | `src:reg, immediate:u32, dst:reg` | Arithmetic with immediate. |
| `50` | `compreg` | `left:reg, right:reg, dst:reg` | Store 0 if equal, 1 if unsigned left greater, 2 if unsigned left less. |
| `51`-`52` | `ifreg/ifnotreg` | `condition:reg, target:u32` | Jump if nonzero / zero. |
| `53`-`54` | `ifsubreg/ifnotsubreg` | `condition:reg, target:u32` | Conditional call if nonzero / zero. |
| `55` | `notreg` | `src:reg, dst:reg` | Bitwise NOT. |
| `56`-`5B` | `orreg/andreg/norreg/nandreg/xorreg/xnorreg` | `left:reg, right:reg, dst:reg` | 32-bit bitwise operation. |
| `5C`-`5D` | `shlreg/shrreg` | `left:reg, amount:reg, dst:reg` | Logical shifts; amount >= 32 yields zero. |
| `60`-`62` | `loadregi8/16/32` | `addr_reg:reg, dst:reg` | Load from RAM address held in register; zero-extend. |
| `63`-`65` | `storeregi8/16/32` | `addr_reg:reg, value_reg:reg` | Store to RAM address held in register. |
| `66`-`6B` | `eqreg/neqreg/ltreg/gtreg/lereg/gereg` | `left:reg, right:reg, dst:reg` | Unsigned comparisons; write 0 or 1. |
| `6C` | `outreg` | `src:reg` | Print unsigned register value. |
| `6D`-`6E` | `sdivreg/smodreg` | `left:reg, right:reg, dst:reg` | Signed division/remainder; zero divisor leaves destination unchanged; `INT32_MIN / -1` yields `INT32_MIN` and remainder 0. |
| `6F` | `sshrreg` | `left:reg, amount:reg, dst:reg` | Arithmetic right shift; amounts >= 32 yield all sign bits. |
| `70` | `outregsigned` | `src:reg` | Print signed 32-bit register value. |
| `71`-`74` | `sltreg/sgtreg/slereg/sgereg` | `left:reg, right:reg, dst:reg` | Signed comparison; write 0 or 1. |
| `75`-`77` | `andregv/orregv/xorregv` | `src:reg, immediate:u32, dst:reg` | Bitwise operation with immediate. |
| `78`-`7A` | `shlregv/shrregv/sshrregv` | `src:reg, amount:u8, dst:reg` | Logical left/right or arithmetic right; amounts >= 32 yield zero for logical and sign-fill for arithmetic. |
| `7B` | `cmpregv` | `src:reg, immediate:u32, dst:reg, condition:u8` | Compare register with immediate and store encoded comparison result. |
| `7C` | `jcmp` | `left:reg, right:reg, condition:u8, target:u32` | Conditional branch using comparison selector. |
| `7D` | `jcmpv` | `left:reg, immediate:u32, condition:u8, target:u32` | Conditional branch against immediate. |
| `FF` | `exit` | — | Halt VM. |

For `cmpregv`, `jcmp`, and `jcmpv`, condition selectors 0..5 are unsigned
`==`, `!=`, `<`, `>`, `<=`, `>=`; selectors 6..9 are signed `<`, `>`, `<=`,
`>=`. Gaps `0x5E..0x5F` and `0x7E..0xFE` are reserved/undefined.

## Callback dependence and errors

Graphics, audio, input, timing, storage, and flag-support instructions depend
on callbacks installed by the embedding host. Missing optional callbacks
generally produce no output or a zero/failure result, as documented on
[host integration](../vm/host-integration.md).

Unknown opcodes, invalid float operations, invalid checked indirect targets,
return/value stack overflow or underflow, and malformed bytecode operands
halt the VM and set fault metadata. Out-of-range checked memory operations
also report failure or fault according to the instruction. Do not treat
reserved opcode bytes as no-ops.
