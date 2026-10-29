# ONE-ish step sequencer

Added 2026-10-27, tested offline (`test/stepseq_check.c` through `test/run.sh`; the window through `load_test.exe` on
Windows), not yet on a device. After the Moog One manual's SEQUENCER chapter (p82-95). Code: `src/stepseq.h` (the
sequence, its text form, the factory sequences), `src/stepseq_run.h` (playing, recording, editing; part of
`src/engine.c`), `desktop/seq_page.c` (the step editor in the window).

## Playing a sequence (desktop window)

- **The SEQUENCER module** (under the centre screen): ON (whether it plays the synth), PLAY (start from step 1 / stop),
  REC, TRIG (NOTE TRIG: plays while a key is held), TRANSP (keys move the sequence instead of sounding), STEP MOD, the
  four PAGE LEDs (the page playing, lit red; click one to edit it), LENGTH and RATE (each step's note value). Its corner
  triangle: every setting (sync, free rate, timing, follow swing and the master swing, note reset, restart on trig and
  on hold, transpose mode, one shot, record mode, the factory sequences).
- **SEQ** (next to MATRIX/MORE/ANALOG/RANDOM) opens the step editor on the screen; SEQ again switches NOTES / MODS.
- **Entering notes:** select a step (click its cell), light REC, play the on-screen keys or a MIDI keyboard: each note
  (or chord: keys held together) goes into that step and the selection moves on. Running, with REC lit, notes land on
  the nearest step (RECORD MODE: Replace, Overdub, Focused on the selected step).
- **Editing with the mouse (NOTES):** a cell is a step of a lane; a note is a box as tall as its velocity and as wide as
  its gate, ticks under it for ratchets, an arrow when tied. Drag a note up/down (velocity) or sideways (gate); the wheel
  moves it a semitone (SHIFT: an octave); double-click an empty cell for a new note; right-click deletes. The values row
  under the grid: NOTE, VEL, GATE, RATCH of the selection (drag or scroll). Buttons: REST, TIE, DELETE, CLEAR (asks),
  DUP (copies steps after the end: SHIFT + click a step number to pick them), SAVE, LOAD. Right-click a step number: rest.
  Arrow keys move the selection, Delete deletes.
- **Modulation lanes (MODS):** each row is a lane: DEST (the mod matrix's list), CTRL (a controller: with AMT 100%, the
  controller at its minimum silences the lane) and an offset per step (drag across the row; right-click removes one).
  STEP MOD makes them from the knobs: with it lit, turning a knob records its move as that step's offset (the playing
  step, or the selected one when stopped) in a lane for that knob's destination; the knob itself stays where it was.
- **Three Synths** (`docs/MULTITIMBRAL.md`, 2026-10-28, offline): each Synth has its own sequencer, settings and steps;
  the SEQUENCER module and the step editor show the Synth in focus. PLAY ALL SEQS on the SETUP page (`seq_all`)
  starts or stops every Synth that is on with its sequencer ON on the same sample.
- **The two presets:** "Seq Bass Line" and "Seq Chord Stabs" (NOTE TRIG and TRANSPOSE on): hold a key and the sequence
  plays from that key; the bass line's cutoff lane comes in with the mod wheel.

## What it does (engine)

- Up to 64 steps (pages of 16), 8 note lanes (a chord takes several), 8 modulation lanes. Per note: note, velocity
  1-127, gate 1-100% of the step, ratchet 1-4, tie. Per step: rest. Per lane: destination, controller, amount, offsets
  -100..+100% per step.
- Notes go through chord memory (keys, arpeggiator, sequencer, then chord, as on the Moog One) and never through the
  arpeggiator: the arp plays the keys you hold, the sequencer its own notes; keys reach the sequencer instead of sounding
  only with NOTE TRIG or TRANSPOSE on (REC lets them sound and records them).
- A tie on the same note carries on with no new attack; into another note the voice moves there legato (mono: legato by
  overlap). A rest keeps its notes and plays nothing.
