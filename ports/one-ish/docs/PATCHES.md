# ONE-ish user patches (desktop window)

Added 2026-10-27, tested offline (load_test.exe on Windows), not yet on a device. Code: `desktop/patches.c`,
`desktop/patches.h`.

## Using them

- **The name on the centre screen** shows what is playing: `F 004  Acid Line` is factory preset 4, `U  Bright Pad`
  is one of your own, `-  Other` is a sound that is no longer saved anywhere (its patch was deleted, or a project from
  before this feature). A `*` after the name means the sound has changed since it was loaded or saved.
- **`<` `>`, PREV / NEXT and the PRESET knob** step through the 27 factory presets, then your patches A to Z.
- **Click the name** (or click the PRESET knob) for the PATCHES page: factory presets on the left, yours on the right,
  7 at a time (`<` `>` on its title line, or the mouse wheel, for more). Click one to load it. Click the name again to
  go back to the matrix.
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

## Where they live

`Documents\ONE-ish\Patches` (the Documents folder Windows reports, also when it is moved to OneDrive), made on the first
save. If Documents is missing or can't be written, `%APPDATA%\ONE-ish\Patches`. The environment variable
`ONEISH_PATCH_DIR` overrides both (the tests use a temporary folder). One file per patch, `<name>.oneish`; copying,
backing up or deleting the files in Explorer works too (the folder is read again when the window or the PATCHES page opens, and on each step). A file whose name
breaks the rules above is not listed; IMPORT it to give it a usable name.

## The file

Plain text, one `key=value` per line:

```
ONE-ish patch 1
name=Bright Pad
author=...        (optional, kept when the patch is saved over)
category=...      (optional, kept when the patch is saved over)
flt_cut=42.5
o2_wave=0
...
```

- The first line is the header, with the format's version. IMPORT needs it; loading tolerates a file without it.
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
- The step sequencer's steps, when there are any, are one more line, `seq=<text>` (the engine's own form, see
  `docs/SEQUENCER.md`): a patch keeps its sequence, and loading a patch without the line empties the sequencer
  (2026-10-27, offline). A typical sequence adds a few hundred characters.
- Synths 2 and 3 (`docs/MULTITIMBRAL.md`): their values are lines with `s2_` / `s3_` in front of the key
  (`s2_flt_cut=40`), their sequences `s2_seq=` / `s3_seq=`. A patch without them has Synths 2 and 3 at their defaults
  (off), so every patch saved before 2026-10-28 loads as before. A three-Synth patch is about 4 KB (2026-10-28,
  offline).

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
