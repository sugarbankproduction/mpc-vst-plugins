# ONE-ish: Moog One-inspired MPC Plugin — Build Plan

Oct 27, 2026 · @Christian

## Goal and scope

Build ONE-ish: a native MPC OS plugin that captures what makes the Moog One special (three oscillators, two filters, a deep mod matrix, multitimbral parts) inside the CPU budget of a standalone MPC.

**In scope**

- A virtual-analog voice built from scratch: 3 oscillators, a ladder filter and a state-variable filter, envelopes and LFOs
- A mod matrix with 8 to 16 slots
- 4 to 8 voices of polyphony, or mono if the CPU budget demands it
- Up to 3 multitimbral parts, if the budget allows
- A touchscreen page and Q-Link mapping

**Out of scope**

- A cycle-accurate copy of the Moog One. It has 16 voices of fully modelled analog circuitry, which no standalone MPC can run.
- Moog's code, firmware or factory presets. Sound design is modelled from the public manual, listening and openly published circuit maths.
- The Moog or Moog One name. The trademark stays out of the plugin name, UI and release notes.

## Platform and constraints

ONE-ish is built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (Open MPC), which uses the Linux VST2 host already built into MPC OS. Development and testing happen on a computer; the MPC is only needed at the end.

| Constraint | What it means for ONE-ish |
| --- | --- |
| VST2 only, Linux ARM (armhf) | Write the engine in C/C++ from scratch or from open-source code; existing Mac/Windows plugins can't be converted |
| Audio is 44.1 kHz in 128-sample blocks | The engine renders 128 samples at a time |
| About 2.9 ms per block on Gen1, shared with the whole project | Every feature is benchmarked on the device; voice count is set by measurement, not by wish |
| Touchscreen page = knobs, buttons, text, images | No live waveform or envelope drawing; pre-drawn images can follow a parameter |
| Install needs root SSH on the MPC | Only modded units can load plugins |
| Gen2 units (incl. MPC Key 37 G2) are reported more locked down | Running on your own MPC is not guaranteed yet |
| A plugin crash takes MPC down | Defensive code: no allocation or file I/O in the audio path |

**Repo features to use:** the porting kit (`vst.json` + `tools/build_port.sh`), the offline test (`tools/test_port.sh`), the on-device CPU bench (`tools/bench.sh`), Skin Studio for the page, and the repo's own Claude skill and CLAUDE.md.

## Sound architecture

Each voice mixes three oscillators and noise, runs them through a ladder filter and a multimode filter, then an amp. The mod matrix can reach any of these.

```
Oscillator 1 ─┐
Oscillator 2 ─┤                ┌─> Ladder filter (4-pole, resonance, drive) ─┐
Oscillator 3 ─┼─> Mixer ───────┤        (series or parallel)                 ├─> Amp (VCA) ─> Output
Noise ────────┘ (levels, sync) └─> Multimode filter (SVF: LP / BP / HP) ─────┘
      ▲                                     ▲                                  ▲
      └──────────────── Mod matrix (8–16 slots: source → destination × amount) ┘
                                     ▲
             Mod sources: 3 envelopes, 2–4 LFOs, velocity, mod wheel, aftertouch
```

The ladder filter is the signature sound, so it gets the most care and CPU. Multitimbral parts (stretch goal) are up to 3 copies of this voice on separate MIDI channels, sharing one voice pool.

## Build phases

Each phase ends with something you can hear and a passing offline test. Don't start the next until the current one sounds right.

