#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cd "$root"

cc -O2 -std=c11 -I C-interpreter tools/test_vm_features.c -lm -o "$tmp/test_vm_features"
"$tmp/test_vm_features"

cc -O2 -std=c11 -pthread -I C-interpreter tools/test_desktop_network.c -lm -o "$tmp/test_desktop_network"
"$tmp/test_desktop_network"

cc -O2 -std=c11 -I C-interpreter tools/headless.c -lm -o "$tmp/headless"
python3 tools/regress.py
python3 compiler.py examples/network/headless_smoke.c -o "$tmp/network_smoke.ulc"
output=$("$tmp/headless" "$tmp/network_smoke.ulc" -frames 1)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -q 'NETWORK SMOKE PASS'
if printf '%s\n' "$output" | grep -q 'NETWORK SMOKE FAIL'; then
    exit 1
fi
