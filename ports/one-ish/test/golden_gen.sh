#!/usr/bin/env bash
# Writes ports/one-ish/test/golden_states.txt (test/golden_check.c) from ANOTHER copy of the repo: the engine before a
# change. Put that copy somewhere inside this repo first (git archive keeps it clean), e.g. on Git Bash:
#   mkdir -p ports/one-ish/build/golden_base && git archive <base-commit> | tar -x -C ports/one-ish/build/golden_base
#   win/run.sh bash ports/one-ish/test/golden_gen.sh ports/one-ish/build/golden_base
# Then test/run.sh checks the engine of THIS copy against it.
set -euo pipefail
BASE=$1
OUT=$PWD/ports/one-ish/test/golden_states.txt
TOOLS=("$PWD/ports/one-ish/test/golden_check.c" "$PWD/ports/one-ish/test/golden_rng.cc")
cd "$BASE"
P=ports/one-ish
python3 tools/gen_vst.py "$P/vst.json" --params-h
eval "$(python3 tools/gen_vst.py "$P/vst.json" --shell)"
mkdir -p "$P/build/obj_golden"
objs=()
for f in "${TOOLS[@]}" $SOURCES; do
  o="$P/build/obj_golden/$(basename "${f%.*}" | tr '/' '_')_$(echo "$f" | md5sum | cut -c1-8).o"
  case "$f" in
    *.cc|*.cpp|*.cxx) g++ -std=gnu++11 -fno-exceptions -fno-rtti -O2 -I"$P/build" -I"$P/src" -Iwrapper $CFLAGS -c "$f" -o "$o" ;;
    *) gcc -std=gnu11 -O2 -I"$P/build" -I"$P/src" -Iwrapper $CFLAGS -c "$f" -o "$o" ;;
  esac
  objs+=("$o")
done
g++ -O2 "${objs[@]}" -lm -o "$P/build/golden_gen"
"$P/build/golden_gen" gen "$OUT"
