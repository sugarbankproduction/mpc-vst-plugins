#!/usr/bin/env bash
# ONE-ish offline checks (test/offline_check.c): aliasing, envelope, a demo WAV, and a rough PC timing.
# Engine sources come from vst.json (C, or C++ for third-party DSP); everything links with g++.
# Run from the repo root after tools/test_port.sh has made build/params.h; on Windows: win/run.sh bash ports/one-ish/test/run.sh
set -euo pipefail
P=ports/one-ish
mkdir -p "$P/build/obj_san" "$P/build/obj_o2" "$P/test/out"
eval "$(python3 tools/gen_vst.py "$P/vst.json" --shell)"
INC=(-I"$P/build" -I"$P/src" -Iwrapper -DVST2_CHUNK_MAX=262144)   # the chunk size: the desktop build's (desktop/build.sh)
build() {   # build <objdir> <out> <flags...>: the test (TEST, default offline_check.c) plus the engine sources
  local dir=$1 out=$2; shift 2
  local objs=()
  for f in ${TEST:-$P/test/offline_check.c} $SOURCES; do
    local o="$dir/$(echo "${f%.*}" | tr '/' '_').o"   # by full path: third-party files share names
    case "$f" in
      *.cc|*.cpp|*.cxx) g++ -std=gnu++11 -fno-exceptions -fno-rtti "$@" "${INC[@]}" $CFLAGS -c "$f" -o "$o" ;;
      *) gcc -std=gnu11 -Wall -Wextra "$@" "${INC[@]}" $CFLAGS -c "$f" -o "$o" ;;
    esac
    objs+=("$o")
  done
  g++ "$@" "${objs[@]}" -lm -o "$out"
}
build "$P/build/obj_san" "$P/build/offline_check" -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
"$P/build/offline_check" "$P/test/out/demo.wav"
build "$P/build/obj_o2" "$P/build/offline_bench" -O2
# old sounds play exactly as before (test/golden_check.c against test/golden_states.txt, made by test/golden_gen.sh)
TEST="$P/test/golden_check.c $P/test/golden_rng.cc" build "$P/build/obj_o2" "$P/build/golden_check" -O2
"$P/build/golden_check" check "$P/test/golden_states.txt"
# (2026-10-30) the randomiser gives what it gave before but for that day's deliberate changes (test/rnd_golden.c)
gcc -std=gnu11 -Wall -Wextra -O1 "${INC[@]}" "$P/test/rnd_golden.c" -lm -o "$P/build/rnd_golden"
"$P/build/rnd_golden" check "$P/test/rnd_golden.txt"
"$P/build/rnd_golden" stats
# (the factory banks, test/bank_check.c, run inside offline_check above: both banks' tables and bank_*_demo.wav)
"$P/build/offline_bench" bench
