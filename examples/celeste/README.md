# Celeste

A small original precision-platforming example for the uniClang VM. Guide the
climber up the screen, avoid the spikes, touch the checkpoint, and reach the
summit marker.

Controls:
- Left and Right: walk
- C (A button): jump
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

Run the resulting bytecode with a compatible uniClang VM. The Python VM
mentioned in the root README is not included in this workspace.