#!/usr/bin/env bash
# Web build of the whole plugin ("Moo Won" with its own panel, in the browser): the engine, the shared VST2 wrapper and
# the desktop editor (desktop/*.c), compiled unchanged with Emscripten against a small Windows stand-in (web/full:
# win32/*.h, win32.c, gdi.c, gdiplus.c, drawing with third_party/plutovg) and played by web/full/host.c.
#   web/site/moowon_full.js, .wasm   the module (threads: needs a cross-origin isolated page, docs/WEB.md)
# Run from the repo root in Git Bash on Windows (or any bash), with Emscripten installed:
#   EMSDK=/h/Tools/emsdk bash ports/one-ish/web/full/build.sh
set -euo pipefail
P=ports/one-ish
F=$P/web/full
OUT=$P/build/web_full
SITE=$P/web/site
EMSDK=${EMSDK:-/h/Tools/emsdk}
EM=$EMSDK/upstream/emscripten
EMCC=$EM/emcc; EMXX=$EM/em++
[ -f "$EMCC.bat" ] && EMCC=$EMCC.bat EMXX=$EMXX.bat   # Windows
PY=${PYTHON:-python3}; command -v "$PY" >/dev/null || PY=python
eval "$("$PY" tools/gen_vst.py "$P/vst.json" --shell)"
for try in 1 2 3 4 5; do   # (Dropbox sometimes holds build/params.h for a moment: try again)
  "$PY" tools/gen_vst.py "$P/vst.json" --params-h && break
  [ $try = 5 ] && exit 1; sleep 3
done
mkdir -p "$OUT" "$SITE"
"$PY" "$P/desktop/gen_layout.py" "$P/params.json" "$OUT/layout.h" > /dev/null
CHUNK=(-DVST2_CHUNK_MAX=262144)
COMMON=(-O3 -pthread -Wno-unused-parameter -I"$P/build" -Iwrapper $CFLAGS "${CHUNK[@]}")
FLAGS=("${COMMON[@]}" -std=gnu11)
CXXFLAGS=("${COMMON[@]}" -std=gnu++11 -fno-exceptions -fno-rtti)
WIN=(-I"$F/win32" -I"$F" -I"$P/desktop" -I"$OUT" -I"$P/third_party/plutovg/include")
OBJS=()
cc() {   # source, object, flags...: compiled again only when the source (or a header in its list) is newer
  local src=$1 obj=$2; shift 2
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$P/build/params.h" -nt "$obj" ] || [ "$F/win32/windows.h" -nt "$obj" ] \
     || [ "$F/gdi.h" -nt "$obj" ] || [ "$F/win32_web.h" -nt "$obj" ]; then
    case "$src" in
      *.cc|*.cpp) "$EMXX" "${CXXFLAGS[@]}" "$@" -c "$src" -o "$obj" ;;
      *) "$EMCC" "${FLAGS[@]}" "$@" -c "$src" -o "$obj" ;;
    esac
  fi
  OBJS+=("$obj")
}
for f in $SOURCES; do cc "$f" "$OUT/obj_$(echo "${f%.*}" | tr '/' '_').o"; done
# the wrapper, renamed as in desktop/build.sh: editor.c's VSTPluginMain wraps it; hostrate.c's table feeds it
cc wrapper/vst2_wrap.c "$OUT/vst2_wrap.o" -DVSTPluginMain=one_ish_wrapped_main -Dmpc_engine=one_ish_desktop_engine \
  -Dmpc_engine_changed=one_ish_desktop_changed
for f in editor editor_draw skin patches library settings scala seq_page more_pics rate_leds quickmod hub nav hub_gens \
         cables matrix_page parts m1p_import hostrate resample; do
  cc "$P/desktop/$f.c" "$OUT/$f.o" "${WIN[@]}" -Wno-comment -Wno-string-plus-int
done
cc "$P/desktop/m1p.c" "$OUT/m1p.o" "${WIN[@]}" -I"$P/src"
for f in win32 gdi gdiplus host; do cc "$F/$f.c" "$OUT/web_$f.o" "${WIN[@]}" -Wall; done
for f in "$P"/third_party/plutovg/source/*.c; do
  cc "$f" "$OUT/pvg_$(basename "${f%.c}").o" -I"$P/third_party/plutovg/include" -DPLUTOVG_BUILD_STATIC -w
done
EXPORTS='["_main","_web_start","_web_info","_web_event","_web_midi","_malloc","_free"]'
"$EMXX" -O3 -pthread "${OBJS[@]}" -o "$SITE/moowon_full.js" \
  -sMODULARIZE -sEXPORT_NAME=createMooWon -sENVIRONMENT=web,worker -sEXPORTED_FUNCTIONS="$EXPORTS" \
  -sEXPORTED_RUNTIME_METHODS='["FS","IDBFS","UTF8ToString","stringToNewUTF8","HEAPU8","HEAPU32","HEAP32","HEAPF32","wasmMemory"]' \
  -lidbfs.js -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=1GB -sSTACK_SIZE=2MB \
  -sDEFAULT_PTHREAD_STACK_SIZE=2MB -sPTHREAD_POOL_SIZE=3 -sFORCE_FILESYSTEM=1 \
  --embed-file "$F/fonts@/fonts" --embed-file "$P/libraries@/bundled" --exclude-file "*.py" -fno-exceptions
ls -l "$SITE"
