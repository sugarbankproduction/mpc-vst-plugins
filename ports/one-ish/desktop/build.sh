#!/usr/bin/env bash
# Desktop build of ONE-ish: the same engine and wrapper as the MPC plugin, cross-compiled as a 64-bit Windows VST2
# (ONE-ish.dll) to play in a DAW while developing, with its own editor window (desktop/editor.c), plus Windows tests:
#   build/desktop/ONE-ish.dll        the plugin; the engine runs at 44.1 kHz like MPC, converted to the DAW's rate with float
#                                    output (desktop/hostrate.c, resample.c), so any project rate plays in tune
#   build/desktop/load_test.exe      loads the DLL like a DAW does, plays presets, opens the editor (desktop/load_test.c),
#                                    and plays it at other rates and block sizes (desktop/rate_test.c)
#   build/desktop/resample_test.exe  the rate converter alone: its quality, delay and CPU (desktop/resample_test.c)
#   build/desktop/host_test.exe      the repo's wrapper test (tools/host_test.c), built for Windows
# Run from the repo root inside the workbench: win/run.sh bash ports/one-ish/desktop/build.sh
# Then run the .exe files on Windows (the workbench can't run them).
set -euo pipefail
P=ports/one-ish
OUT=$P/build/desktop
CC=x86_64-w64-mingw32-gcc-posix   # the posix-threads flavour: the wrapper uses pthreads
CXX=x86_64-w64-mingw32-g++-posix  # for C++ engine sources (third-party DSP); C files still build as C
eval "$(python3 tools/gen_vst.py "$P/vst.json" --shell)"
python3 tools/gen_vst.py "$P/vst.json" --params-h
mkdir -p "$OUT"
python3 "$P/desktop/gen_layout.py" "$P/params.json" "$OUT/layout.h"   # where each control sits (warns about unplaced params)
# the project chunk (the sound's state) may be far bigger here than the MPC's 8 KB (wrapper/vst2_wrap.c's default):
# the wrapper, the engine (its STATE_MAX), the editor's patches and load_test all take this one size
CHUNK=(-DVST2_CHUNK_MAX=262144)
FLAGS=(-O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I"$P/build" -Iwrapper $CFLAGS "${CHUNK[@]}")
CXXFLAGS=(-O2 -std=gnu++11 -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter -I"$P/build" -Iwrapper $CFLAGS "${CHUNK[@]}")
OBJS=()
LINK=$CC   # g++ links once any source is C++ (it brings libstdc++, static like the rest)
for f in $SOURCES; do
  o="$OUT/obj_$(echo "${f%.*}" | tr '/' '_').o"   # by full path: third-party files share names
  case "$f" in
    *.cc|*.cpp|*.cxx) $CXX "${CXXFLAGS[@]}" -c "$f" -o "$o"; LINK=$CXX ;;
    *) $CC "${FLAGS[@]}" -c "$f" -o "$o" ;;
  esac
  OBJS+=("$o")
done
# the wrapper's entry point is renamed: editor.c's VSTPluginMain wraps it and adds the editor window; and it drives the
# engine through hostrate.c's table (float samples for the rate converter), which passes every call on to the engine
$CC "${FLAGS[@]}" -DVSTPluginMain=one_ish_wrapped_main -Dmpc_engine=one_ish_desktop_engine \
  -Dmpc_engine_changed=one_ish_desktop_changed -c wrapper/vst2_wrap.c -o "$OUT/vst2_wrap.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/hostrate.c" -o "$OUT/hostrate.o"
$CC "${FLAGS[@]}" -c "$P/desktop/resample.c" -o "$OUT/resample.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/editor.c" -o "$OUT/editor.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/editor_draw.c" -o "$OUT/editor_draw.o"
$CC "${FLAGS[@]}" -c "$P/desktop/skin.c" -o "$OUT/skin.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/patches.c" -o "$OUT/patches.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/library.c" -o "$OUT/library.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/settings.c" -o "$OUT/settings.o"
$CC "${FLAGS[@]}" -c "$P/desktop/scala.c" -o "$OUT/scala.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/seq_page.c" -o "$OUT/seq_page.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/more_pics.c" -o "$OUT/more_pics.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/rate_leds.c" -o "$OUT/rate_leds.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/quickmod.c" -o "$OUT/quickmod.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/hub.c" -o "$OUT/hub.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/nav.c" -o "$OUT/nav.o"
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/hub_gens.c" -o "$OUT/hub_gens.o"   # (2026-10-30)
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/cables.c" -o "$OUT/cables.o"   # (2026-10-30) the CABLES view
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/matrix_page.c" -o "$OUT/matrix_page.o"   # (2026-10-30)
$CC "${FLAGS[@]}" -I"$OUT" -c "$P/desktop/parts.c" -o "$OUT/parts.o"         # (2026-10-30) presets, names, libraries
# -static: winpthreads and libgcc go inside the DLL, so it needs nothing else installed
$LINK -shared "${OBJS[@]}" "$OUT/vst2_wrap.o" "$OUT/editor.o" "$OUT/editor_draw.o" "$OUT/skin.o" "$OUT/patches.o" "$OUT/library.o" "$OUT/settings.o" "$OUT/scala.o" "$OUT/seq_page.o" "$OUT/more_pics.o" "$OUT/rate_leds.o" "$OUT/quickmod.o" "$OUT/hub.o" "$OUT/nav.o" "$OUT/hub_gens.o" "$OUT/cables.o" "$OUT/matrix_page.o" "$OUT/parts.o" "$OUT/hostrate.o" "$OUT/resample.o" "$P/desktop/vst2.def" $LIBS -static -lpthread -lgdiplus -lgdi32 -luser32 \
  -lcomdlg32 -lshell32 \
  -o "$OUT/ONE-ish.dll"
$CC -O2 -std=gnu11 -Wall -Wextra "${CHUNK[@]}" -I"$OUT" -I"$P/build" "$P/desktop/load_test.c" "$P/desktop/rate_test.c" "$P/desktop/library_test.c" "$P/desktop/parts_test.c" -static -lgdi32 -luser32 \
  -o "$OUT/load_test.exe"
$CC -O2 -std=gnu11 -Wall -Wextra "$P/desktop/resample_test.c" "$P/desktop/resample.c" -static -lpthread -o "$OUT/resample_test.exe"
$CC "${FLAGS[@]}" -c wrapper/vst2_wrap.c -o "$OUT/vst2_wrap_plain.o"
$CC -O2 -std=gnu11 -I"$P/build" -Iwrapper $CFLAGS -c tools/host_test.c -o "$OUT/host_test.o"
$LINK -O2 "$OUT/host_test.o" "${OBJS[@]}" "$OUT/vst2_wrap_plain.o" $LIBS -static -lpthread -o "$OUT/host_test.exe"
ls -l "$OUT"/*.dll "$OUT"/*.exe
