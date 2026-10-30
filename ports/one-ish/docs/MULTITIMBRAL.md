# ONE-ish multitimbral: three Synths in one instance

Added 2026-10-28, tested offline (`test/multi_check.c` and `test/golden_check.c` through `test/run.sh`; the window
through `load_test.exe` on Windows), not yet on a device. After the manual's description of a preset's three Synths
(p8-9, p14-15, p63-66, p96-101, p129). Code: `src/synths.h` (the instance, key routing, voice pool, MIDI routing,
state), `src/engine.c` (one Synth), `src/fx.h` (`fx_master_multi`), `presets/make_synths.py` (the params of Synths 2
and 3), and the window (`desktop/editor.c`, `editor_draw.c`, `seq_page.c`, `patches.c`).

## Playing it (desktop window)

- **SYNTH 1 / 2 / 3** (console buttons, the keys 1 / 2 / 3 (Shift+1 / 2 / 3: added to the group, 2026-10-29), or Shift+F5 / F6 / F7): the Synth you edit. The lit button and the title line
  (`SYNTH 2 | ...`) show it. Every knob, button, LED, option list, the MORE pages, the matrix, the step editor and the
  randomiser show and change that Synth. Ctrl-click (or Ctrl+F5-F7) adds a Synth: a knob then moves the same control
  on every Synth picked (the last picked is the one drawn).