- Clock: its own, sample-accurate like the arpeggiator's. Synced, the host's tempo; with the transport playing, the
  song position: a looping sequence started with PLAY lines up with the song (step 1 on the song's start), and a loop
  back in the host plays from there. NOTE TRIG and ONE SHOT start where the key or PLAY fell. Unsynced: `seq_rate`.
  FOLLOW SWING (synced) applies the master swing `clock_swing` (only the sequencer follows it so far; the arpeggiator
  keeps its own `arp_swing`).
- NOTE TRIG and PLAY exclude each other (setting one turns the other off; the host is told). ONE SHOT turns PLAY off at
  the end. HOLD keeps a note-trig sequence running with no key; RESTART ON HOLD restarts it with the next key.
- Modulation lanes add to their destination like a matrix slot (voices, or the effect pass for an effect destination),
  the playing step's offset for the whole step; with STEP MOD on and the sequencer stopped, the selected step's (to hear
  it). Nothing applies with ON off.
- ON off with a sequence in it: the same samples as without the sequencer (tested). CPU: see the bench lines of
  `test/run.sh` (the clock and steps alone, and 8 modulation lanes on 8 voices).

## Storage

- **Params** (`seq_*`, `clock_swing`, at the end of `params.json`, section SEQUENCER): the switches and settings, saved
  with presets and projects. PLAY, REC and STEP MOD are not kept in a saved project (it opens stopped). `seq_pos` is a
  text readout ("Step 5/16" running, "Edit 7/16" stopped: the selected step), for the window and later the MPC page.
- **The steps** are engine state: the project's state gets `;seq=<text>` (base64url of a compact binary form: only the
  notes there are, each storing only what differs from the note before). Sizes (offline, 2026-10-27): a typical sequence
  (the 32-step chord stabs, 3-4 lanes and a modulation lane) about 350 characters; the fullest possible (64 steps x 8
  lanes, every field different, 8 modulation lanes full) about 4.2 KB. With the parameters of a normal sound that fits
  MPC's 8 KB chunk; with every parameter off its default (6.4-7.7 KB) and the fullest sequence it does not: the sound is
  then saved without the steps and the readout says "Too long to save" (the window can still save the sequence as a
  file). The chunk's size is the build's `STATE_MAX` (the test reads it): a larger one, as planned for the desktop
  build, keeps everything; MPC stays at 8 KB, so the text stays compact.
- **Presets** can't hold the step text (`presets.json` values are numbers or options), so a preset picks a factory
  sequence with `seq_pattern` (Bass Line, Chord Stabs, Ratchet Run; `src/stepseq.h` builds them). Setting it replaces
  the steps; "User" loads nothing, so presets without a sequence leave yours in place (silent while ON is off).
  `make_presets.py` sets `seq_step_mod` (Off) right after `flt_link`, so a preset's values land even if STEP MOD was on.
- **User patches** (`.oneish`) keep the `seq=` line (docs/PATCHES.md).
- **Sequence files** (`.oneseq`) in `Documents\ONE-ish\Sequences` (or `%APPDATA%\ONE-ish\Sequences`;
  `ONEISH_SEQ_DIR` overrides both), from SAVE / LOAD on the step editor:

```
ONE-ish sequence 1
name=Walking Bass
seq_length=16      the sequence's shape: length, division, timing (the other settings stay as they are)
seq_div=4
seq_timing=0
seq=<text>
```

## How the window edits it

The engine owns the sequence. The window sends commands (`seqcmd` lines, `seqcmd2` / `seqcmd3` for Synths 2 and 3: `note`, `rest`, `clear`, `dup`, `lane`, `off`,
`sel`, `gate`, `rtz`, `load`; `src/stepseq_run.h` `sq_command`) through the plugin's chunk call (`effSetChunk`), which
the wrapper hands to the engine under its per-instance lock, between two audio blocks, the same way as any knob move:
no new lock, no allocation, no file access on the audio thread (files are read and written by the window). It reads the
steps back from the plugin's state (`effGetChunk`) after its own edits and on its 50 ms timer while REC or STEP MOD can
change them, and the playing step from the `seq_pos` readout.

## Not built yet

- OUTPUT to another Synth or all of them [MULTI]: each Synth's sequence plays its own Synth (2026-10-28). Sending it
  elsewhere needs its notes to go to that Synth's voices instead of `out_on`.
- Performance notes with a saved sequence, and sorting the load list by date (the Windows file dialog sorts by name or
  date itself).
- The arpeggiator following the master swing.
- The MPC page (Phase 9): the params are ready; editing steps there needs a page design (and the readout's polling).
