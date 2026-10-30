# ONE-ish user patches (desktop window)

Added 2026-10-27, tested offline (load_test.exe on Windows), not yet on a device. Code: `desktop/patches.c`,
`desktop/patches.h`; undo, COMPARE, snapshots, tags and notes, performance sets and MIDI program change (2026-10-29,
offline): `desktop/library.c`, `desktop/library.h`, tested by `desktop/library_test.c` inside load_test.

## Using them

- **The name on the centre screen** shows what is playing: `F 004  Acid Line` is factory preset 4, `U  Bright Pad`
  is one of your own, `-  Other` is a sound that is no longer saved anywhere (its patch was deleted, or a project from
  before this feature). A `*` after the name means the sound has changed since it was loaded or saved.
- **The PRESET box's `<` `>`** (inside the box round the name, labelled PRESET on its top edge, since 2026-10-30;
  BACK / FWD at the box's two ends only change the page the screen shows, never the sound), **PREV / NEXT and the
  PRESET knob** step through the factory presets (296 on 2026-10-30: the first 40, bank A, then bank B), then your patches A to Z. With
  PRESET AUTOLOAD off (SETTINGS page, `docs/SETTINGS.md`, 2026-10-29) they only point (`> F 004  Name ?`); a click on
  the PRESET knob or the name then loads it.
