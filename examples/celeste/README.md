# Celeste

A colorful 128x64 RGB332 precision-platforming game for the uniClang VM. Guide
the climber through a glowing aurora, avoid the red spikes, touch the golden
checkpoint, and reach the summit marker. The classic 64-pixel-wide level is
expanded to the full display with double-width pixels.

Controls:
- Left and Right: walk
- C (A button): jump
- Hold C for a higher jump; jump is buffered and supports coyote time
- Horizontal movement accelerates and eases to a stop
- Left or Right + V (B button): dash horizontally
- Up + V (B button): dash upward
- Dash recharges when you land
- Enter (Start): exit after reaching the summit

The bottom spike strip sends the climber back to the last checkpoint; jump or
dash over it. Controls and dash readiness stay in the bottom bar.

Compile from the repository root:

```powershell
python .\compiler.py .\examples\celeste\celeste.c -o .\examples\celeste\celeste.ulc
```

Run the resulting bytecode with a compatible uniClang VM. When using the SDL
interpreter in WSL/Linux, rebuild it after VM or opcode changes, then run:

```sh
gcc -Wall -Wextra -std=c11 C-interpreter/main.c -o C-interpreter/main \
  $(pkg-config --cflags --libs sdl2) -lm
./C-interpreter/main examples/celeste/celeste.ulc
```

The Python VM mentioned in the root README is not included in this workspace.