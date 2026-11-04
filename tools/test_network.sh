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

cc -O2 -std=c11 tools/test_host_options.c -o "$tmp/test_host_options"
"$tmp/test_host_options"

python3 tools/regress.py
