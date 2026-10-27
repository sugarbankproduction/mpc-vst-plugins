#!/usr/bin/env bash
# ONE-ish offline checks (test/offline_check.c): aliasing, envelope, a demo WAV, and a rough PC timing.
# Run from the repo root after tools/test_port.sh has made build/params.h; on Windows: win/run.sh bash ports/one-ish/test/run.sh
set -euo pipefail
P=ports/one-ish
mkdir -p "$P/build" "$P/test/out"
SRC=("$P/test/offline_check.c" "$P/src/engine.c")
INC=(-I"$P/build" -I"$P/src" -Iwrapper)
gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 "${INC[@]}" "${SRC[@]}" -lm \
  -o "$P/build/offline_check"
"$P/build/offline_check" "$P/test/out/demo.wav"
gcc -std=gnu11 -O2 "${INC[@]}" "${SRC[@]}" -lm -o "$P/build/offline_bench"
"$P/build/offline_bench" bench
