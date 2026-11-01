# Adding a parameter: the checklist

From the code audit of 2026-11-02. One new setting touches several places; this is the order that works.

1. `params.json`: **at the very end** of the list (never in the middle: saved projects, automation and skins bind by
   index). A sound-shaping or per-Synth param gets its `s2_` / `s3_` copies from step 3; a shared one goes in `GLOBAL` in
   `presets/make_synths.py`.
2. `src/param_ids.h`: a `P_*` name in the enum and its key in `KEYS`, in the same position. If a default differs from what
   an older sound meant, `state_absent` in `src/synths.h` gives the old meaning, and `OLD_S1` in `presets/make_synths.py`
   keeps the factory sounds as designed.
3. Regenerate, in this order: `python3 ports/one-ish/presets/make_synths.py`, then `presets/make_presets.py`, then
   `python3 tools/gen_vst.py ports/one-ish/vst.json --params-h`. `params.lock` only ever gains lines. `GEN_VST_RELOCK=1`
   moves indexes: don't use it after a release.
4. The window: a control or a page row in `desktop/gen_layout.py`; help text in `desktop/editor_draw.c`. A setting that
   links across Synths: `LINK_GROUPS` in `gen_layout.py`. New hit zones: check the number ranges in `desktop/editor.h` (the
   `_Static_assert` there catches a chain running into an explicit number).
5. The randomiser: a rule in `src/randomize.h`, or none (never touched) and its key in `NEWER` in `test/rnd_golden.c`.
6. Tests: an offline check; a `desktop/load_test.c` click if it's a button. Old sounds must stay identical: `test/run.sh`'s
   golden check. Remake `test/golden_states.txt` only for a deliberate sound change, after checking which sounds differ.
7. Docs: `docs/ONEISH_PRESET_FORMAT.md`, `PLAN.md` (dated, offline or device).