- **TOGETHER** (2026-10-30, the desktop window; the console's third row, under SYNTH 1): the plain-click way to edit
  several Synths at once. Lit, a click on a SYNTH button adds that Synth to the ones edited together, or takes it out
  (the panel stays on the Synth it shows unless that one goes); off again, back to just the Synth shown. Ctrl-click,
  Shift+1-3 and Ctrl+Shift+F5-F7 light it too. The window's own state, never saved. (load_test: `together_test`.)
- **M | S** beside each SYNTH button (2026-10-29): M switches the Synth off and on (its ON; red while off; saved with the
  sound). S solos it (shortcut: a double-click on the SYNTH button; Shift-click adds a Synth to the ones edited together, 2026-10-30): while any
  Synth is soloed only the soloed ones are heard, the others keep playing silently; never saved (`syn_solo`, kept out
  of states, patches and the randomiser; a 10 ms glide so it never clicks).
- **Shared** sections stay the same whichever Synth is in focus, and say so on the panel: MASTER EFFECTS, DELAY and
  REVERB (the two master effects) are marked SHARED. SYNTH OUTPUT (level, pan, spread) belongs to the Synth, and so
  do the SEND 1 and SEND 2 knobs in the MASTER EFFECTS panel: how much of the Synth in focus goes into each.
- **SETUP** (next to the SYNTH buttons): the three Synths side by side: ON, MIDI CHANNEL, LOWEST KEY, HIGHEST KEY,
  TRANSPOSE, OCTAVE, VOICES, LEVEL, PAN, SEND 1, SEND 2, SYNTH EFFECT, and the shared MIDI IN (ALL), MASTER LEVEL,
  PLAY ALL SEQS, CHORD LEARN mode.
- **Split:** Synth 1 HIGHEST KEY B3, Synth 2 ON with LOWEST KEY C4. **Layer:** both on, full range. Factory
  performances: "Split Bass + Lead", "Layer Pad + Pluck", "Split Bass, Pad + Bells".
- **PATCHES page:** INTO S1/2/3 loads the picked preset or patch into the Synth in focus only (that Synth keeps its
  ON, channel and key range; the other Synths and the master effects stay). SAVE S1/2/3 saves the Synth in focus as
  its own user patch. SAVE and SAVE AS keep the whole instance (all three Synths: a performance).
- **KEY and SCALE per Synth** (2026-10-29): the step editor's GEN view (`docs/SEQUENCER.md`) generates a sequence for
  the Synth in focus in its own key and scale, so the Synths can play in different keys or modes; the other Synths'
  sequences stay as they are.
- **PLAY ALL SEQS** starts (or stops) every Synth that is on with its sequencer ON on the same sample (also the PLAY
  ALL button on the SEQUENCER module's title strip, 2026-10-30).
- **Sequencer OUTPUT** (2026-10-30, offline and the desktop window; `seq_output`, `docs/SEQUENCER.md`): a Synth's
  sequence can play its own Synth (OWN SYNTH, the default), another one, or several (1+2, 1+3, 2+3, 1+2+3): one pattern
  on up to three sounds. The targets play it on their own voices from the shared pool, with their own sound, transpose
  and octave, while they are on; their key ranges don't apply to sequenced notes. The sequence's modulation lanes stay
  with its own Synth. Notes end where they started, whatever changes meanwhile.
- **LINK** (2026-10-30, desktop window): small LINK buttons keep a setting the same in all three Synths. While one is
  lit, changing that setting on the Synth in focus sets the same value on the other two (host automation for each, one
  undo step). LINKs: KEY and SCALE (the GENERATIVE page and the GEN page), the arpeggiator's RATE (under it on the
  panel: its division, timing, sync and free rate), the sequencer's RATE (beside it), each LFO's RATE (in each LFO:
  its rate, sync and timing), LINK SENDS (the MASTER EFFECTS panel: SEND 1 and SEND 2) and the balls and particles
  (every `phys_*` setting; their pages). **ALL 3** above LINK CUTOFF (between the filters) is the one relative LINK:
  turning either CUTOFF moves that cutoff on all three Synths by the same amount, so their offsets stay (a cutoff at
  its end stays there). Lighting one copies nothing by itself: the next change does. They are shared
  params (`link_*`, one copy, no engine effect), so they are saved with the sound (projects, patches, presets, undo)
  and the host can automate them; the randomiser leaves them alone (it follows them: a linked setting it changes
  goes to all three).
- **SHARE** (2026-10-30, offline and the desktop window; `lfo1_share` .. `lfo4_share`, shared, default Off;
  `src/engine.c` `lfo_owner`, `control_lfos`; `test/lfoshare_check.c`): the button beside LINK at the bottom of each LFO
  panel (and on the LFO's MORE page). Lit, that LFO in Synths 2 and 3 *is* Synth 1's: rate, sync, timing, wave and
  every setting on its page (range, polarity, variation, smoothing, start phase, note reset, delay, fades, repeats),
  and in step: the free-running LFO is Synth 1's, the same value at the same moment in all three, so one wobble moves
  all three Synths together. A voice that runs the LFO itself (note reset, repeats, the High range, an LFO the matrix
  modulates) still restarts per note, now with Synth 1's settings, so voices of two Synths started by the same key move
  in step; each Synth's own matrix still reaches its own voices. With Synth 1 switched off it goes on (the first Synth
  sharing it steps it). It is done in the engine, so host automation, MIDI learn and the MPC follow it. The Synths' own
  LFO settings stay stored (and the window shows them while SHARE is lit); SHARE off, each Synth hears its own again.
  Unlike LINK (which copies the rate into the other Synths' own settings), SHARE copies nothing. Saved with the sound;
  the randomiser never touches it (no rule, like the LINKs). Old sounds: Off, unchanged (golden check).
- **GEN OFF** (the console's third row) and **ALL OFF** (the GENERATIVE page) switch every generator off in all three
  Synths at once; pressed again while they are all still off, they bring back the ones they switched off.
- **MASTER** (2026-10-30): the small MASTER button in SYNTH OUTPUT turns that section's VOLUME knob into MASTER, the
  shared master level of all three Synths (`master_level`, also on the SETUP page); off, the knob is the focused
  Synth's own VOLUME again. The button is only a view (never saved).
- **OUTPUT: separate outputs per Synth** (2026-10-30, offline and the desktop DLL; `syn_out`, `src/synths.h`
  `one_ish_render_multi`, `desktop/hostrate.c`; `test/multiout_check.c`, load_test's rate tests). The desktop plugin
  has eight outputs: 1-2 MAIN, 3-4 Synth 1, 5-6 Synth 2, 7-8 Synth 3 (named "Main L" .. "Synth 3 R" for hosts that ask).
  Each Synth's OUTPUT (the SETUP page's OUTPUT row, or SYNTH OUTPUT's MORE page: a plain click) is **Main** (the
  default: on 1-2 with the others, as always), **Own Pair** (only on its own pair) or **Main + Own** (both). Choice made
  (instead of the Moog One's one shared SUB pair): a pair per Synth gives each Synth its own mixer channel in the host,
  and MAIN + one Synth on Own Pair is the instrument's MAIN / SUB split anyway; SUB VOLUME is the host's fader on that
  pair. The **master effects** (FX 1, FX 2) are shared, so their returns always come out on MAIN: a Synth on its own
  pair still feeds them through its SEND 1 and SEND 2 (an aux send on a mixer: its echo and reverb on MAIN, its dry
  sound on its pair); turn its two sends to 0 for a dry pair. The Synth's own synth effect is in its sound, so it goes
  with it. The master level and the limiter apply to every pair. OUTPUT belongs to the Synth's setup, like its MIDI
  channel: saved with the sound and the project, kept by LOAD INTO SYNTH, left out of SAVE SYNTH; the randomiser never
  touches it. With every OUTPUT at Main (every old project and sound) outputs 1-2 are exactly as before and 3-8 are
  silent, so a host that only listens to the first two hears everything. The MPC build (and any stereo caller) has
  one output: every Synth is on it whatever its OUTPUT says.
  **In Carla** (2.5.10): in Continuous Rack mode Carla uses only a plugin's first two outputs (its rack sends outputs
  3 and up to a scratch buffer: `RackGraph::process` in Carla's `CarlaEngineGraph.cpp`), so there everything must stay
  on Main, which is the default and plays as before. To use outputs 3-8, switch Carla to Patchbay mode (Configure
  Carla, Engine, Process mode: Patchbay; Carla restarts its engine), load Moo Won, and in the Patchbay tab connect its
  ports (`output_1` .. `output_8`: Carla names VST2 ports by number and does not read the plugin's names) to the
  Audio Output box or to other plugins: `output_1` / `output_2` (MAIN) to the speakers' left and right, and a Synth's
  pair (e.g. `output_5` / `output_6` for Synth 2) into its own effect or straight to the speakers. Save that as a new
  project (keep `Moo Won.carxp` as it is). In a DAW: enable the plugin's extra outputs (most show them as "Synth 2 L"
  etc.) and give each pair a mixer channel.
- **UNDO / REDO, COMPARE, snapshots and autosaves** (2026-10-29, offline; `docs/PATCHES.md`) work on the whole
  performance: all three Synths, the shared settings and every sequence, whichever Synth is in focus.

## Params (owner's choice A, 2026-10-28)

Every Synth's params are host parameters, so the host automates any Synth directly (the other choice, one set that
follows the focus, would have let automation reach only the focused Synth). 1,529 params in all:

- Synth 1: the 593 keys from before (468 of its own plus the 125 shared), unchanged, in the same slots.
- Synths 2 and 3: 468 each, the same keys with `s2_` / `s3_` in front (`s2_flt_cut`), appended after Synth 1's in
  `params.json` and `params.lock`, named "S2 ..." / "S3 ...". `presets/make_synths.py` writes them; run it (and
  `GEN_VST_RELOCK=1`, `gen_vst --params-h`) after adding a param.
- Shared, one copy (Synth 1's key, mirrored into the others): the master effects (`dly_*`, `rev_*`, `mfx_on`,
  `mfx_series`, `mfx_width`, `mfx_buss`), `clock_swing`, `master_tune`, `fine_tune`, `midi_ch`, `learn_mode`,
  `master_level`, `seq_all`, `gen_follow_tempo` (2026-10-30), `lfo1_share` .. `lfo4_share` (2026-10-30) (`engine_global()` in `src/synths.h`; `is_global()` in
  `make_synths.py` keeps the same list).
- Per Synth, beyond the sound: `syn_on` (all three On since 2026-10-30; before, Synths 2 and 3 started Off), `syn_ch` (1, 2, 3), `key_lo` / `key_hi` (0-127,
  shown as note names), `syn_out` (2026-10-30: Main, Own Pair, Main + Own; the desktop's outputs), and its own `transpose`, `octave_shift`, `voices`, `volume`, `pan`, `mfx_send1/2`, the synth
  effect (`modfx_*`), arp, chord memory and sequencer. A Synth's matrix moves its own params, its synth effect
  included; the master effects' matrix destinations follow Synth 1's matrix.
- The audio input (desktop, 2026-10-29) is one for the instance: every Synth's EXT channel (`ext_level`,
  `ext_route`, its own) and every vocoder (a synth effect or a master slot) hears the same input.

## Voices

One pool of 16 shared by the Synths. Each Synth stays within its own VOICES. When a new note would take the pool past
16, a voice is stolen from any Synth: released voices first (the quietest), then by the new note's Synth's stealing
mode; with No Stealing the note is dropped. A stolen voice fades in a few ms. Unison counts against the pool the same
way.

## MIDI

- `midi_ch` (shared): Omni (every channel: the default, as before), 1-16 (every Synth listens on that channel), or Per
  Synth (each Synth on its own `syn_ch`, Omni or 1-16).
- A note goes to every Synth that is on, listens on its channel and has the key in its range. Its release goes to the
  Synths the key went to, even if the range or channel changed meanwhile, so no note hangs. Controllers, pitch bend and
  pressure go to every Synth on the channel. (2026-10-30, offline: each Synth keeps, per key, the channels it came on,
  and the release on that channel ends it whatever `midi_ch` or `syn_ch` became; before, a channel change while a key
  was down left it hanging. A Synth on Omni lets go on any channel, as before. All Notes Off / All Sound Off on a
  channel a Synth has left ends the notes it started there. `test/robust_check.c` routing.)
- The window's keyboard plays on `midi_ch`, or on the focused Synth's `syn_ch` when it is Per Synth.

## A new instance (2026-10-30, owner's request: "make the default that all 3 synths are turned on")

- A new instance and the Init preset play all three Synths: playing a key layers them (that is how to play all three
  at once; a split or another channel per Synth comes from KEY LO / HI and the channels on the SETUP page). So the layer
  is not three times the same sound, Synths 2 and 3 have an Init of their own: Synth 2 its oscillator an octave down
  (16'), level 45; Synth 3 a triangle an octave up (4'), level 35; Synth 1 is the old Init unchanged. Offline (a
  3-note chord, `test/defaults_check.c`): peak 0.83, about 4 dB louder than Synth 1 alone, no clipping; 3 Synths x a
  chord take 9 of the 16 shared voices, so a 5-note chord already shares the pool (the quietest released voices go
  first).
- Every Synth (new instance and Init) also has the hands wired in matrix rows 13-16: Aftertouch -> Pitch LFO1 Amt
  (vibrato depth, +4: about half a semitone at full pressure), Pad X -> Ladder Cutoff (-90%: to the right darker, up
  to 4.5 octaves), Pad Y -> Ladder Res (+60%), Ribbon -> Pitch (2 semitones each way). At rest (pad at 0, ribbon
  centred, no pressure) they change nothing. LINK SENDS is on.
- Channel pressure reaches every Synth listening on its channel (as the wheels); poly pressure (0xA0) the Synths that
  hold that key, as their (whole-Synth) aftertouch.
- The other factory presets are as designed: single-Synth presets keep Synths 2 and 3 off, with no hands rows.

## State (projects and patches)

- (2026-10-30) The defaults' version: a state, a SAVE SYNTH sound and a patch file written from now on carry `dv=1`
  (after `v=`). One without it was saved with the defaults before 2026-10-30, and as only non-default values are
  saved, what it leaves out is read as those old defaults: Synths 2 and 3 off with Synth 1's sound (but their
  channel), matrix rows 13-16 empty (`legacy_defaults()` in `src/synths.h`; `make_synths.py old_default()` the same
  for the presets). So old projects, Carla projects, patches and presets play sample for sample as before
  (`test/golden_check.c`); an old state with no `s2_syn_on` loads with Synth 2 off, a new one round trips with all
  three on (`test/defaults_check.c`). LOAD INTO SYNTH loads a sound against Synth 1's defaults (not the target's own
  Init); SAVE SYNTH 2 or 3 writes that Synth's own Init values (16', its level) into the sound.

