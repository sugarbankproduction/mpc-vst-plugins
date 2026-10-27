#!/usr/bin/env bash
# ONE-ish offline checks (test/offline_check.c): aliasing, envelope, a demo WAV, and a rough PC timing.
# Engine sources come from vst.json (C, or C++ for third-party DSP); everything links with g++.
# Run from the repo root after tools/test_port.sh has made build/params.h; on Windows: win/run.sh bash ports/one-ish/test/run.sh
set -euo pipefail
P=ports/one-ish
mkdir -p "$P/build/obj_san" "$P/build/obj_o2" "$P/test/out"
eval "$(python3 tools/gen_vst.py "$P/vst.json" --shell)"
INC=(-I"$P/build" -I"$P/src" -Iwrapper)
build() {   # build <objdir> <out> <flags...>: offline_check.c plus the engine sources, each by its language
  local dir=$1 out=$2; shift 2
  local objs=()
  for f in "$P/test/offline_check.c" $SOURCES; do
    local o="$dir/$(printf %s "${f%.*}" | tr / _).o"   # the path in the name: two resources.cc must not collide
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
"$P/build/offline_bench" bench
