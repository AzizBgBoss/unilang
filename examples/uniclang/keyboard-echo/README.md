# Keyboard echo

This example polls the keyboard state and prints which keys are currently down.

It highlights how the VM exposes the NES-style keyboard bitfield and how the C-like source reads it via `getkey()`.