- The engine state stays `key=value;...` (`v=1`, or the compact `v=2` when it would not fit): Synth 1's keys as
  before, then `s2_...` and `s3_...` values that differ from their defaults, then `seq=`, `s2_seq=`, `s3_seq=` for
  the sequences. A project, patch or preset saved before multitimbral loads as Synth 1 with Synths 2 and 3 off, and
  sounds sample for sample as before (`test/golden_check.c`: 37 presets and 24 random sounds).
- Sizes (2026-10-28, offline): the worst case, every param of all three Synths off its default, is 39,506 bytes of the
  desktop's 256 KB chunk; a typical three-Synth performance is about 4 KB.
- The MPC's 8 KB chunk: when the whole state doesn't fit even as `v=2`, Synth 3 is left out, then Synth 2; Synth 1 and
  the shared values always fit (worst case 7,736 bytes). The engine says on stderr which Synths were not saved.
- Commands through the chunk call (as the sequencer's): `seqcmdN` (the sequencer commands for Synth N), `synthloadN`
  (INTO SYNTH: a single-Synth state into Synth N). Readouts: `synth_stateN` (SAVE SYNTH), `voices_total`,
  `voices_sounding`.

## CPU

A Synth that is off and silent costs nothing. On the PC (128-frame block, 2026-10-29, `test/run.sh`'s bench and
`test/multi_check.c` `multi_bench`):

- Synths 2 and 3 off: 1, 4, 8 voices 9.40, 33.91, 67.20 us, against 9.15, 33.69, 66.07 us for the single-Synth engine
  (within 3%). 16 voices: 135 us (8.4 a voice).
- 16 voices of a busy patch with a chorus: 124 us in Synth 1 alone, 134 us shared 6 / 5 / 5 by three Synths with a
  chorus each (+8%: the two extra synth effects). The voices cost the same wherever they are; each Synth that is on
  adds its synth effect.
- The MPC's numbers need `tools/bench.sh` on a device.

## Builds

`ONEISH_SYNTHS` (1 to 3, default 3) sets the Synths per instance at compile time (vst.json `defines`). With 1, the
`s2_` / `s3_` params still exist but do nothing; a state's Synth 2 and 3 values are skipped. Whether the MPC build
gets three Synths (and how its page shows them) is the owner's later call.

## Not built

- Per-Synth outputs (MAIN / SUB as extra VST2 output pairs), SUB VOLUME.
- Setting a key range by pressing LOW RANGE and then a key (the range is set as a value).
- The master effects' matrix destinations follow Synth 1's matrix only.
- Sequencer OUTPUT to another Synth or all of them; the sustain pedal's target Synth.
- Timbre names on the home line. (NRPN for Synths 2 and 3 came with MIDI learn, 2026-10-29: `docs/SETTINGS.md`.)
- The randomiser's undo history starts again when the focus changes.
