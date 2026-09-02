# Examples

This folder is split into the two language families in this repo:

- `unilang/` — raw VM / bytecode examples
- `uniclang/` — C-like compiler examples for the `uniClang` subset

## Quick start

```bash
python compiler.py path/to/example.uc -o path/to/example.ulc
python main.py -f path/to/example.ulc
```

## Contents

### unilang
- `helloworld` — minimal bytecode greeting
- `roulette` — handcrafted VM demo text

### uniClang
- `pong` — the classic graphics/keyboard demo
- `arithmetic-wraparound` — fixed-width overflow and wraparound behavior
- `bitwise-branches` — comparisons and branch-heavy logic patterns
- `keyboard-echo` — reads keyboard state and prints keys
- `final-combo` — combined graphics + keyboard + arithmetic demo