- **Click the name** (or click the PRESET knob) for the PATCHES page: factory presets on the left, yours on the right,
  8 at a time (the PAGE `<` `>` at its title line's right end, or the mouse wheel over the list, for more). Click one to load it. Click the name again to
  go back to the matrix.
- **Category tabs** (2026-10-30, desktop window; load_test `category_tabs`): two lines of tabs over the list, ALL and
  every category a factory preset or user patch has (BASS, LEAD, PAD, KEYS, STRINGS, BRASS, BELLS, PLUCK, ARP, SEQ, DRUM, FX,
  PERF, GEN, ORGAN ...; made from the tags, so a new category shows up by itself), each with how many patches it lists. A plain
  click lists only that category, from its first page; ALL lists everything. A tab is the CATEGORY filter of the TAGS
  page (the same one: the TAGS page shows it, CLEAR clears it), and the other filters (TYPE, MOOD, GROUP) still apply
  to the list and to the counts.
- **The buttons on that page:**
  - SAVE: stores the sound over the current user patch. On a factory preset or an unsaved sound it asks for a name
    (factory presets are never changed). It lights up while there are changes to keep.
  - SAVE AS: stores the sound under a new name; asks before replacing a patch with the same name.
  - RENAME, DELETE: act on the current user patch. DELETE asks first and removes the file; the sound stays loaded
    (`-  name *`), so SAVE AS can still keep it.
  - IMPORT: picks a `.oneish` file anywhere (from a friend, a backup) and copies it into your patches.
  - EXPORT: writes the sound to a `.oneish` file wherever you choose.
  - INTO S1/2/3 (the number is the Synth in focus): while lit, the preset or patch you click loads into that Synth
    only; it keeps its ON, MIDI channel and key range, and the other Synths and the master effects stay as they are.
    From a patch or preset with several Synths, its Synth 1 is loaded. Click INTO again to go back to loading whole
    sounds (2026-10-28, offline).
  - SAVE S1/2/3: saves the Synth in focus alone as a new user patch (asks for a name), as Synth 1's keys, so it loads
    into any Synth later. SAVE and SAVE AS keep all three Synths (a performance) (2026-10-28, offline).
- Names: up to 32 plain characters; not `\ / : * ? " < > | ;`, no space at either end, no dot at the end. Upper and
  lower case count as the same name (as in Windows).
- Messages (saved, deleted, a file that couldn't be read or written and why) show for a few seconds where the help
  text is.
- **Tabs on the title line** of the PATCHES page lead to the library's other pages: SET, ALL SETS, ARRANGE (performance
  sets), SNAPSHOTS and TAGS (2026-10-29, offline).
- **The home line** under the patch name shows its tags (TYPE, CATEGORY, MOOD, GROUP), `NOTES` when it has notes, and
  the host's tempo when the host reports one (2026-10-29, offline).

## Factory bank A (2026-10-30, offline)

`presets/banks/bank_a.py`: 128 presets after the original 40, tagged for the browser: 32 Bass, 32 Lead, 20 Keys,
16 Brass, 16 Pluck and 12 Organ (ORGAN is a new category). Most are Single (Synths 2 and 3 off); 11 are Layers or
Splits across Synths 2 and 3. One controller plan throughout: the mod wheel opens basses up, gives leads and brass
vibrato, moves keys (tremolo) and speeds the organs' rotor; aftertouch opens, swells or adds vibrato; velocity
brightens; pad X shapes the timbre and pad Y the space or dirt; the ribbon bends leads and sweeps bass cutoffs. Each
preset's one-line description is in the file (`ABOUT`).

Loudness: `LEVEL` in the file trims every preset's volume so the bank plays at one level (past 100% volume, a matrix row
Constant > Amp Level adds the rest). `test/bank_check.c` (in the offline checks, with bank B's; alone: `offline_check banka
<dir>/demo.wav`, `bankb` bank B, `banks` both) plays each preset a phrase for its category (bass line, melody, chords; a split adds bass notes, a
sequenced preset holds a key), then again with the wheel and aftertouch full, and fails on a peak above -1 dBFS,
silence, non-finite samples or a loudness 6 dB away from the bank's median. 2026-10-30, offline: median -18 dB, every preset within
-21 .. -17.3 dB (the loudest 300 ms of RMS; the lowest, Strung Air, uses the MACRO string model, which varies by a
few dB from run to run), loudest peak -2.0 dBFS; CPU (x86, -O2) 36 us per 128-frame block on average, 122 us at most (a
layer); `bank_a_demo.wav` in `test/out` has them all.

## Undo, COMPARE, snapshots (2026-10-29, offline)

- **UNDO / REDO** (the console's UNDO and REDO, 2026-10-30, at the right of its third row: hovering one names the step
  it would take back or do again; Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z in the window; the UNDO and REDO buttons on the
  SNAPSHOTS page): every
  edit made in the window is a step: a knob, a button, an option list, the matrix, the SETUP page, the step editor, the
  randomiser, and loading a preset or patch (undoing a load brings back the sound and its name). A knob drag is one
  step, so is a burst of mouse-wheel clicks (a step once the wheel rests 0.4 s) and a RANDOMISE or MUTATE. The last 100
  steps are kept. A step is the whole performance (all three Synths and their sequences). Changes the host makes
  (automation) are not steps. The randomiser keeps its own UNDO / REDO too.
- **COMPARE** (console, under PREV): plays the saved version of the patch; COMPARE again: back to your edit. The name
  line says `COMPARE`. Loading another sound ends it (the edit is autosaved); turning a knob while comparing edits the
  saved version, and the edit COMPARE was hiding is kept as an autosave. SAVE, SAVE AS, SAVE S1/2/3 and EXPORT first
  bring the edit back, so they save the edit.
- **SNAPSHOT** (console, under NEXT) opens the SNAPSHOTS page: TAKE keeps the sound as it is now with a note (asked
  for); click a snapshot to select it, then LOAD, NOTES (change its note), DELETE (asks first), SAVE OVER (loads it and
  saves it over the patch it was taken of; of a factory preset: asks for a name), SAVE NEW (loads it and saves it as a
  new patch). The newest are first, 8 at a time.
- **Autosave:** when another sound loads (from the list, PREV / NEXT, the PRESET knob, INTO, a snapshot, a set, a MIDI
  program change, the host's program list from its window thread) and the sound has unsaved changes (`*`), a snapshot
  marked `AUTO` is made first ("Autosave before loading ..."). The newest 32 autosaves are kept, older ones deleted;
  snapshots you TAKE are never deleted. An autosave that would be the same as the newest snapshot is not made.

## Tags and notes (2026-10-29, offline)

- **The save box** (SAVE AS, and SAVE of a sound that isn't a user patch yet) asks for the name, TYPE (Single, Layer,
  Split, Multi; it starts from what the Synths do: one on Single, key ranges apart Split, all overlapping Layer, else
  Multi), CATEGORY, MOOD, GROUP and notes. SAVE over a user patch keeps its tags and notes. A sound saved from a factory
  preset starts with that preset's tags (the factory presets carry ONE-ish's own tags: since 2026-10-30 they come from
  presets.json's `tags`, which `presets/make_presets.py` writes along with `desktop/factory_tags.h`, the window's copy).
- **The TAGS page:** four columns, TYPE, CATEGORY, MOOD and GROUP. A click on a tag filters the PATCHES page (and PREV
  / NEXT) by it, one tag per column (again: off; the column title says `(filter)`); a small square marks the current
  patch's tags. TAG PATCH puts the selected tag on the current user patch (again: off); ADD (asks a name, into the
  selected tag's list, CATEGORY when none), RENAME and DELETE (asks first) change the CATEGORY, MOOD and GROUP lists,
  and every patch file that carries the tag; CLEAR takes the filters off; NOTES writes the current patch's notes; SHOW
  NOTES shows them in the help area of every page.
- Tag names: up to 24 plain characters, not `; = | , \`.

## Performance sets (2026-10-29, offline)

- 128 sets of 64 patches each, banks A-H x numbers 1-8 (a factory preset or a user patch in each).
- **SET** (the current set): the bank buttons A-H show that bank's eight patches below (bank preview: nothing loads; SHOW BANK PREVIEW off on the SETTINGS page shows only the numbers; a
  dark letter is an empty bank). Click a number to load its patch (the number lights); **hold it 0.6 s to put the
  current patch there** (a sound that isn't saved must be saved first; an edited patch is assigned as saved). ASSIGN and
  CLEAR act on the selected number; `<` `>` step through the sets.
- **ALL SETS:** every set with how many patches it holds; a click makes it the current set.
- **ARRANGE:** click a set, then UP / DOWN (move it), COPY (a copy after it), DELETE (asks first; its patches stay),
  NAME, NOTES; ADD makes a new empty set at the end (asks for a name). The first set ("Set 1") is made when none exist.
- A number that points at a user patch that was renamed or deleted says so when clicked.

## MIDI program and bank change (2026-10-29, offline)

The host's MIDI reaches user patches and sets:

- Bank select MSB (CC 0) **0**, LSB (CC 32) **n**, then a program change **p**: position 128 x n + p of the list,
  factory presets first, then your patches A to Z (with 296 factory presets, bank 2 program 40 is your first patch: 256 + 40).
- MSB **1**, LSB **n**, program **p** (0-63): performance set n + 1, number p (0 = A1, 7 = A8, 8 = B1 ... 63 = H8).
- Other MSBs are ignored. Any MIDI channel. The host's own program list (`effSetProgram`) still picks factory presets.
- The program change is noted on the host's thread (no file touched there) and loaded on the window's thread: by the
  window's timer, or, with the window closed, by a hidden window's timer (made when the plugin instance is made).

## Where they live

`Documents\ONE-ish\Patches` (the Documents folder Windows reports, also when it is moved to OneDrive), made on the first
save. If Documents is missing or can't be written, `%APPDATA%\ONE-ish\Patches`. The environment variable
`ONEISH_PATCH_DIR` overrides both (the tests use a temporary folder). One file per patch, `<name>.oneish`; copying,
backing up or deleting the files in Explorer works too (the folder is read again when the window or the PATCHES page opens, and on each step). A file whose name
breaks the rules above is not listed; IMPORT it to give it a usable name.

## The file

Plain text, one `key=value` per line:

```
ONE-ish patch 2
name=Bright Pad
author=...        (optional, kept when the patch is saved over)
type=Single       (the tags and notes, optional, kept when the patch is saved over)
category=Pad
mood=Warm
group=Live
notes=Slow attack.\nMod wheel: vibrato.
flt_cut=42.5
o2_wave=0
...
```

- The first line is the header, with the format's version. IMPORT needs it; loading tolerates a file without it.
- **Versions.** 1 (2026-10-27): the name, `author=`, `category=` (free text), then the parameters. **2 (2026-10-29)**:
  adds `type=`, `mood=`, `group=` and `notes=` beside `category=`, written after the name line. Notes are one line:
  `\n` is a new line, `\s` a `;` and `\\` a backslash. A version-1 file loads as before (its `category=` is its
  CATEGORY); a version-1 reader (an older ONE-ish) skips the new lines, because they are not parameters.
- After it, the parameters as the engine's own state holds them (`get_state` in `src/engine.c`): the parameter key from
  `params.json`, its value in the parameter's units, an option as its number (its label, e.g. `o2_wave=Tri`, works
  too). Only values that differ from the defaults are written, and loading starts from the defaults, so a missing key
  is the default and a patch made today still loads after new parameters are added. Values out of range are clamped.
- Unknown keys (a parameter a later version adds, or one renamed) are skipped quietly. Lines that make no sense (no
  `=`, a value that isn't a number or an option) are skipped and counted; the screen says how many. `;` also separates
  entries, so a pasted engine state (`v=1;flt_cut=42.5;...`) loads. Files over the state limit plus 64 KB are refused.
- The engine's state limit applies: the desktop build's chunk, 256 KB (`VST2_CHUNK_MAX` in `desktop/build.sh`; the MPC's
  is 8 KB, where a state that would not fit is written in the compact "v=2" form). The offline check measures the worst
  case, every parameter off its default (its output gives the bytes), and fails when it leaves less than 15% of the
  desktop chunk free or no longer fits the MPC's in either form (2026-10-28, offline). A state too big for the chunk is
  never saved cut short: nothing is saved, the engine says so on stderr and the window on its screen.
- A Synth's microtuning (2026-10-29, `docs/SETTINGS.md`), when it has one, is one or two more lines, `tun_scl=` and
  `tun_kbm=` (`s2_` / `s3_` for Synths 2 and 3), in the engine's compact form (`src/tuning.h`).
- The step sequencer's steps, when there are any, are one more line, `seq=<text>` (the engine's own form, see
  `docs/SEQUENCER.md`): a patch keeps its sequence, and loading a patch without the line empties the sequencer
  (2026-10-27, offline). A typical sequence adds a few hundred characters.
- Synths 2 and 3 (`docs/MULTITIMBRAL.md`): their values are lines with `s2_` / `s3_` in front of the key
  (`s2_flt_cut=40`), their sequences `s2_seq=` / `s3_seq=`. A patch without them has Synths 2 and 3 at their defaults
  (off), so every patch saved before 2026-10-28 loads as before. A three-Synth patch is about 4 KB (2026-10-28,
  offline).

## The library's other files (2026-10-29)

All beside the patches (so `ONEISH_PATCH_DIR` moves them too), plain text, written to `<file>.tmp` and then moved over
the old file:

- `Snapshots\<yyyymmdd-hhmmss-mmm>[-auto].oneishsnap`: `ONE-ish snapshot 1`, then `time=`, `of=<F|U|N>,<program>,<name>`
  (the patch it was taken of), `auto=0|1`, `notes=` (escaped as above), then the state one `key=value` per line as in a
  patch file (the sequences included). The file names sort by time; `-auto` marks an autosave.
- `tags.oneishtags`: `ONE-ish tags 1`, then `category=`, `mood=`, `group=` lines, one tag each, in list order (TYPE's
  four are fixed). Missing: the default lists (CATEGORY Bass, Lead, Pad, Keys, Strings, Brass, Bells, Pluck, Arp,
  Sequence, Drum, FX, Performance, Generative; MOOD Bright, Dark, Warm, Soft, Aggressive, Dirty, Clean, Evolving; GROUP Favourites, Live,
  Studio, Work in progress). A category or mood a factory preset has is added to the lists when they are read
  (2026-10-30), so an older file still lists the new banks' tags.
- `sets.oneishsets`: `ONE-ish sets 1`, `current=<set number>`, then per set `[set]`, `name=`, `notes=` (escaped) and one
  line per filled number, `A1=U,0,Bright Pad` or `B3=F,3,Acid Line` (a factory preset by name first, its program
  number if the name is gone).

## How it works

- Everything runs on the window's thread; no file is touched on the audio thread.
- Factory presets load through the host's program list as before (`effSetProgram`). Picking the program the wrapper
  already has (it ignores a re-pick) puts the preset's values back from the editor instead.
- A user patch is parsed into a clean engine state and restored the way a host restores a project (`effSetChunk`:
  defaults, then the values, all at once), then the host is told about every parameter that changed
  (`audioMasterAutomate` inside begin/end edit, as a knob does), so the host and the window stay in step. Restoring as
  one state rather than one `setParameter` at a time keeps every value exact and stops LINK CUTOFF from dragging one
  cutoff while the other loads.
- The project the host saves (`effGetChunk`) gets `;ui_patch=<edited>,<F|U|N>,<program>,<name>` appended (up to 96 bytes) when it
  fits the chunk; the engine ignores the key, and the window uses it on reopening to show the same patch name and `*`.
- The `*`: the editor keeps every parameter's value from the moment the patch was loaded or saved and compares.
- Undo, COMPARE and snapshots use the engine's whole state (`effGetChunk` / `effSetChunk`, as a project), so they cover
  every parameter of the three Synths and the sequences, and nothing in the engine changed for them. The editor reports
  to the library before an edit (`set_value`, the step editor's clicks and keys, the randomiser, loads) and when it ends
  (mouse up, a one-shot edit, 0.4 s after the wheel); a step is kept only if the state changed. A step holds the state,
  which patch it was, its saved version and the values the `*` compares with (a few KB; at most 100 undo and 100 redo
  steps per instance). If a host picks a program or restores a project from another thread, the library only notes
  it: the saved version and the tags are fetched on the window's thread later.

## On the MPC later (not built)

The MPC build has no window, so the same feature would live in the engine and its touchscreen page:

- `create(data_dir)` gives the plugin a data folder; patches would be `<data_dir>/patches/<name>.oneish` in this same
  format (it is the engine's own state, so the parser can move into `engine.c` unchanged in meaning).
- Browsing: list the user patches as extra VST programs after the factory ones (the wrapper's program list would ask
  the engine for their names and count, like the `programs` mode does), so MPC's PRESET menu shows them; or a page
  parameter "Patch" with the names as a dynamic list.
- Saving: a momentary "Save" parameter on the page that writes the current state under the current name, plus a name
  picked from a short list ("User 01" ...) or built with a letter stepper, since MPC can't type into a plugin. File
  writes must happen off the audio thread (the wrapper's housekeeping or a worker), and the device stages files as
  `x.new` then `mv`.
- Rename, delete, import and export are easier from a computer: the files are plain text in the data folder.