1. **Hello synth.** Build and load the repo's `poc/synth.c` example unchanged, to prove the toolchain works.
   - [x] Docker build runs; `tools/test_port.sh` passes (2026-10-27, offline: `synth.c` built for armv7 and aarch64 and rendered to a WAV; `test_port.sh` and `build_port.sh` run on `poc/inputprobe`, because `synth.c` is a hand-written plugin the test can't load)
2. **One oscillator, one voice.** A band-limited saw and square (PolyBLEP), pitch from MIDI notes, an amp envelope.
   - [x] No aliasing whistle on high notes (2026-10-27, offline: `ports/one-ish/test/run.sh` measures aliasing 26-34 dB below the notes, 15-18 dB less than a plain oscillator; the faint remainder on the very top notes is PolyBLEP's limit; revisit with oversampling when the ladder filter (Phase 4) needs it)
3. **Three oscillators.** Per-oscillator wave, octave, fine tune, level; hard sync; noise source; a mixer.
4. **Ladder filter.** A 4-pole Moog-style ladder with resonance and drive, based on an openly published model.
   - [x] Self-oscillates cleanly at full resonance; stays stable at all cutoffs (2026-10-27, offline: a sine within 1 cent of the cutoff from 110 Hz to 8 kHz, everything else 57-62 dB down, no level drift; no blow-up in any mode or slope at full resonance and drive. Added from the manual: LP/HP mode and 24/18/12/6 dB slopes)
5. **Second filter.** A state-variable filter (low/band/high/notch), with series or parallel routing to the ladder.
6. **Envelopes and LFOs.** 3 envelopes (filter, amp, mod) and 2 to 4 LFOs with tempo sync from MPC's transport.
7. **Mod matrix.** 8 to 16 slots of source → destination → amount.
8. **Polyphony.** A voice allocator with voice stealing; measure the cost per voice and set the voice count.
   - [ ] `tools/bench.sh` shows PASS at the chosen voice count
9. **Page and Q-Links.** Lay out the touchscreen page in Skin Studio; map the 16 most useful controls to Q-Links; add presets in `presets.json`.
10. **Multitimbral parts (stretch).** 2 to 3 parts on separate MIDI channels, sharing the voice pool.
11. **Release.** Package with `tools/release.py`; test on a device (yours if Gen2 is unlocked, or a Gen1 tester from the Discord).

## Working with Claude Code

Start from a fork of mpc-vst-plugins so Claude Code gets the repo's own skill and CLAUDE.md for free, then add this plan as `PLAN.md`.

**Setup**

1. Fork `sd88me/mpc-vst-plugins` on GitHub and clone it.
2. Install Docker (the build uses the `arm32v7/gcc:11-bullseye` image) and Python 3.
3. Save this file in the repo root as `PLAN.md`.
4. Create `ports/one-ish/` for the engine, its `vst.json` and its skin.

**Add to the repo's CLAUDE.md**

- Read `PLAN.md` first; work one phase at a time and stop at each phase's tests.
- Explain changes in plain language; I'm not from a developer background.
- Never allocate memory, lock or touch files in the audio render path.
- Benchmark every new feature's CPU cost before moving on.
- Keep Moog names, code and presets out of the project.

**First prompt to paste**

```
Read CLAUDE.md, PLAN.md and docs/PORTING.md. Then do Phase 1 of PLAN.md: build poc/synth.c, run tools/test_port.sh, and explain in plain language what each step did and what passed.
```

## Where we are (resume here)

Updated 2026-10-29. Owner's instruction: build ONE-ish as a **local desktop VST2 first** and test it; port and adjust
for MPC afterwards. Work through the steps without stopping for approval at each one.

- **Done and pushed (branch `one-ish` on the owner's fork):** Phases 1-8 offline; fixes from the phase 6-8 code
  review; presets in the program list (`presets/make_presets.py` -> `presets.json`). The desktop `ONE-ish.dll` is
  installed in `%USERPROFILE%\VST2` and plays in Carla.
- **Now (owner, 2026-10-27): match the Moog One's features, and give the desktop VST a front-panel window laid out
  like the instrument** (original artwork, no maker's names or marks; reference photos in `DOWNLOADS/`, not
  committed). `ports/one-ish/docs/MOOG_ONE_AUDIT.md` lists every Moog One feature against ONE-ish (2026-10-29: 418 have,
  16 partial, 41 missing, 27 of those hardware-only) and the build order, worked through in this order:
  1. Tier 1, the sound: done 2026-10-27 (offline; 180 params, 21 presets): dual-wave oscillators with linear FM and
     ring mod; glide (LCR/LCT/EXP, legato, gated, glissando); unison (count, detune) and pitch variance; envelope
     extras (delay, hold, curves, loop, latch, sync, time scale, velocity amount, multi trig, reset); the SVF pair
     (spacing, series/parallel), link cutoff, key tracking to +-200%, filter FM. About 7.7-8.2 us per voice per block
     on the PC; filter FM on both filters about 11.4. Built by parallel agents in worktrees and merged. Left for later:
     envelope MIN time and the 13-value sync list, unison stacks that re-divide as keys are added, link cutoff in
     presets.
  2. The front-panel window, laid out from the photos, with the keyboard, wheels and X/Y pad: done 2026-10-27, with
     per-module MORE pages on the centre screen, a 16-row matrix and a ribbon strip (CC 16 position, CC 17 touch).
     A photographed-instrument look added 2026-10-28 (desktop, offline pictures only): `desktop/skin.c` shades brushed
     aluminium frames, black anodised panels, ash wood, display glass, lit 3D knobs, push buttons, LEDs, keys and wheels
     per pixel (no image files); the static layer is made once per window size, text and sprites are cached, and only
     what changed is repainted. On the PC (with the sequencer strip): full repaint 10-12 ms, a knob turned about
     0.5 ms, the first paint about 0.5 s (making the static layer); GDI handles stable over 20 opens. The SEQ page and
     the SEQUENCER strip use the same parts (`skin_led` has a red LED for the playing page). Not yet seen in Carla by
     the owner. Owner's follow-ups (2026-10-28, offline): polished silver pitch/mod wheels; square window corners
     (Windows 11 rounds top-level windows: the editor asks for DWMWCP_DONOTROUND on the host's window when that window
     holds only the editor, never a bigger one; load_test checks both; the wood cheeks' ends are square with a
     slight bevel). Pictures and indicators (audit package 7, 2026-10-29, desktop, offline pictures): MORE pages for the
     three envelopes (the shape with its curves), the mixer (a bar graph of levels and filter buses) and the filters
     (routing, modes and response curves), the LFO pages' wave with its variation (`desktop/more_pics.c`); rate LEDs
     by the LFOs', arp's and sequencer's RATE and a TEMPO LED (`desktop/rate_leds.c`: only an LED repaints, 0.05 ms);
     the open-source effects' 14 params on the synth effect's page by type. Full repaint 8-10 ms, a knob 0.5 ms.
  3. Tier 2: done 2026-10-27 (offline): LFOs 3-4 and their options, 16-slot matrix with transforms and the missing
     sources/destinations (every effect control too, from global sources), noise colours and noise envelope,
     voice/keyboard options, stereo voices and pan, analog drift, the assignable ribbon. 402 params, 27 presets.
     Saved states hold only non-default values (worst case 6.2 of 8 KB).
  4. Tier 3: the full effect list (original designs; the Eventide reverbs are licensed, so original reverbs in their
     spirit). Done 2026-10-27 (offline): a synth effect slot with 15 types and CTRL 1/2, master delay (stereo or
     ping-pong, L/R times, offset) and reverb (plate, room, hall; size, freeze) on two sends with series and width; 3.1 MB
     of buffers; 1.5-7 us per block per synth type, 3.6 delay, 6-18 reverb on the PC. Left: vocoders (no mic input),
     shimmer and blackhole reverbs (CPU), per-type effect presets. Then (audit package 10, 2026-10-27, offline and the
     desktop build): two master effect slots, each any of 23 types (the reverbs, now with original Shimmer and
     Blackhole-style designs, and the manual's delays, choruses, flangers, phasers, bit crusher, resonator), BUSS MODE
     Dual Mono / Stereo, each slot's own MIX, GAIN and controls (slot 1 `dly_*`, slot 2 `rev_*`), the manual's L/R
     feedback and modulation, band decays and levels, Infinite, DISTANCE, the room's detents, the crusher's crossover;
     the synth effect gains Stereo Delay and Ping Pong. 564 params, 158 effect destinations; 4.3 MB of effect buffers;
     the factory presets sound as before (within 1 LSB). The window's pages for the new controls done 2026-10-29 (desktop):
     each slot's MORE page shows only its type's controls, no OTHER page left. Left: effect presets. Tier 4: arpeggiator and chord memory done 2026-10-27
     (offline): the manual's arp (Up/Down/Order/Random, 1-4 octaves up/down/up-down/down-up, octave order, pendulum
     once/twice, random repeating or not, synced 1/1-1/128 straight/dotted/triplet or a free 10-280 BPM, gate, swing,
     latch (and HOLD), note reset) and chord memory (CHORD LEARN as an on/off param, first key the root; 14 built-in
     shapes; applied after the arp), sample-accurate, following the host's song position (`HAS_SONG_POS`); `arp_*`,
     `chord_*`; 437 params, 29 presets. Next: a decision with the owner on the sequencer and multitimbral parts.
  5. After Tier 2/3 (owner, 2026-10-27): an Analog Character section, all 0 by default: per-voice component
     differences (cutoff, envelope times, level, slight detune), drift, and a non-linear ladder if the CPU allows.
     Done 2026-10-27 (offline): `an_cut`, `an_res`, `an_env`, `an_lvl`, `an_tune`, `an_ladder` with `drift` in the
     ANALOG section, preset "Vintage Poly Pad"; the non-linear ladder costs about +21% per voice on the PC.
  6. Later (owner, 2026-10-27): the open-source Plaits macro-oscillator models (Mutable Instruments, MIT licence:
     keep its copyright notice) as an extra oscillator mode. To check first: C++ in the MPC build, its 48 kHz
     tuning at 44.1 kHz, and the CPU per voice of each model on the device.
     Done 2026-10-27 (offline and the desktop DLL): a MACRO mixer channel per voice with all 24 Plaits v1.2 models
     (`mac_*` params, MACRO OSC section; matrix destinations Macro Harmonics / Timbre / Morph / Level; presets "Macro
     String", "Macro Wave Pad", "Macro FM Keys"). Vendored unmodified in `ports/one-ish/third_party/` with two patched
     headers (44.1 kHz constants; one UBSan fix) listed in its `THIRD_PARTY.md`. In tune within 0.1 cent offline; level 0
     (the default) gives the same samples and CPU as before. On the PC a model adds 0.5-2x a busy voice's CPU (Particles
     about 2x); needs `tools/bench.sh` on the device before choosing the default voice count with it in use.
  7. Later (owner, 2026-10-27): a patch randomiser for inspiration: a "random" button that makes playable sounds
     (ranges kept musical: audible levels, open-enough cutoffs, sensible envelopes), a "mutate" amount for small
     variations of the current sound, per-section locks (e.g. keep the envelopes), undo, and later per timbral part.
     Done 2026-10-27 (offline and the desktop window): `src/randomize.h` (rules per param key, six archetypes, a
     safety pass) and a RANDOM page on the centre screen (RANDOMISE, MUTATE with an amount, 7 section locks, UNDO /
     REDO over 32 sounds). Offline: 300 random sounds all heard and within the limiter's range; volume, tuning, bend,
     keyboard settings, ribbon and link cutoff never touched. Left: an MPC control. Per part done 2026-10-29 (the
     desktop window, load_test): it works on the Synth in focus; ALL 3 SYNTHS (lit) gives every Synth its own new sound
     (ON, channels, key ranges and levels stay); keys R randomise, M mutate, Shift+R / Shift+M all three.
     More variety done 2026-10-29 (offline and the desktop window; the owner found the results too tame): a WILD bar
     (0-100%, a window setting like AMOUNT; 0% gives exactly the sounds it gave before). As it rises RANDOMISE and
     MUTATE use more matrix rows (often all 16 at 100%), any source to any destination (the effects', sends' and LFOs'
     too; the matrix has no arp or GENERATIVE destinations) with VIA and transforms, LFOs in the High range, odd filter
     routings, FM, ring and sync, extreme envelopes, unusual effect types, bigger option switches; MUTATE's moves grow
     with it. The safety pass still keeps every sound heard, finite and in the limiter's range (WILD also keeps effect
     feedback lower and counts unison and level rows in the loudness). Offline, the average sound at WILD 0 / 50 /
     100% has 2.2 / 7.2 / 12.0 matrix rows and 93 / 127 / 171 params away from their defaults (200 sounds each, all
     heard and safe). Three tick buttons, ARPEGGIATOR, GENERATIVE and SEQUENCER (off by default): lit, that section is
     randomised too (arp on, pattern, octaves, division, gate, swing; Euclid, Turing, KEY and SCALE; the sequencer's
     on, division, length, swing, triggering and transposing by the keys (only if it has notes) and its modulation
     lanes). The sequencer has no play-direction setting. The steps' notes too (wired at the merge, 2026-10-29):
     RANDOMISE writes a pattern with the sequence generator (`seq_random_notes`, desktop/seq_page.c: random GEN options,
     wider with WILD, in the KEY and SCALE just picked), MUTATE keeps the notes or with probability AMOUNT makes new ones;
     load_test checks every note is in key. The randomiser's UNDO does not bring back the sequence (Ctrl+Z does).
  8. Later (owner, 2026-10-27): generative features inside the plugin (it plays its own voices, so it works even
     though MPC ignores plugin MIDI out): Euclidean rhythms, probability/random note and step generators, a
     Turing-machine-style looping random sequence, with the arpeggiator (Tier 4) as their base. Done 2026-10-27
     (offline), `gen_*`: a Euclidean rhythm (1-32 steps, hits, rotation; Bjorklund), step probability, velocity
     randomness, and a Turing machine (length 2-16, change 0-100%, the held keys or 8 scales over 1-4 octaves). With
     the arp off, Euclid plays the held chord on its hits. Left: saving a locked Turing loop with the preset, ratchets.
  9. Later (owner, 2026-10-27): open-source effects to port, MIT-licensed only unless the owner decides otherwise
     (GPL code such as Surge XT, Vital or Dragonfly would make ONE-ish GPL): Mutable Instruments Clouds/Beads
     (granular), Rings (resonator), Elements (modal), Warps (cross-modulation), Marbles (random generator, for the
     generative features); Airwindows (consoles, tape, saturation, reverbs). Check each file's licence header and the
     CPU on the device first.
     Done 2026-10-27 (offline and the desktop build), all MIT: Rings as the "Modal Resonator" and Clouds as the
     "Granular" synth effect types (`src/fx/mi_effects.{h,cc}`, sources in `ports/one-ish/third_party/`), Airwindows
     ToTape6, Density and Console7 as "Tape Color", "Density", "Console" and Galactic as the "Space" reverb
     (`src/fx/airwindows.h`, matched against the original plugins' output); 14 params, 9 matrix destinations, presets
     "Modal Bells", "Grain Cloud", "Tape Bass". No added memory (they use the slot's own buffers). PC cost per block:
     Rings up to 42 us (modal), Clouds 12-73 us by mode (spectral in bursts). Left: their CPU on the device, a place
     for the 14 params on the desktop window and the MPC page; Beads, Elements, Warps, Marbles.
  10. The Moog One's step sequencer (owner, 2026-10-27). Done 2026-10-27 (offline and the desktop window;
     `ports/one-ish/docs/SEQUENCER.md`): 64 steps in pages of 16, 8 note lanes (note, velocity, gate, ratchet 1-4, tie)
     with rests, 8 modulation lanes (destination, controller and amount, an offset per step), STEP MOD from the knobs,
     note trig, transpose (start key / interval), one shot, restart on trig / hold, follow swing (a master swing
     `clock_swing`), divisions and timing, sync to the host's song position; step and real-time recording (replace,
     overdub, focused); 20 `seq_*` settings, the `seq_pos` readout and `clock_swing`; the steps saved in the state
     (`seq=`, about 350 characters for a typical sequence), in user patches and `.oneseq` files; a SEQ step editor and a
     SEQUENCER module in the window; presets "Seq Bass Line", "Seq Chord Stabs". PC cost: the clock and steps alone
     0.1 us per block, 8 modulation lanes 0.24 us per voice. Left: OUTPUT to Synth 1/2/3 [MULTI] (the RTZ button since: 18), the
     MPC page (the audit's section 17 and package 2 list the small leftovers).
  11. After the Windows version is finished (owner, 2026-10-27): a web version to share with friends: the engine
     compiled to WebAssembly in an AudioWorklet, a browser panel generated from the same layout data, Web MIDI, patches
     in the browser (download/share as files), static hosting on Vercel. Build and test locally first; deploying
     publishes it, so only on the owner's go-ahead.
  12. Voices, MIDI and three Synths (audit packages 9, 8 in part, and 13). Owner's decision (2026-10-28, option A):
     every Synth's params are host parameters, so a host automates any Synth (about 1,400 per-Synth params rather than
     one set following the focus). Done 2026-10-28 (offline and the desktop window; `ports/one-ish/docs/MULTITIMBRAL.md`):
     16 voices and unison that shares them out as keys are added; `midi_ch` (Omni, 1-16, Per Synth), silent chord
     learn, OCTAVE held for the default octave then a panic; three Synths per instance, each a whole sound with its own
     arp, chord memory, sequencer and synth effect, its own ON, MIDI channel, key range, transpose, octave, voices,
     level, pan and two sends into the shared master effects; one 16-voice pool; SYNTH 1/2/3 focus buttons (Ctrl for
     several, Shift/Ctrl+F5-F7), a SETUP page, INTO S1/2/3 and SAVE S1/2/3 on the PATCHES page, PLAY ALL; three factory
     performances. 1,529 params (468 per Synth, 125 shared); Synth 1's keys unchanged, so old projects, patches and
     presets play sample for sample as before (`test/golden_check.c`). State: worst case 39.5 KB of the desktop's
     256 KB; on the MPC's 8 KB Synths 3 then 2 are left out, Synth 1 always kept. CPU (PC, per 128-frame block,
     2026-10-29): Synths 2 and 3 off cost nothing measurable (1, 4, 8 voices 9.40, 33.91, 67.20 us against 9.15, 33.69,
     66.07 before: within 3%); 16 voices 135 us (8.4 a voice); 16 voices of a busy patch with a chorus 124 us in Synth 1
     alone, 134 us shared 6/5/5 by three Synths with a chorus each (+8%: two more synth effects).
     Left (solo and the M | S buttons done 2026-10-29; sequencer OUTPUT 2026-10-30, item 27): per-Synth outputs, the sustain pedal's target, a range set by
     pressing a key, NRPN; three Synths on the MPC is the owner's later call (`ONEISH_SYNTHS`).
  13. The library (audit packages 5, 12a, 12b). Done 2026-10-29 (offline and the desktop window; `desktop/library.c`,
     `docs/PATCHES.md`): UNDO / REDO of any edit from the window (knobs, buttons, lists, matrix, SETUP, step edits, the
     randomiser, loads; a drag or a wheel burst is one step; 100 steps; Ctrl+Z / Ctrl+Y), COMPARE and SNAPSHOT on the
     console (where the console's name plate was), the SNAPSHOTS page and autosaves of unsaved edits (newest 32), all
     over the whole state of the three Synths; tags (TYPE, CATEGORY, MOOD, GROUP) and notes in the save box, the TAGS
     page (filters, editable lists, TAG PATCH, NOTES, SHOW NOTES), the home line with tags and the host's tempo (patch
     format version 2; version 1 still loads); 128 performance sets of 64 (SET with A-H / 1-8, a long press assigns, bank
     preview; ALL SETS; ARRANGE); MIDI bank select and program change reach user patches and sets (a hidden window loads
     them when the editor is closed). No engine change and no new params. Left: the library's parts (12c: effect and
     matrix presets, sequences), user spaces and bulk export (12d). (PRESET AUTOLOAD and SHOW BANK PREVIEW: on the SETTINGS page, item 16.)
  14. Audio input and the EXT channel (audit package 11, first half). Done 2026-10-29 (offline and the desktop DLL): the
     desktop plugin has a stereo audio input (two VST2 inputs, declared by the desktop layer: the shared wrapper and the
     MPC build are unchanged). The host's rate is converted to 44.1 kHz (`desktop/hostrate.c`, the rate converter run
     the other way) and each engine block gets its input; the input is heard a fixed 128 samples late at 44.1 kHz (about
     223 at 48 kHz), not reported as latency, so notes play exactly as before. The mixer's EXT channel per Synth:
     `ext_level` (0 by default) and `ext_route` (None, SVF, Ladder, Both) into every sounding voice's filters, on the
     MIXER's MORE page; the matrix destination "Ext Level". Level 0 or no input connected gives exactly the old samples
     (golden check). CPU (PC): the EXT channel about 1 us a voice per block while heard (8 voices 49.1 against 56.9 us), none at level 0 or silent; the input converter at 48 or 96 kHz about 108 us per 512-frame host block, nothing
     while the input is silent. In Carla (Continuous Rack: the rack's plugins run in series, the first one fed by
     Carla's input): either add Carla's internal "Audio File" plugin above ONE-ish in the rack and load a WAV, or connect
     a capture device to `Carla:AudioIn1/2` in the external patchbay; then raise EXT IN LEVEL (the MIXER's MORE page)
     and hold a key.
  15. The vocoders (audit package 11, second half). Done 2026-10-29 (offline and the desktop DLL): 10 Band Vocoder and
     16 Band Vocoder as synth effect types and in either master slot, an original filter-bank design
     (`src/fx/vocoder.h`): the audio input is the modulator, the voices (or the send) the carrier; MIX, SIBILANCE,
     SENSITIVITY (a gate on the input), TONE and NOISE as the manual lists (`modfx_sibil`, `modfx_sens`; the master
     slots' `dly_*` / `rev_*`; CTRL 2's Auto target is SIBILANCE). No input gives silence (and no work). CPU (PC, per
     128-frame block): about 12 us (10 bands), 18-19 us (16 bands). Demo: `test/out/vocoder_demo.wav` (a synthetic voice,
     then a chord played by it through each vocoder). To try it in Carla: route a voice recording into ONE-ish as above,
     set SYNTH EFFECT TYPE to 16 Band Vocoder (the MORE page has its controls), MIX 100%, hold a chord.
  16. Settings and MIDI (audit packages 8, 14a, 16). Done 2026-10-29 (offline and the desktop window;
     `ports/one-ish/docs/SETTINGS.md`): a SETTINGS page (console button next to SETUP; GLOBAL, LIBRARY, TUNING) whose
     settings live in `Settings.txt` beside the Patches folder, never in a sound: PRESET AUTOLOAD (off: PREV/NEXT and
     the PRESET knob point, a click loads), SHOW BANK PREVIEW (the SET page), RECEIVE MIDI CLOCK with an own tempo, MIDI I/O FROM PRESET (off: loads keep
     the MIDI channels), the sustain / EXP 1 / EXP 2 pedal CC numbers, RESTORE DEFAULT SETTINGS, the version and the
     folders. Defaults behave exactly as before. No CPU cost (nothing in the audio path but a pedal number lookup).
     MIDI learn on the page's MIDI tab (LEARN, click a control of the Synth in focus, move a CC; 7- or 14-bit; the map
     with 7/14-BIT, DELETE, CLEAR ALL and the reserved CCs), kept in the settings file and sent to every engine as a
     fixed 128-entry table; NRPN input after the manual's table for all three Synths (the wrapper's own param-index NRPN
     is off for ONE-ish). A learned CC or an NRPN moves its param as host automation would, and the host is told.
     Microtuning on the TUNING tab: a Scala `.scl` scale and `.kbm` keyboard map per Synth, read by the window, kept
     in the sound (state, patches, SAVE / INTO SYNTH); master tune and fine tune still apply; tested offline with
     12-TET (identical samples), 19-EDO and just intonation with a keyboard map (`ports/one-ish/test/scales/`).
     Left: MIDI out of the plugin's notes (package 14b); the sustain pedal's target Synth; the owner's check in Carla.
  17. Modulation extras (audit package 3). Done 2026-10-29 (offline and the desktop window; `test/modx_check.c`): the
     manual's step-mod destinations in the matrix and the sequencer's lanes (oscillator octave, the noise EG's attack,
     sustain and release, each envelope's multi trig, sync, loop and latch, each LFO's wave; 275 destinations), an
     ENABLE per matrix slot (click a row's number on the MATRIX page), PAD POLARITY, X/Y HOLD and LOAD SAVED POS (the
     wheel and pad saved with the sound; CTRL page). Old sounds play sample for sample as before; CPU on the PC within
     2% of before (8 voices 67.4 against 66.4 us, A/B on the same machine, 2026-10-29).
  18. LFO and envelope fidelity (audit package 4). Done 2026-10-29 (offline; `test/lfoenv_check.c`): LFO RANGE High
     (1 Hz-1 kHz, run per sample on pitch, both cutoffs and the amp level), the LFO wave "Pulse" (width over 0-100%),
     synced LFOs restarting at their start phase when the host transport starts, and EG TIMES "MIN, 13 Syncs" (an
     attack, decay or release knob at its far left is MIN, 0.2 ms, which time scale leaves alone; synced times from the
     manual's 13 values). Old sounds unchanged (the default EG TIMES keeps the old times). CPU on the PC: unchanged with
     no LFO in High (8 voices 68.0 us); one LFO in High on pitch adds 3.2-3.7 us per voice (two runs; 89.4 to 114.9 us for 8 voices),
     LFOs 1 and 2 in High on pitch and both cutoffs 5.1-6.4 us per voice (130.4 us).
  19. The sequencer's leftovers (audit package 2). Done 2026-10-29 (offline and the desktop window;
     `test/seqx_check.c`, load_test): RTZ and AUTO (step edit auto-scroll) on the step editor's title line; ALL (edit a
     step's notes together) and DEL STEP; performance notes saved with a .oneseq sequence; the arp following the master
     swing (`arp_follow_swing`); the host transport's start and stop starting and stopping the sequence
     (`seq_host_ss`, RECEIVE START/STOP). No CPU cost. Left: OUTPUT to another Synth [MULTI], the MPC page.
  20. The sequence generator (owner's request). Done 2026-10-29 (offline and the desktop window; `src/seqgen.h`,
     `test/seqgen_check.c`, load_test; `ports/one-ish/docs/SEQUENCER.md`): GEN on the step editor writes a sequence for
     the Synth in focus in its own KEY and SCALE (each Synth its own key or mode): STYLE (bass, arp, lead, stabs,
     random), RHYTHM (straight, syncopated, euclidean), DENSITY, RANGE, VARIATION (a first-bar motif that repeats and
     wanders, chord progressions from the scale, phrases ending on the root), ACCENT, FILLS (ties, ratchets) and MODS
     (cutoff, resonance and a fourth lane); KEY, SCALE, LENGTH and SEED there; GENERATE and RE-ROLL, one undo step each.
     The generator is pure (a seed in, a pattern out) for the randomiser to call later. The window runs it; the engine
     is unchanged (no CPU cost, no new params).
  21. Bouncing balls and particles (the GENERATIVE section, beyond the Moog One). Done 2026-10-29 (offline and the
     desktop window; `src/physgen.h`, `src/physgen_run.h`, `test/physgen_check.c`, load_test): per Synth, 1-8 balls
     bounce in a box (SPEED, GRAVITY, BOX SIZE, BOUNCE); the floor plays a degree of the Synth's KEY and SCALE by where
     a ball lands, the ceiling an octave up, the walls the root and the fifth; a pool of 32 particles falls from an
     emitter (RATE, SPREAD, LIFE, ATTRACTION) and plays crossing a line, colliding or dying. Shared: MAX NOTES/S (a cap),
     DYNAMICS, NOTE LENGTH, SYNC to the host tempo (1/4 .. 1/32, triplets), FREE or HELD KEYS (from the lowest key),
     KEY RESTARTS, LOW OCTAVE, OCTAVES (20 `phys_*` params per Synth). Notes go the Euclid / Turing way (`play_step`),
     deterministic from a seed per Synth; positions are never saved. Two MORE pages (BOUNCING BALLS, PARTICLES; in the
     MORE index) show the box moving, read from the engine's `phys_view` through a seqlock (no lock). Old sounds play
     sample for sample as before (golden check). CPU on the PC per 128-frame block: the world alone 0.04 us (8 balls),
     1.0 us (32 particles colliding); the engine with 4 held voices 24.8 us, plus 8 balls and 32 particles playing into
     the voices 28.1 us. Off: no work at all. Left: the MPC's 8 KB chunk overflows in the every-param-changed test (8424
     bytes, the lead's call); a panel button for the pages; an optional modulation output of the particles.
     Extended 2026-10-30 (offline and the desktop window; `test/physgen_check.c` physgen2_checks, load_test): each
     wall (FLOOR, CEILING, LEFT, RIGHT) PLAYS (as before), is SILENT (bounces, no note) or OPEN (a ball leaves and a
     new one comes in; particles leave), drawn bright / dim / dashed in the box. PHYS KEYS adds KEY LAUNCH (each key
     throws in a ball, BALLS at most, oldest replaced, and a burst of 3-12 particles by velocity) and KEY EMIT (a ball
     per key held; particles stream while keys are held at RATE x velocity / 64); each ball and particle plays from its
     own key, in the KEY and SCALE. FOLLOW OCTAVE: with FREE keys LOW OCTAVE follows the window's keyboard octave
     buttons (the window sends `kboct` through the state, like its sequencer edits; runtime only, never saved). Eight
     new matrix sources at the end of the list, per Synth, 0..1 and smoothed at control rate: BALL 1 HEIGHT, BALL 1 X,
     BALLS SPEED, BALL HIT, PARTICLE COUNT, PARTICLE HIT, TURING VALUE, EUCLID GATE; the bipolar flags are now 64 bits
     (an assert and a polarity check). 5 new `phys_*` params per Synth. Old sounds unchanged (golden check). CPU on the
     PC per 128-frame block: the world alone 0.04 us (8 balls), 1.0 us (32 particles); the engine with 4 held voices
     24.9-25.2 us, plus 8 balls and 32 particles 28.3-29.1 us (before: 24.8 / 28.1, the same within noise); with a
     silent floor, an open wall and 3 slots from the new sources 35.6-37.3 us, against 36.3-44.3 us for the same 3
     slots from LFO 1 / Mod Env / Velocity: the new sources cost nothing beyond an ordinary slot.
  22. Quick assign: modulation from the front panel (audit package 6, owner's request). Done 2026-10-30 (the desktop
     window; `desktop/quickmod.c`, load_test): pick a source with the DEST button in the title strip of LFO 1-4, the
     three envelopes or NOISE (Noise EG), or Alt-click / right-click its part of the panel: NOISE (Noise EG, Random,
     Random Uni), the pitch and mod wheels, the X/Y pad (X, Y, gate), the ribbon, the keyboard (velocity, aftertouch,
     the key, KB gate, sustain, sostenuto, EXP 1, EXP 2, constant), GENERATIVE (any source added to the matrix's list
     later; none yet). The part lights amber; the help line says what to do. Then click a knob (or a switch or LED list
     the matrix can move, or a MORE page item): it takes the row with the same source and destination, else the first
     row with no destination, in each Synth in focus (Ctrl-picked Synths each their own row); the drag sets the amount,
     up positive, down negative, 1% a pixel (Shift 0.2%), shown live on the help line and under the knob; the knob's
     own value stays. Esc or DEST again ends it. Ctrl-drag a modulated knob later for its amount. Thin rings around the
     modulated knobs show each connection's amount from the knob's setting (both ways for LFOs, the key, random, the
     pitch wheel, the ribbon), coloured by family: envelopes amber, LFOs blue, the hands' controllers green, random
     violet, later sources pink; a dot on a modulated switch. Hover: "modulated by ..."; right-click: each connection
     with REMOVE and OPEN IN MATRIX (the row marked). A full matrix says so on the help line. The MATRIX page's params
     are the only truth; every change is told to the host like a knob move; a connection and its drag, or a remove, is
     one undo step. No engine change, no CPU cost in the audio path.
     Destinations: 101 of the panel's 194 controls. Knobs: LFO RATE and WAVE; oscillator OCTAVE, FREQUENCY, BEAT FREQ,
     WAVE ANGLE, PULSE WIDTH, MIX; the pitch, wave and FM mod amounts; NOISE ATTACK, RELEASE, SUSTAIN, COLOR MIX; every
     mixer level (macro too); both cutoffs, resonances, SPACING, MIX, DRIVE, the cutoffs' EG, LFO 2 and FM amounts; each
     envelope's six stages, KEY SCL (time scale) and its MULTI, SYNC, LOOP, LATCH; the synth effect's CTRL 1, CTRL 2,
     MIX; SEND 1, SEND 2, SERIES, WIDTH; master FX 1 TIME, FDBK and FX 2 DECAY, SIZE; VOLUME (the matrix's Amp Level),
     PAN, SPREAD. MORE page items with a destination too (LFO variation, delay, fades; every effect control; drift;
     the macro channel's harmonics, timbre, morph; the EXT level). Knobs the matrix can't move: ARP GATE and SWING,
     GENERATIVE STEPS, HITS, ROTATE, CHANCE, unison DETUNE, sequencer LENGTH, the cutoffs' KEY TRK, the envelopes' A, D,
     R CURVE and VEL AMT, GLIDE RATE, BEND UP and DOWN; the rest are on/off switches and option lists with no
     destination (waves of the oscillators, sync, routes, modes, types, the arp, chord and sequencer buttons). Matrix
     destinations with no knob of their own: Pitch (all oscillators) and each LFO's Rate (st). Left: EDIT MODULATIONS
     (the matrix filtered by a source); rings on the MORE pages' items; the MPC page.
  23. Rain and ripples, Markov melody (the GENERATIVE section, beyond the Moog One). Done 2026-10-30 (offline and the
     desktop window; `src/gens_b.h`, `src/gens_b_run.h`, `test/gens_b_check.c`, load_test): per Synth. RAIN: drops fall
     on a pond at a RATE (0.2-10 a second, uneven like rain; SYNC puts the notes on the tempo grid); each landing plays
     a degree of the KEY and SCALE by where it falls, its size its velocity (DROP SIZE SPREAD), and starts a ring
     (RIPPLE SPEED, RIPPLE LIFE); where two rings first meet, or a ring reaches one of the STONES (0-6), a softer echo
     may play (ECHO: how likely and how loud; 0 none). Pools of 16 drops and 32 rings. 13 `rain_*` params. MARKOV:
     learns from the keys played into the Synth (LEARN on; while it plays or not; chords and pauses left out): the step
     from note to note as scale degrees (IN KEY, else semitones) after the last one or two degrees (ORDER 1/2), and its
     gaps (1-8 steps of RATE); ON, it improvises on the RATE grid (1/4 .. 1/32, triplets) with CREATIVITY (0: always
     the most likely step; up to 50% a draw by temperature; above it also wanders), RHYTHM learned / straight / sparse,
     NOTE LENGTH (a share of the gap), OCTAVES around where the player played, the player's velocity. It starts from a
     gentle built-in table (small steps, leaning home to the root) so it plays before learning; FORGET clears it.
     12 `mkv_*` params. Shared: FREE / HELD KEYS (from the lowest key), MAX NOTES/S, notes through `play_step` (CHORD
     works), deterministic from a seed per Synth. The learned table is runtime state, not saved with the sound: about
     4.7 KB of counts per Synth (more than a whole sound's state, and the MPC chunk is already full), and a sound should
     not change with whoever played into it last. Four matrix sources appended: Rain Drop, Ripple Energy, Markov Step,
     Markov Height (0..1). Two MORE pages (RAIN AND RIPPLES, MARKOV MELODY; page ids RAIN, MARKOV) show the pond and the
     melody's last notes and choice moving (`rain_view`, `markov_view`, a seqlock as the balls'). Old sounds play sample
     for sample as before (golden check). CPU on the PC per 128-frame block (2026-10-30): the pond alone 1.7 us (10
     drops a second, 32 rings, 6 stones, every echo), one Markov choice 0.15 us; the engine with 4 held voices 26.1 us,
     both off with every setting moved 25.0 us (no cost), with the rain playing into the voices 40.8 us (mostly the
     extra voices its notes and echoes sound), with the Markov melody on 1/32 28.5 us. Left: the matrix's bipolar mask
     is 32 bits (30 sources now; others adding sources past 32 must widen it); the MPC chunk (25 more params a Synth).
  24. Front-panel conveniences (owner's request). Done 2026-10-30 (offline and the desktop window; `desktop/hub.c`,
     load_test, `test/midi_check.c`): the window is 56 units taller (856) for a third console row and a third line in
     the bottom row. TEMPO on the console: a readout with HOST / OWN that is the SETTINGS page's RECEIVE MIDI CLOCK and
     TEMPO (one setting, two places); its < > (a click), a drag or the wheel set an own tempo (Shift 0.1 BPM).
     Offline: with the host at 60 BPM and OWN at 150, the arp, the sequencer and the balls run at 150 and every Synth's
     synced LFOs, envelopes and effects follow it. LINK buttons (eleven shared `link_*` params, saved with the sound, no
     engine effect): KEY, SCALE, the arp's RATE, the sequencer's RATE, each LFO's RATE, the FX SENDS, the balls /
     particles, and ALL 3 above LINK CUTOFF (relative: both cutoffs move by the same amount in all three Synths); lit,
     an edit on the Synth in focus goes to all three (host automation, one undo step). Every feature works with a
     plain click; Shift-click on GENERATIVE (GEN OFF) and the arrow keys are extra shortcuts. GENERATIVE (console button): a hub page
     of the generators (data-driven from the MORE pages: EUCLID, TURING, BOUNCING BALLS, PARTICLES, and LIFE, ORBITS,
     PEND, RAIN, MARKOV once they exist) with each one's ON and the Synths it is on in, KEY / SCALE with their LINKs,
     and ALL OFF; GEN OFF beside the button: every generator off in all three Synths (one undo step), again: back on.
     MASTER in SYNTH OUTPUT: lit, its knob is the shared master level (a view, not saved). New MORE triangles and
     pages: PITCH MODULATION, RING MOD, WAVEFORM MOD, FREQ MOD, NOISE, CUTOFF MODULATION, SYNTH OUTPUT, BEND RANGE;
     BALLS and PARTICLES (their ON and triangle) in GENERATIVE. The screen's sub-pages: the < > on the title line, and
     now the keys Left / Right (PgUp / PgDn) everywhere but the step editor; the < > by the patch name, PREV / NEXT
     and the PRESET knob step presets as before. No CPU cost (nothing in the audio path).
  25. (2026-10-30) GAME OF LIFE, GRAVITY ORBITS, PENDULUMS (the GENERATIVE section, beyond the Moog One). Done
     2026-10-30 (offline and the desktop window; `src/gens_a.h`, `src/gens_a_run.h`, `test/gens_a_check.c`, load_test):
     per Synth, in its KEY and SCALE, notes the Euclid / Turing way (`play_step`, so CHORD works). LIFE: a 16 x 8 grid
     (rules Life, HighLife, Seeds; wrap edges), stepping at 1/4 .. 1/32 (triplets) or a free rate; BIRTHS, SCAN COLUMN or
     SCAN ROW play (row or column the degree), NOTES PER STEP, DENSITY, SEED and a RESEED button (a dead or looping
     grid seeds itself again). ORBITS: 1-6 planets round a sun (SPEED, GRAVITY between the planets, ECCENTRICITY,
     SPREAD), 1-8 spokes (a spoke a degree; PLANETS AS octave or velocity), SYNC to a grid. PENDULUMS: 2-12 pendulums,
     pendulum i swinging SWINGS + i x SPREAD times per CYCLE of bars at the host's tempo (they meet again at each cycle's
     end), playing at the centre, the ends or both, DAMPING, KEY RESTARTS, DYNAMICS. Each: FREE / HELD KEYS, MAX
     NOTES/S, NOTE LENGTH, LOW OCTAVE (42 `life_*`, `orb_*`, `pend_*` params per Synth). Five matrix sources at the end
     of the list (Life Population, Orbit 1 X, Orbit 1 Y, Pendulum 1, Pendulum Sync, 0 .. 1); the matrix's bipolar flags
     are now 64 bits. Three MORE pages (GAME OF LIFE, GRAVITY ORBITS, PENDULUMS; ids LIFE, ORBITS, PEND) show each moving,
     read from `life_view`, `orbit_view`, `pend_view` through seqlocks. Deterministic (LIFE and ORBITS from a seed per
     Synth; the pendulums from the beat); positions never saved; off: no work. Old sounds unchanged (golden check), and
     the randomiser draws exactly as before (`orb_quant`, not `orb_sync`: the randomiser's `o*_sync` rule would have
     caught it). CPU on the PC (O2, per 128-frame block): the worlds alone, LIFE 1.28 us per generation (0.07 us a block
     at its fastest, 1/32 at 300 BPM), ORBITS 1.08 us (6 planets pulling on each other, 8 spokes), PENDULUMS 0.70 us (12
     pendulums); the engine with 4 held voices 24.9 us, plus LIFE 22.8 (within the noise), ORBITS 27.5, PENDULUMS 27.0,
     all three at once 43.1 us (most of it the extra notes, each generator up to 8 a second into the voices).
     Left: an MPC page; a panel button for the pages (the GENERATIVE hub page, another helper's).
  26. (2026-10-30) Plain clicks for everything (owner's rule: no one should have to hold Shift or know a hidden click).
     Done 2026-10-30 (the desktop window only; `desktop/hub.c`, `quickmod.c`, `seq_page.c`, `editor.c`, load_test
     `plain_test`). Every gesture that needed a modifier key, a double-click, a right-click, a hold or a computer key now
     has a visible button or a plain click; the old gestures stay as shortcuts and the help line names the button first.
     - Editing several Synths together (Ctrl-click SYNTH, Shift+1-3, Ctrl+Shift+F5-F7): TOGETHER on the console's third
       row; lit, SYNTH buttons add and remove; off, back to one. (Removing a Synth that isn't shown no longer jumps the
       panel to it.)
     - Undo / redo (Ctrl+Z / Ctrl+Y; the SNAPSHOTS page's buttons): UNDO and REDO at the third row's right end; hovering
       one names the step, the help line says what was undone. To make room the row is spaced for its labels:
       TOGETHER, GENERATIVE, GEN OFF, TEMPO (a little narrower) with HOST / OWN, the beat LED, UNDO, REDO.
     - Quick assign: Alt-click / right-click on the wheels, the X/Y pad, the ribbon and the keyboard: a DEST button at the
       controller panel's top left (a list of all the hands' sources); the generators' sources (Rain Drop .. Pendulum
       Sync), which the panel could not reach at all (it looked for them past MS_COUNT): a DEST in GENERATIVE's title
       strip. Ctrl-drag for an amount, right-click for REMOVE / OPEN IN MATRIX, Esc: while a source is picked the screen
       shows MOD FROM <source> (SCR_QMOD), its connections in the Synth in focus, each with - / + (5%), REMOVE and
       MATRIX, then CHANGE SOURCE (a list of every source), ALL IN MATRIX and DONE (back to the page shown before).
       Dragging a knob the source already moves changes that connection's amount. A ringed knob's help says how.
     - The step editor: SHIFT + click on a step number for DUP's range: RANGE on the title line (NOTES, MODS); a
       double-click for a new note: a second click on the selected empty cell; right-click delete / rest and SHIFT +
       Delete were already DELETE, REST and ALL + DEL STEP (the help now says so).
     - The keyboard's octave held 1 s (the default) and 2 s (MIDI panic): a click on an octave LED picks that octave,
       PANIC (under glide's ON) stops every note.
     - Option lists open with a plain click everywhere (two options toggle, LEDs pick, a red button steps or opens its
       list); right-click lists stay. Shift for fine moves and double-click for a default stay as shortcuts, named in
       the help line.
     - The computer keyboard needs the window's keyboard focus (any click on the panel gives it): the front rail under
       the keys says so and lists the keys.
     - The layout warning "syn_solo has no place..." is gone (the SYNTH buttons' S; never saved, no MORE row): the OTHER
       page is no longer made.
     load_test reads the help line now (`help_line()`, HT_HELP) and checks every console button and controller part has
     help of its own. No engine change, no new params, nothing in the audio path. Left: the MPC page has none of this.
  27. (2026-10-30) The sequencer's OUTPUT (owner: "one sequence plays all three synths"; Moog One manual p94). Done
     2026-10-30 (offline and the desktop window; `src/stepseq_run.h`, `test/multi_check.c` `seq_output_check`, load_test
     `seq_output_ui_test`; `docs/SEQUENCER.md`). Each Synth's `seq_output` (Own, Synth 1, 2, 3, 1+2, 1+3, 2+3, 1+2+3;
     Own first and the default, so old sounds are unchanged: golden check) chooses the Synths its step sequencer's
     notes play: each on its own voices (the shared 16), its own sound, poly count, transpose and octave, only while
     on; key ranges ignored (the notes go inside the instrument, not through the keyboard zones). Chord memory and the
     modulation lanes stay the sequence's own Synth's; the arp and the generators are not routed. Each lane remembers
     the Synths its notes went to, so note-offs reach them whatever happens (OUTPUT changed mid-note, a target or the
     sequence's own Synth switched off, STOP, All Notes Off): offline, 40 changes of OUTPUT over ratchets and ties leave
     no voice sounding. The randomiser never changes it. Window: OUTPUT (a plain click opens the list) and PLAY ALL
     (`seq_all`, until now only on the SETUP page) on the SEQUENCER module's title strip, OUTPUT also on its MORE page.
     CPU: nothing per sample, a few operations per note; offline bench (PC, O2, per 128-frame block): one sequence to
     1+2+3 96.73 us against the same notes from three sequences of their own 96.79 us (the routing costs nothing
     measurable; the cost is the extra voices themselves). Left: the MPC page; `seq_output` is not on the MPC
     params screen's SEQUENCER section yet (Phase 9).
  Order now (owner, 2026-10-27): finish the Windows desktop version (sequencer, review fixes, the refreshed audit's
  remaining work incl. multitimbral, a hands-on Carla check, presets), then the web version, then the MPC port.
  Parameters may be renamed or reordered freely until the first release (`GEN_VST_RELOCK=1`); presets are regenerated.
- **Then, the MPC port (in order):**
  1. MPC builds for armv7 + aarch64 and `tools/test_port.sh` x86 + aarch64, from a copy outside Dropbox (CLAUDE.md).
  2. Phase 9: touchscreen page (layout.conf, drawn from scratch, Moog One manual's panel as layout reference only, no
     Moog names/logos), Q-Links; then MPC builds, skin preview and a code review.
  3. Phase 10: decide with the owner whether multitimbral parts make sense on MPC (each track can host its own ONE-ish).
  4. Phase 11: package with `tools/release.py` locally; publishing and the final name are the owner's decisions.
- **Needs a real MPC (owner):** `tools/bench.sh` at 4 and 8 voices to set the default voice count; loading on the
  device. Offline estimate: about 7.7-8.2 us per voice per block on the PC (8 voices: about 62-66 us of the 2902 us
  budget there; measured 2026-10-27 after Tier 1).
- **Quick check after a restart:** start Docker Desktop, then `win/run.sh bash ports/one-ish/test/run.sh` must print
  PASSED; `Play ONE-ish.bat` in `%USERPROFILE%\Tools` opens Carla with ONE-ish.

## Risks and open questions

| Risk | Fallback |
| --- | --- |
| Your MPC Key 37 G2 can't load plugins (Gen2 lock-down) | Build and test on the computer; ask on the Open MPC Discord for a Gen1 tester; revisit when Gen2 root access appears |
| The ladder filter costs too much CPU per voice | Cheaper filter model, fewer voices, or mono |
| Multitimbral parts don't fit the budget | Ship single-part first; parts become a later version |
| A plugin crash takes down the MPC mid-session | Offline ASan test on every build; save projects before testing |
| Plugin MIDI out is ignored by MPC | Not needed for ONE-ish; only matters if an internal sequencer is added |

**Open questions**

- [ ] Has anyone got root SSH on a Key 37 G2 yet? (ask on Discord)
- [ ] Polyphony target: 4 voices, 8 voices, or mono first?
- [ ] Final plugin name

## Sources

- [mpc-vst-plugins on GitHub](https://github.com/sd88me/mpc-vst-plugins)
- [Open MPC plugin catalog](https://sd88me.github.io/mpc-vst-plugins/)
- [Synth Anatomy: VST plugins on MPC OS](https://synthanatomy.com/2026/09/run-vst-plugins-on-the-akai-mpc-os-standalone-mpc-force.html)
