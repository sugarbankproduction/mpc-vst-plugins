#!/usr/bin/env bash
# Web build of ONE-ish ("Moo Won" in the browser), sound only: the same engine, compiled to WebAssembly with Emscripten,
# played by web/site/ (an AudioWorklet, an on-screen and computer keyboard, Web MIDI, the factory presets).
#   web/site/moowon.wasm   the engine and web/glue.c, a standalone module (no Emscripten JavaScript runtime)
# Run from the repo root in Git Bash on Windows (or any bash), with Emscripten installed:
#   EMSDK=/h/Tools/emsdk bash ports/one-ish/web/build.sh
# Then serve web/site/ (any static server) or publish it (docs/WEB.md).
set -euo pipefail
P=ports/one-ish
OUT=$P/build/web
EMSDK=${EMSDK:-/h/Tools/emsdk}
EM=$EMSDK/upstream/emscripten
EMCC=$EM/emcc; EMXX=$EM/em++
[ -f "$EMCC.bat" ] && EMCC=$EMCC.bat EMXX=$EMXX.bat   # Windows
PY=${PYTHON:-python3}; command -v "$PY" >/dev/null || PY=python
eval "$("$PY" tools/gen_vst.py "$P/vst.json" --shell)"
"$PY" tools/gen_vst.py "$P/vst.json" --params-h
mkdir -p "$OUT"
CHUNK=(-DVST2_CHUNK_MAX=262144)
FLAGS=(-O3 -std=gnu11 -Wall -Wno-unused-parameter -I"$P/build" -Iwrapper $CFLAGS "${CHUNK[@]}")
CXXFLAGS=(-O3 -std=gnu++11 -fno-exceptions -fno-rtti -Wno-unused-parameter -I"$P/build" -Iwrapper $CFLAGS "${CHUNK[@]}")
OBJS=()
for f in $SOURCES; do
  o="$OUT/obj_$(echo "${f%.*}" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$P/build/params.h" -nt "$o" ]; then
    case "$f" in
      *.cc|*.cpp|*.cxx) "$EMXX" "${CXXFLAGS[@]}" -c "$f" -o "$o" ;;
      *) "$EMCC" "${FLAGS[@]}" -c "$f" -o "$o" ;;
    esac
  fi
  OBJS+=("$o")
done
"$EMCC" "${FLAGS[@]}" -c "$P/web/glue.c" -o "$OUT/glue.o"
EXPORTS='["_mw_create","_mw_out","_mw_render","_mw_midi","_mw_presets","_mw_preset_name","_mw_preset","_mw_buf","_mw_set"]'
# STANDALONE_WASM + --no-entry: a plain module the sound worklet instantiates itself; its few system imports
# (WASI: printing, exit, clock) are stubbed there. Memory may grow while the engine is created, never while it renders.
"$EMXX" -O3 "${OBJS[@]}" "$OUT/glue.o" -sSTANDALONE_WASM --no-entry -sEXPORTED_FUNCTIONS="$EXPORTS" \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB -fno-exceptions -o "$P/web/site/moowon.wasm"
ls -l "$P/web/site/moowon.wasm"
