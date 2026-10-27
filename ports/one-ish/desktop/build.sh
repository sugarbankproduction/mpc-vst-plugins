#!/usr/bin/env bash
# Desktop build of ONE-ish: the same engine and wrapper as the MPC plugin, cross-compiled as a 64-bit Windows VST2
# (ONE-ish.dll) to play in a DAW while developing, with its own editor window (desktop/editor.c), plus Windows tests:
#   build/desktop/ONE-ish.dll        the plugin; the engine runs at 44.1 kHz like MPC, so set the DAW project to 44.1 kHz
#   build/desktop/load_test.exe      loads the DLL like a DAW does, plays presets, opens the editor (desktop/load_test.c)
#   build/desktop/host_test.exe      the repo's wrapper test (tools/host_test.c), built for Windows
# Run from the repo root inside the workbench: win/run.sh bash ports/one-ish/desktop/build.sh
# Then run the two .exe files on Windows (the workbench can't run them).
set -euo pipefail
P=ports/one-ish
OUT=$P/build/desktop
CC=x86_64-w64-mingw32-gcc-posix   # the posix-threads flavour: the wrapper uses pthreads
eval "$(python3 tools/gen_vst.py "$P/vst.json" --shell)"
python3 tools/gen_vst.py "$P/vst.json" --params-h
mkdir -p "$OUT"
python3 "$P/desktop/gen_layout.py" "$P/params.json" "$OUT/layout.h"   # where each control sits (warns about unplaced params)
FLAGS=(-O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I"$P/build" -Iwrapper $CFLAGS)
OBJS=()
for f in $SOURCES; do
  o="$OUT/$(basename "${f%.*}").o"
  $CC "${FLAGS[@]}" -c "$f" -o "$o"
  OBJS+=("$o")
done
# the wrapper's entry point is renamed: editor.c's VSTPluginMain wraps it and adds the editor window
$CC "${FLAGS[@]}" -DVSTPluginMain=one_ish_wrapped_main -c wrapper/vst2_wrap.c -o "$OUT/vst2_wrap.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/editor.c" -o "$OUT/editor.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/editor_draw.c" -o "$OUT/editor_draw.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/patches.c" -o "$OUT/patches.o"
# -static: winpthreads and libgcc go inside the DLL, so it needs nothing else installed
$CC -shared "${OBJS[@]}" "$OUT/vst2_wrap.o" "$OUT/editor.o" "$OUT/editor_draw.o" "$OUT/patches.o" "$P/desktop/vst2.def" $LIBS -static -lpthread -lgdiplus -lgdi32 -luser32 \
  -lcomdlg32 -lshell32 \
  -o "$OUT/ONE-ish.dll"
$CC -O2 -std=gnu11 -Wall -Wextra -I"$OUT" "$P/desktop/load_test.c" -static -lgdi32 -luser32 -o "$OUT/load_test.exe"
$CC -O2 -std=gnu11 -I"$P/build" -Iwrapper $CFLAGS tools/host_test.c $SOURCES wrapper/vst2_wrap.c $LIBS -static -lpthread -o "$OUT/host_test.exe"
ls -l "$OUT"/*.dll "$OUT"/*.exe
