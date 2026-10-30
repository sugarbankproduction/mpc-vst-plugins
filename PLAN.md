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
     Left (solo and the M | S buttons done 2026-10-29; sequencer OUTPUT 2026-10-30, item 27; per-Synth outputs 2026-10-30, item 38): the sustain pedal's target,
     NRPN (a range set by pressing a key: item 39); three Synths on the MPC is the owner's later call (`ONEISH_SYNTHS`).
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
     destinations with no knob of their own: Pitch (all oscillators) and each LFO's Rate (st). Left: rings on the MORE
     pages' items; the MPC page. (EDIT MODULATIONS, the matrix filtered by a source: item 39.)
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
  27. (2026-10-30) FOLLOW TEMPO (owner's request: "when I adjust the bpm they adjust as well"). Done 2026-10-30 (offline
     and the desktop window; `src/physgen_run.h` gen_tempo_factor, `src/gens_a_run.h`, `src/gens_b_run.h`,
     `test/gens_a_check.c` gen_tempo_checks, load_test follow_tempo_test). One shared param `gen_follow_tempo` (Off / On,
     default On; GLOBAL in make_synths.py, engine_global()). On, the generators that ran at their own speed follow the
     tempo the synced parts use (the host's song position's, else the host's or the OWN tempo; 120 if none): the balls'
     and particles' world (motion, gravity, the emitter, the particles' life, KEY EMIT), the rain's pond (drops a second,
     ripple speed and life), the orbits' SPEED and LIFE's FREE rate run at tempo / 120 times their speed (0.25 .. 4),
     the factor gliding over about 0.1 s, so a tempo change never jumps; MAX NOTES/S stays a real second's cap. The
     already-synced parts (LIFE's note-value RATE, Markov, the pendulums' CYCLE, SYNC) are unchanged. At 120 BPM the
     factor is exactly 1: the same notes on and off, old sounds sample for sample (golden check); the randomiser never
     touches it. Offline: at 240 BPM, on, x 1.92 (balls), 2.08 (particles), 2.00 (rain), 2.00 (orbits), 1.96 (LIFE
     free) the notes of 120 in 12 s, off x 1.00; OWN 240 with the host at 120 x 1.92; the host's song position at 60
     x 0.53; 120 -> 400 -> 30 BPM at once moves a ball on smoothly (16 of 1000 at most a block) and its speed glides.
     The window: FOLLOW TEMPO on the GENERATIVE page (the right column's last row, a plain click; beside it "speed x
     1.50"), and on the BOUNCING BALLS, PARTICLES, RAIN, ORBITS and LIFE (with FREE) MORE pages. CPU on the PC per
     128-frame block, 4 held voices with balls, particles, rain, orbits and LIFE playing at 120 BPM: off 41.84 us, on
     40.98 us (no measurable cost: a glide and a multiply per generator step). Left: the MPC page.
  28. (2026-10-30) The sequencer's OUTPUT (owner: "one sequence plays all three synths"; Moog One manual p94). Done
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
  29. (2026-10-30) Getting around the screen (owner: "the back and forward buttons at the top change the preset";
     "click through the generators as tabs"; "a page that shows all the running generators"). Done 2026-10-30 (the
     desktop window only; `desktop/nav.c`, `more_pics.c` pics_tile, `gen_layout.py` HUB_TAB, load_test `nav_tests`).
     The top of the centre screen, left to right: BACK, the PRESET box, FWD. The PRESET box is the frame round the
     patch name, PRESET written on its top edge, with the preset's own < > (solid arrows) inside it: they and the
     console's PREV / NEXT and the PRESET knob change the sound, as before. BACK and FWD (framed buttons, a hooked arrow
     over the word, dim when there is nowhere to go) change only the page: BACK shows the page shown before, at the
     sub-page it was left on, FWD goes forward again; a history of 16 pages each way, the window's own (not saved),
     taken from whatever changed the page (buttons, triangles, keys, quick assign). Turning a page's own sub-pages is
     not a step. Shortcuts: Alt+Left / Alt+Right, the mouse's back / forward buttons. The sub-pages' < > (the title
     line's right end, also on the library's pages) are now small triangles round "n / m" in a thin box: a third look,
     so the three can't be mixed up. (BACK / FWD went at the preset line's ends, not on the title line, because the
     step editor's and the library's title lines are full of their own buttons: this way they are in the same place on
     every page.) The generator tabs: on the GENERATIVE hub page and every generator's page the column-heads line is a
     row of tabs, HUB, EUC (EUCLID and TURING's page), BALLS, PART, LIFE, ORB, PEND, RAIN, MKV, ALL, one click each;
     a generator page's tab has a dot, lit while it is on in the Synth in focus. Data-driven (`HUB_TAB`, from the hub's
     generator list; a new generator page gets a tab named by its id). Those pages' one-line note moves to the help
     line. The GENERATORS overview (ALL, or ALL RUNNING at the hub page's bottom right): every generator on in one Synth
     (the one in focus, or SYNTH 1 2 3 at the title line's right end, this page only), each in a tile with its name,
     moving (the MORE pages' own pictures, drawn small without their text; BALLS and PARTICLES each show only their
     own), 1 to 12 tiles in a grid that gives each as much room as fits. EUCLID (its ring of hits) and TURING (its
     register's length and CHANGE) are drawn from their settings and stand still: the engine doesn't tell the window
     their step. Generators that are off are left out (the room goes to the ones playing; the tabs' dots show the rest).
     A click on a tile opens its page in that Synth. The tiles repaint every third tick of the rate timer (about 22 a
     second) and only while the overview shows. No engine change, no new params, nothing in the audio path, no CPU cost
     there. Left: the MPC page has none of this.
  30. (2026-10-30) New defaults, the effects' ONs, the generators' RANDOMISE / RESET, the randomiser's extras (owner's
     requests). Done 2026-10-30 (offline and the desktop window; `src/synths.h` legacy_defaults, `src/randomize.h`,
     `desktop/hub_gens.c`, `test/defaults_check.c`, `test/rnd_golden.c`, load_test `defaults_ui_test`;
     `docs/MULTITIMBRAL.md`).
     - A new instance and the Init preset: all three Synths on (Synth 2 16', level 45; Synth 3 a triangle 4', level 35;
       Synth 1 the old Init), so a key plays the three-layer sound: peak 0.83, about 4 dB over Synth 1 alone, no
       clipping (9 of the 16 shared voices for a 3-note chord). Every Synth's matrix rows 13-16: Aftertouch -> vibrato
       depth, Pad X -> ladder cutoff (darker to the right), Pad Y -> resonance, Ribbon -> pitch 2 semitones; nothing at
       rest. LINK SENDS on (a window convenience with no sound of its own: on for old sounds too).
     - Old states, Carla projects, patches and presets: a state / SAVE SYNTH sound / patch file now carries `dv=1`; one
       without it reads what it leaves out as the old defaults (Synths 2 and 3 off with Synth 1's sound, no hands rows),
       so the 61 old sounds play sample for sample as states and as programs (golden check; the Init program with
       Synths 2 and 3 switched off is the old Init exactly). The other factory presets are filled with the old defaults
       (`make_synths.py old_default`): single-Synth presets stay single. LOAD INTO SYNTH loads against Synth 1's
       defaults; SAVE SYNTH 2 / 3 writes that Synth's own Init values.
     - ON buttons on the front panel for every effect: SYNTH EFFECT (as before), MASTER EFFECTS (both, as before), and
       new MASTER FX 1 and MASTER FX 2 each (`dly_on`, `rev_on`, shared, default On: today's sound); off, the slot fades
       out and its send does nothing (offline: the very samples of that send at 0).
     - Aftertouch: channel pressure reaches every Synth on its channel (it did); poly pressure (0xA0) was ignored, now
       it is the aftertouch of the Synths holding that key.
     - GENERATIVE page: RANDOMISE | ALL, MUTATE | ALL, RESET | ALL (the right column under the generators). RANDOMISE:
       every generator's settings in musical ranges (new rules for the balls, particles, rain, Markov, LIFE, orbits,
       pendulums; the RANDOM page's GENERATIVE tick uses them too), one generator switched on, now and then two, and a
       new KEY / SCALE; the sound stays. MUTATE nudges the settings of those on (40%, or AMOUNT if higher). RESET: every generator setting of the
       Synth in focus (ALL: all three) to its default, so all off; KEY, SCALE, FOLLOW TEMPO stay; the first click asks
       ("CLICK AGAIN", 4 s). One undo step each. Offline: 24 random generator sets, one or two on, all heard, finite,
       every note in key (Turing aside: its own SCALE).
     - SYNC above the filters' ALL 3: once, both cutoffs of all three Synths to the Synth in focus's (one undo step).
     - The randomiser: RANDOMISE always wires aftertouch, pad X, pad Y and the ribbon (rows 13-16, distinct heard
       destinations from lists made for each: cutoffs, resonance, the effects' mix and sends, wave shape, FM; pitch for
       the ribbon; vibrato depth, a cutoff or the level for aftertouch; bigger at WILD); the MODULATION lock keeps the
       matrix; MUTATE never adds, moves, removes or nudges those rows. The ANALOG page and SPREAD are never changed
       (their numbers are still drawn, so the rest is the same). The effects are heard: at WILD 0 send 1 / send 2
       9.5% / 17.2% -> 15.5% / 26.9% on average, sounds with both sends under 15% 24% -> 2%, the synth effect's mix
       33.7% -> 45.7% (400 sounds, `rnd_golden stats`); within the safety caps. BOTH / MONO / POLY on the RANDOM page
       (where ALL 3 SYNTHS was); RANDOMISE and MUTATE are split buttons, the right part ALL (all three Synths; Shift+R
       / Shift+M still), so the ALL 3 SYNTHS toggle went. The randomiser's edits no longer follow lit LINKs to the other
       Synths (with LINK SENDS on by default, a random sound's sends would have reached all three). A bug found on the way: the randomiser's rule table had grown
       past 255 rules, so the sequencer tick's last rules wrapped onto the first ones (fixed: 16-bit rule numbers).
       `test/rnd_golden.c`: RANDOMISE and MUTATE at WILD 0 / 50 / 100% (and with the arp tick) give exactly what they
       gave before but for the rows 13-16, ANALOG, SPREAD, the sends and the mix.
     CPU on the PC (offline_bench, O2, per 128-frame block, 4 held keys of Init, the median of three runs): Synth 1
     alone with rows 13-16 empty (the old Init) 26.3 us, with the four hands rows 28.2 us (+2 us: four more matrix rows
     per voice), the new Init with its three Synths 83.3 us (three layers: 12 voices). The FX slot ONs: two flags.
     Nothing in the audio path allocates, locks or touches a file. Left: the MPC page has none of this.
  31. (2026-10-30) Robustness: the classes of bug a hardware synth's firmware notes list as fixed, each looked for in
     ONE-ish with a regression test (`test/robust_check.c`, offline; `offline_check robust` runs it alone; load_test
     `learn_shows_test`; host_test's 300-event block). Already fine, now tested: every envelope ends (52 settings x
     poly / mono legato / unison: LOOP, LATCH with sustain 0, MULTI TRIG, delay and hold, curves, synced times in both
     EG TIMES, MIN, a 100-second release; the last voice is idle, no tail left at a tiny level); a Synth switched on or
     off never cuts the others' notes; 112000 random messages in 40 blocks on every channel (and an NRPN storm) leave
     nothing hanging, no NaN, about 2 us a message under ASan; PANIC never starts a note and stops every Synth's notes,
     the arp's and the sequencer's OUTPUT targets'; MIDI learn reaches all 1901 params, the host told each time; LOOP
     with every time at MIN, a 200-key retrigger storm while looping: finite; the Init sound is one plain saw. Fixed:
     (a) a key held while a Synth's MIDI channel, the global channel or Per Synth changed hung (its release was checked
     against the new channel); (b) All Notes Off in mono moved the releasing voice onto another note of the chord or
     arp; (c) LOOP switched off mid-loop with the key down stopped the note dead (it now goes on to its sustain);
     (d) NRPN synced times: only 16383 reached the last of the 13, and in MIN, 13 Syncs an envelope's synced time
     landed on the wrong value; (e) the plugin wrapper's SAMPLE_ACCURATE queue (other ports; ONE-ish applies events at
     once) sent the 257th event of a block ahead of the 256 queued, so a note-off could beat its note-on. No event is
     ever dropped: the MPC build passes each event straight to the engine; the desktop's rate converter (EVQ 512) sends
     what it holds, in order, when full. Old sounds unchanged (golden check). Nothing new in the audio path.
  32. (2026-10-30) Factory bank B and browsing by category. Done 2026-10-30 (offline and the desktop window): 128
     original presets in `presets/banks/bank_b.py` (pads 28, strings 16, bells 12, effects 14, arps 12, sequences 12,
     drums 8, performances 14, generative 12), most played from aftertouch, the mod wheel, velocity, Pad X / Y and the
     ribbon; `test/bank_check.c` (`bank_b_checks`; since the merge with bank A, run inside offline_check with `bank_a_checks`, one loudness helper for both; alone: `offline_check bankb <dir>/demo.wav`) plays each one: no peak over -1 dBFS, none
     silent, no NaN, every one within 6 dB of the bank's median loudness (a TRIM table in the bank evens them out), the
     generative ones make notes (ten by themselves, two from a held chord), and its CPU on the PC (about 52 us per
     128-frame block on average, 1.8% of a block); `test/out/bank_b_demo.wav` to listen to. The factory presets' tags
     now come from presets.json (`make_presets.py` writes `desktop/factory_tags.h`; the first 40's tags moved there
     from library.c). The PATCHES page: category tabs (ALL and every category with its count, plain clicks), 8 patches
     a column, pages by the arrows or the wheel. Bank A (`presets/banks/bank_a.py`: 128 basses, leads, keys, brass,
     plucks and organs; `docs/PATCHES.md`) is merged with it: 296 factory presets, the ORGAN tab from bank A's tags.
     Left: listening on Carla, the MPC (the generative presets' CPU there).
  33. (2026-10-30) GRAINS, Auto CTRLs that play, and LINK on the X/Y pad (owner's requests). Done 2026-10-30 (offline
     and the desktop window; `src/fx/grains.h`, `test/grains_check.c` (offline_check `grains`), load_test
     `pad_link_test`).
     - GRAINS: a new SYNTH EFFECT type and MASTER FX 1 / 2 type (last in each list), an original granular design, kept
       apart from GRANULAR (Mutable Instruments Clouds): a 5.9 s recording (mono, in the slot's own memory: the delay
       lines', no new memory but a 64-grain pool, about 3 KB), FREEZE that holds all of it, POSITION (0 .. 5 s back),
       SIZE (10 .. 500 ms), DENSITY (1 .. 150 grains a second), PITCH (+-48 st, the master slots' PITCH A in cents),
       SPRAY (up to 2 s more back at random), PITCH RANDOM (up to +-12 st), PITCH QUANTISE (Free, Semitones, Octaves,
       Oct + 5ths), REVERSE (the chance a grain plays backwards), STEREO (SPREAD), ENVELOPE (TEXTURE: 0 near-square,
       50% Hann, 100% percussive; edges never under 1.5 ms), FEEDBACK (the grains back into the recording, a block
       later, soft-limited, at most 90%), MIX. Hermite-interpolated reads, every grain kept inside the recorded audio
       whatever FREEZE does (no crossing the write point), level by the expected overlap. The synth effect reuses the
       Clouds-era keys (`modfx_pos`, `size`, `density`, `texture`, `pitch`, `freeze`, `spread`, `fb`) and adds
       `modfx_spray`, `modfx_prand`, `modfx_reverse`, `modfx_gquant`; each master slot `dly_` / `rev_` `gpos`, `gdens`,
       `genv`, `gspray`, `gprand`, `greverse`, `gquant` (SIZE, PITCH A, SPREAD, FEEDBACK, HOLD shared with the other
       types). Matrix destinations: the reused keys already were; Spray, Pitch Random, Reverse and each slot's six new
       ones are 15 new destinations at the very end of the list (`MD_FX2_FIRST`, `md_fxk()` in src/modmatrix.h: no
       older destination moved). CTRL 1 / CTRL 2 can target Spray, Pitch Random, Reverse. FREEZE: a button on the
       SYNTH EFFECT panel (a plain click; GRANULAR's too). RANDOMISE: WILD's any-type pick may choose GRAINS (its new
       controls get no randomiser rule, so they stay as they are, at first their defaults: a lush cloud), and WILD's
       matrix rows and lanes draw from the destinations before GRAINS' 15 (so their draws are as before); WILD 0
       draws exactly as before. Found: src/randomize.h's rule table has 262 entries but `rnd_ctx_t.rule` is an
       `unsigned char` (255 = no rule), so its last seven rules (the sequencer's, from `seq_note_trig` on) never
       apply; not changed here (widening it changes what RANDOMISE does), the randomiser's owner to decide (since fixed by item 30: 16-bit rule numbers). Offline: finite and bounded
       (largest 7.0) over 64 extreme settings, no denormals; FREEZE holds (-15.2 / -15.1 dB at 2 and 5 s, unfrozen
       silence); mix 0 exactly dry; PITCH +12 turns 220 Hz into 442 Hz; reverse as loud, another waveform; type
       changes mid-note (chorus, grains, delay, granular) without a click; state round trip. CPU on the PC (O2, per
       128-frame block): 16.9 us at the defaults (183 ms grains, 74 a second), 74.7 us at full density (all 64 grains,
       99th percentile 93 us); MPC: not measured yet (`tools/bench.sh`); at 4x the PC, full density would be about
       300 us of the 2902 us block.
     - Auto CTRL 1 / CTRL 2 (owner: "the knobs on the front don't always seem to do much"): each type's two most
       audible controls, never MIX (it has its own knob): delays TIME and FEEDBACK, choruses RATE and DEPTH, flangers
       and phasers RATE and FEEDBACK, bit crusher BITS and SAMPLE RATE, resonator CUTOFF MID and RESONANCE, modal
       resonator PITCH and DECAY, granular POSITION and DENSITY, tape colour DRIVE and FLUTTER, density DRIVE and LOW
       CUT, console DRIVE and GAIN, vocoders TONE and SIBILANCE, grains SIZE and DENSITY. The panel writes the target
       under each knob (TIME / FDBK ...) instead of CTRL 1 / CTRL 2. Old sounds: every state now starts "v=1;ctl=2"
       (src/synths.h STATE_CTL; patch files keep the line); a state without it is older, and a CTRL on Auto that does
       something there (its knob off centre, a matrix slot, the ribbon or a sequencer lane on it) gets Auto's old
       target written in (`FX_AUTO_CTRL_V1`, engine.c `fx_auto_compat`), so it sounds as it did (golden check). Not
       covered: host automation of a CTRL knob in an old project whose saved value was 0. Offline: all 25 types' Auto
       CTRLs move the sound (centre against either end), none is MIX.
     - LINK on the X/Y pad: before, the pad sent CC 2 / CC 3 / CC 18 on the window's MIDI channel, so with MIDI
       CHANNEL Omni it already reached every Synth, on a fixed channel only the Synths there, and with Per Synth
       channels only the Synth in focus. `link_pad` (shared, saved with the sound, as the other LINKs): lit, those
       three CCs from anywhere (the window, or a controller) reach all three Synths whatever their channels, so Pad X,
       Pad Y and Pad Gate move together in each (each Synth keeps its own POLARITY and HOLD). Engine-side (src/synths.h
       midi) so a hardware pad and the MPC follow it too. The LINK button sits beside the pad's name (a plain click);
       also on the CONTROLLERS page. Offline and load_test: off, Synth 1 alone; lit, all three, let go in all three.
     Left: an MPC page for GRAINS' controls; an on-device bench.
  34. (2026-10-30) The generators' KEYS, GEN TEMPO and FOLLOW KEYS (owner: "when I put Euclid on, it is waiting for
     me to hold a note ... a button so I can set it to play when I play or hold a note, the same for all generators";
     "each Synth's generators at their own BPM"; "notes related to the notes played"). Done 2026-10-30 (offline and the
     desktop window; `src/seq.h` GK_*, engine.c `clk_go` / `fire_step` / `gen_bpm` / `gen_root_of`, the three
     `*_run.h`, `src/synths.h` `state_absent`, `test/gen_keys_check.c`, load_test `gen_keys_ui_test`).
     - One meaning for every generator's KEYS list (options in this order everywhere, new ones appended at the end):
       FREE (plays by itself while the Synth is ON; the keys play as usual), HELD KEYS (only while keys are held or
       latched, from the lowest; nothing new starts after the last key is let go: the notes waiting for a SYNC grid
       line are dropped too, a fix for the balls, rain, LIFE, orbits and pendulums), KEY LAUNCH (each key starts it
       from that key, and it plays on after the key is let go), KEY EMIT (only while keys are held, more the harder).
       Per generator: EUCLID and TURING (`gen_keys`, new, one for both since Turing plays on Euclid's hits: Free,
       Held, Launch = one pattern, or one Turing loop, from the key; no Emit: Held already plays only while held and
       Euclid's density is HITS); balls and particles (`phys_keys`, unchanged: it had all four); RAIN (Launch: a
       shower of 3-12 drops by velocity; Emit: rain while held at RATE x velocity / 64); MARKOV (Launch: a phrase of 8
       notes from the key, ending on it; no Emit: a melody plays one note at a time, Held already stops it); LIFE
       (Launch: a new grid seeded from the key, playing until it dies out or settles into a loop); ORBITS (Launch: the
       planets launched again in step, from the key); PENDULUMS (Launch: they start together from the key, with no new
       push each cycle, so DAMPING lets them die away). LIFE, ORBITS and PENDULUMS have no Emit: they do not send
       anything out, and Held already plays only while held.
     - FREE Euclid and Turing play in the Synth's KEY and SCALE from a new LOW OCTAVE (`gen_octave`, C3) over the
       Turing OCTAVES; Euclid's hits play EUCLID NOTES (`gen_eu_notes`: Root, Scale Up, Scale Up/Down, Scale Random);
       Turing's SCALE "Held Notes" means the Synth's own scale there, another scale is played from the KEY's root.
       With the ARP on (and Turing off) the arp plays the held keys whatever KEYS says.
     - The default for a NEW sound is FREE. A sound saved before keeps playing from the held keys: a state lists only
       the values off their defaults, so `src/synths.h` has a small table (`state_absent`) of what a MISSING key meant
       when the sound was saved (gen_keys: Held Keys; gen_follow_keys: Off), used when a state is read and when one is
       written (a new sound at FREE writes `gen_keys=0`, about 12 bytes a Synth). It is the minimal form of a state
       version: a version number would hold the same table for many params at once and can absorb it. The factory
       preset "Euclid Turing Bells" says Held Keys itself.
     - GEN TEMPO (`gen_tempo_src` Global / Own, `gen_bpm` 20-300, per Synth): Own, every generator of that Synth uses
       that BPM wherever it used the tempo: the synced rates and grids (LIFE's RATE, Markov's RATE, the pendulums'
       CYCLE, the SYNC grids of the balls, particles, rain and orbits), FOLLOW TEMPO's speed (BPM / 120) and the step
       clock when Euclid or Turing play without the arp. The arp and the step sequencer stay on the global tempo (they
       are the Synth's own playing, not a generator). Own runs free from where each clock is (it no longer follows the
       host's song position); a change of BPM or of the way carries every clock on from the beat it is at, and FOLLOW
       TEMPO's speed glides, so nothing jumps. Global: exactly as before.
     - FOLLOW KEYS (`gen_follow_keys`, per Synth, On for new sounds, Off for those saved before): a generator playing
       from the keys plays in the key you play. One key: the Synth's SCALE with that key as its root (D held in a
       minor Synth: D minor). A chord held: every note goes to the nearest of the chord's own notes (C E G held: only
       Cs, Es and Gs), so the generator follows the harmony. Off: the Synth's KEY and SCALE stay, the key only says
       where to start (as before). Euclid and Turing on Held Keys already play the held notes themselves.
     - The window: on the GENERATIVE page each generator's row has its KEYS button at the right end (FREE / HELD /
       LAUNCH / EMIT, lit unless FREE; a plain click opens the list), and two new rows above FOLLOW TEMPO: GEN TEMPO
       (GLOBAL / OWN BPM, a click toggles; its BPM: < > a click each, which also switch to OWN, a sideways drag or the
       wheel on the number) and FOLLOW KEYS (a click toggles). Every generator's MORE page lists Keys, Follow Keys, Gen
       Tempo and (while Own) Own BPM; the GENERATIVE page also Euclid Notes and Low Octave. The rows' names are short
       now (BALLS, LIFE, ORBITS, RAIN, MARKOV) to fit beside the button. The balls and particles share phys_keys (one
       world), Euclid and Turing gen_keys; FOLLOW KEYS and GEN TEMPO are one setting per Synth for all its generators.
     - Behaviour note: a RANDOMISE with GENERATIVE lit now makes sounds whose Euclid / Turing play by themselves (KEYS
       is never randomised and new sounds are FREE). load_test's keyboard check reloads the first preset for that.
     Old sounds unchanged (golden check, 61 sounds). No allocation, lock or file in the audio path; a few comparisons
     per step. Left: the MPC page; the hub's two new rows use the last free slots (another generator on the page needs
     a new layout). Merged with item 30's RANDOMISE | MUTATE | RESET (2026-10-30): the right column under its three
     generators is GEN TEMPO; FOLLOW KEYS | FOLLOW TEMPO (lit, FOLLOW TEMPO shows its speed, "FOLLOW x 1.25");
     RANDOMISE | ALL and MUTATE | ALL; RESET | ALL sits beside ALL OFF (now "ALL OFF" / "BRING BACK", all three Synths
     as before). The factory presets (the first 40 and both banks) are filled with the old Held Keys / Follow Keys Off
     (`make_synths.py` OLD_S1), so they play as they were designed.
  35. (2026-10-30) SHARE: one LFO for all three Synths (owner: "a separate LINK button so an LFO can be shared by all
     three Synths, not only its speed"). Done 2026-10-30 (offline and the desktop window; `src/engine.c` lfo_owner,
     control_lfos, `src/synths.h`, `test/lfoshare_check.c` (`offline_check lfoshare`), load_test `link_test`;
     `docs/MULTITIMBRAL.md`). Four shared params `lfo1_share` .. `lfo4_share` ("Share LFO n (3 Synths)", Off / On,
     default Off). In the engine (so automation, MIDI learn and the MPC follow it): lit, LFO n of Synths 2 and 3 reads
     Synth 1's settings (rate, sync, timing, wave, range, polarity, variation, smoothing, start phase, note reset,
     delay, fades, repeats) and its free-running LFO is copied from Synth 1's every control step (the Synths render in
     lockstep, Synth 1 first; with Synth 1 off the first Synth sharing it steps it), so all three hear the same value
     at the same moment. Phase-lock choice: a voice that runs the LFO itself (note reset, repeats, the High range, a
     matrix-modulated LFO) keeps its own restart per note, with Synth 1's settings, so voices started by the same key
     in two Synths move in step; each Synth's own matrix still reaches its own voices. The Synths' own LFO settings
     stay stored and are heard again with SHARE off. The window: SHARE beside LINK at the bottom of each LFO panel
     (a plain click, lit when on; LINK, which copies the rate, unchanged and still labelled LINK: LINK RATE would not
     fit beside SHARE), and on each LFO's MORE page, with help text. The randomiser never touches it (no rule, like the
     LINKs); old sounds unchanged (golden check, randomiser golden). Offline: with SHARE on every LFO output of Synths
     2 and 3, free-running and in every voice, equals Synth 1's in 4000 of 4000 pieces (notes at odd moments, note
     reset, delay, fades, S&H, synced, High); off, 0 of 2000; off again restores each Synth's own; a state round trips
     and plays in step. CPU on the PC (offline_bench, O2, 128-frame block, 3 Synths x 4 keys, best of five): with every
     Synth already on Synth 1's LFO settings, off 88.12 us, on 87.60 us (SHARE's own work, a copy per LFO per control
     step, is not measurable); with their own settings 84.35 off, 87.34 on (the difference is running Synth 1's LFOs,
     here a High-range one per sample, in place of their own). Nothing in the audio path allocates, locks or touches a
     file. Left: the MPC page.
  36. (2026-10-30) CABLES: the matrix seen as patch cables (owner said yes 2026-10-29/30). Done 2026-10-30 (the
     desktop window only, offline; `desktop/cables.c`, `skin.c` skin_layer_blend, load_test `cables_test`). CABLES on
     the MATRIX page's title line (left of the 1-8 / 9-16 arrows; the console rows have no free spot) is a plain click:
     the panel stays as it is, a quarter darker, and each active matrix row of the Synth in focus (SYNTH 1/2/3 switch
     it; with TOGETHER, the one shown) is a sagging cable with a dark outline, coloured by the source's family as quick
     assign's rings (envelopes amber, LFOs blue, the hands' controllers green, random violet, the generators pink),
     from its source to the control it moves (a ring round that knob). Sources with a place on the panel start there:
     LFO 1-4 and the three envelopes on their title strip, NOISE (Noise EG), the pitch and mod wheels, the X/Y pad (X,
     Y, GATE), the ribbon, GENERATIVE (each generator source used, named, down its right edge). The rest (velocity,
     aftertouch, the key, KB gate, sustain, sostenuto, EXP 1/2, constant, random, random uni) have labelled jacks in a
     JACK BAY laid over the keyboard's top, with CABLES OFF at its left; a destination with no knob of its own (Pitch,
     an LFO's rate in semitones, a MORE page's item) ends at a jack after them. A row switched off (its ENABLE) is
     faint. Each cable's middle has an amount knob (green positive, red negative, the arc from 12 o'clock): drag up or
     down, 1% a pixel (Shift 0.2%), one undo step a drag, the host told as for a knob; the Synths edited TOGETHER get
     the same amount (as MOD FROM's - / +). A drag or a click on a cable selects it: a label by its knob (row, source >
     destination, amount, VIA, XFORM, "row off") with an X that removes the row (quick assign's REMOVE: one undo step,
     the Synths edited together too). A click on a source's jack starts quick assign from it (then a knob: a new
     cable); clicks anywhere else go to the panel as always; Esc, CABLES again or CABLES OFF hide it. The knobs keep
     off the centre screen and away from each other and the ends; the screen's title and help lines stay in front of
     the cables. The MATRIX page is unchanged and stays the only truth (a row changed there, by the host or by undo
     redraws the cables at the window's timer). Drawing: the cables, jacks and bay are a cached layer (premultiplied,
     GDI+ anti-aliased) drawn again only when a row's source, destination or ENABLE, the Synth in focus or the size
     changes; a paint blends its part in and draws the knobs and labels on top. On the PC (load_test, 2026-10-30,
     the machine busy with other builds): full repaint 9.8 ms without, 11.5 ms with 3 cables (target 16); the layer
     drawn once over 20 repaints; a cable knob dragged repaints 227 x 28 px in 0.93 ms (target 2). Screenshots:
     load_test's `_cables`, `_cables_drag`, `_cables_sel`, `_cables4`. Nothing in the audio path changes. Left: a
     drag from a jack to a knob (a click on the jack, then the knob, does it); hover highlighting of a whole cable;
     VIA shown only in the label (no second cable); the MPC page.
  37. (2026-10-30) HQ MODE and WARM-UP (audit package 15, sections 0, 3, 7). Done 2026-10-30 (offline and the desktop
     window; `src/hq.h`, `src/engine.c` render_voice_os / voice_os / warm_drift / warm_tick, `test/hq_check.c`
     (`offline_check hq`, `hqbench`), load_test `analog_test`). Both on the ANALOG page, per Synth (each Synth pays only
     for itself, and a sound made in HQ keeps it), saved with the sound, Off by default, never touched by the
     randomiser. HQ MODE (`an_hq` Off / 2x / 4x; a click opens its list): the voice's three oscillators, ring mod,
     linear FM, sync and both filters (the non-linear ladder and drive included) run at 2 or 4 times 44.1 kHz, each
     sample's noise, MACRO and EXT input held for the sub-samples; polyphase IIR half-bands (8 coefficients 2x to 1x,
     flat to 20 kHz and 106 dB down from 24.1 kHz; 4 more for 4x) bring the filters' mix back before the amp, which
     stays at 44.1 kHz. Offline, aliasing (power that is not a harmonic) Off / 2x / 4x: hard sync, notes 84-103,
     -33..-40 / -61..-67 / -71..-82 dB; linear FM 40%, notes 84-96, -43..-51 / -63..-73 / -81..-87 dB; a triangle
     into the ladder at full drive and NON-LINEAR LADDER 100%, notes 91-100, -24..-45 / -41..-64 / -78..-87 dB. The
     note's level stays within 0.25 dB (the driven non-linear ladder about 1 dB quieter oversampled: its stages are
     closer to the circuit there) and its pitch within 0.01 cent. WARM-UP (`an_warm` On, `an_warm_amt` 50%,
     `an_warm_time` 1-20 min, default 5): like a cold synth, after the plugin loads or the switch goes on each voice's
     oscillators sit off (at 100% up to about +-25 cents: a shift the whole Synth shares plus each oscillator's own,
     fixed per voice from a seed, so a test gets the same every run), the cutoffs too (+-0.12 octave), and the drift
     wanders up to 3 times deeper; it all fades as exp(-3t / TIME) (5% left at TIME) to the normal DRIFT. Offline at
     100% and 1 minute with DRIFT 0: a held A5 is 5.8 cents off on average over the first 2 s and wanders 4.4 cents,
     0.4 cent at 60 s, within 0.5 cent and still after 90 s; off and on again starts over. Both Off: exactly the old
     samples (golden check, 61 sounds; an instance that had both on and off again equals a fresh one). No NaN or
     denormal at extremes (16 voices, top notes, FM, sync, filter FM, full resonance and drive, 4x); nothing in the
     audio path allocates (the filters' states sit in each voice). CPU on the PC (O2, per 128-frame block, a busy patch:
     two saws, sync, the ladder; A/B against the engine before, same machine): Off 8.4-8.6 us a voice (before 8.0-8.7:
     within the noise, about +3%), 2x 14.6-14.7 (1.75x), 4x 24.3-24.8 (2.9x); with the non-linear ladder Off 10.5, 2x
     19.5, 4x 34 us; 8 voices at 2x 117-119 us, 16 voices 235-265 us (4x: 195-217, 391-438 us). WARM-UP on costs about 0.1-0.2 us a voice. On the MPC
     (not measured) a 2x voice should cost about 1.8 voices, so HQ stays optional there (the owner's call). Left: an
     iterative (Newton) solve of the ladder's stages in HQ (audit row "how resonance thins the bass"); not done, as 4x
     already takes the images below -77 dB and costs 3x a voice.
  38. (2026-10-30) Separate outputs per Synth (audit section 11, package 13). Done 2026-10-30 (offline and the desktop
     DLL; `src/synths.h` `one_ish_render_multi`, `desktop/hostrate.c`, `desktop/editor.c`; `test/multiout_check.c`,
     load_test's rate tests; `docs/MULTITIMBRAL.md` "OUTPUT"). The desktop plugin declares eight outputs: 1-2 MAIN, 3-4
     Synth 1, 5-6 Synth 2, 7-8 Synth 3 (named for hosts that ask, effGetOutputProperties). Choice: a pair per Synth
     rather than the Moog One's single shared SUB pair (each Synth gets its own mixer channel in the host; MAIN plus one
     Synth on its own pair is the instrument's MAIN / SUB split; SUB VOLUME is the host's fader on the pair). Per Synth
     `syn_out` ("Synth Output": Main (default), Own Pair, Main + Own), a plain click on the SETUP page's new OUTPUT row
     (the page now has three sub-pages; since the merge with MIDI OUT, item 40, OUTPUT is on page 2) or on SYNTH OUTPUT's MORE page, with help text. The shared master effects'
     returns always go to MAIN; a Synth on its own pair still feeds them through its sends (an aux send: echo and
     reverb on MAIN, dry on its pair; sends at 0 for a dry pair). The master level and the limiter apply to every
     pair. OUTPUT is setup, not sound: kept by LOAD INTO SYNTH, left out of SAVE SYNTH, never randomised. The engine API
     stays: render() and one_ish_render_float() put every Synth on their one output whatever OUTPUT says, so the MPC
     build (stereo, wrapper unchanged; `tools/test_port.sh` x86 PASSED) and every stereo caller are as before. Each
     extra pair has its own rate converter fed the same engine frames as MAIN's (same latency, sample-aligned).
     Default routing: outputs 1-2 exactly as before (offline: MAIN equals one_ish_render_float() bit for bit, master
     effects on and off; golden check of the old sounds PASSED), outputs 3-8 silent, so a host using only the first two
     outputs hears everything. Offline and in load_test (44.1 and 48 kHz): Own Pair puts the Synth only on its pair
     (MAIN + pair = the default MAIN within 3e-8 offline), Main + Own leaves MAIN exactly as the default; all three on
     their pairs leave MAIN silent; the delay's echo of a Synth on its pair is on MAIN only. CPU: the engine's own routing costs nothing measurable (offline_bench, 128-frame block, 3 Synths x 3 notes, master effects on: stereo render 76.4 us, multi-out all on MAIN 77.6, each on its own pair 75.8: within run-to-run noise). The extra converters (load_test, 512-frame host blocks, the PC busy with other jobs, so roughly): at 44.1 kHz none (a copy); with every Synth on MAIN none either (silent pairs are skipped: 48 kHz 492 us against 536 for the old chord benchmark); with all three on their own pairs about +120-170 us per 512-frame block at 48 kHz (492 to 660 us, about 1.5% of real time more, in the quieter of three runs; 535 to 929 in the busiest) and about +70-120 us at 96 kHz. A pair costs only while its Synth is on it.
     Nothing in the audio path allocates, locks or touches a file (the converters and buffers live in the instance).
     In Carla 2.5.10 the owner's Continuous Rack uses a plugin's first two outputs only (Carla's RackGraph sends the
     rest to a scratch buffer), so `Moo Won.carxp` plays exactly as before; outputs 3-8 need Carla's Patchbay mode
     (Configure Carla > Engine > Process mode: Patchbay), wiring `output_1` .. `output_8` in the Patchbay tab, saved as
     a new project (`docs/MULTITIMBRAL.md`). Not tried in Carla by the owner yet. Left: a panel button on SYNTH OUTPUT
     itself (no room beside VOLUME / PAN / SPREAD), the master effects per pair (they are shared), the MPC (stereo).
  39. (2026-10-30) The MATRIX page's tools, a key range by pressing a key, the vocoders' matrix destinations (audit
     packages 6, 11 and 13). Done 2026-10-30 (offline and the desktop window; `desktop/matrix_page.c`, load_test
     `matrix_page_test`, `test/ext_check.c` voc_matrix). Every feature is a plain click.
     - DELETE: an X at the right end of every matrix row (the columns are a little narrower for it). The row goes, the
       rows below move up one, the last row is empty (Off, 0%, no VIA, no XFORM, enabled); in every Synth in focus;
       one undo step. Nothing else points at a row by its number (the sequencer's lanes and STEP MOD name
       destinations), but a MIDI learn or host automation of a row's own param stays on that row number, and the
       randomiser's hands rows (13-16) are found by position, so after a delete above them they sit one row higher.
     - FILTERS: SOURCE: ALL and DEST: ALL on the MATRIX title line. A click lists the sources (destinations) the rows of
       the Synth in focus use, with how many rows each; one picked, only those rows show (a source also matches as a
       row's VIA), the button lit with its name; CLEAR (shown while a filter is on) or "Show every row" brings back all
       16. Window state, never saved. A row changed so it no longer matches leaves the view at once.
     - EDIT MODULATIONS: the MOD FROM page's middle button (was ALL IN MATRIX): quick assign ends and the MATRIX page
       shows only that source's rows. So: a DEST button, then EDIT MODULATIONS. OPEN IN MATRIX and MATRIX on a line
       clear a filter that would hide the row.
     - DESTINATION in two steps: a click on a row's DESTINATION (or the ribbon's DEST on its MORE page) lists Off and 10
       sections (Oscillators, Mixer and noise, Filters, Envelopes, LFOs, Amp and output, Synth effect, Master sends,
       Master FX 1, Master FX 2), each opening its parameters (sorted by their names; a section "Other" catches any
       destination added later that fits none, and load_test checks none falls there). The old single list: a
       right-click.
     - KEY RANGE: LOW / HIGH beside each Synth's LOWEST KEY and HIGHEST KEY on the SETUP page; click, then press a key
       on the window's keyboard, the computer keyboard or a MIDI keyboard (the host's MIDI is noted lock-free on its
       thread and applied by the window's timer); the button reads PRESS and the help line says what to do; the other
       end moves along if it would cross; one undo step; Esc, a second click or leaving the page cancels. The key
       still plays.
     - The vocoders' SIBILANCE and SENSITIVITY (the synth effect's and each master slot's) as six matrix destinations at
       the very end of the list (296 now), so saved sounds point where they did and WILD's random rows never pick them
       (as with GRAINS'). Offline: a Constant slot at +100% over the knob at 0 plays the knob at 100% sample for sample,
       for all six. Found and fixed on the way: a modulated CTRL 1 / 2 targeting SIBILANCE or SENSITIVITY marked
       `dirty[-1]` in the effect pass (their slot had no effect destination); now it has one.
     Old sounds unchanged (golden check), the randomiser as before (randomiser golden). No engine change but the six
     destinations; nothing new in the audio path (no CPU cost to measure). Not done: shift-click shortcuts on DEST /
     MORE for the filters (the buttons are the way), matrix presets (another helper's), the MPC page.
  40. (2026-10-30) MIDI OUT (audit 14b) and SUSTAIN IN SYNTH. Done 2026-10-30 (offline and the desktop DLL; the window's
     SETUP page; `src/midi_out.h`, `src/midi_out_run.h`, engine.c `out_on` / `out_off` / `key_on` / `key_off` /
     `all_off` / `gen_stop` / `release_held`, `src/synths.h` (the buffer, `one_ish_midi_out_take`), `desktop/hostrate.c`
     (`oev_add`, `oev_send`), `desktop/editor.c` (canDo), `test/midiout_check.c` (`offline_check midiout`), load_test
     `midi_out_test`; `docs/MULTITIMBRAL.md` MIDI, `docs/SETTINGS.md` "MIDI OUT in a host").
     - MIDI OUT per Synth, on SETUP page 3: MIDI OUT (`mout_on`, Off by default), MIDI OUT SENDS (`mout_send`: Keys,
       Generated, Both: default Both), MIDI OUT CHANNEL (`mout_ch`, Synth n on n). Keys: the notes played from the keys
       (chord memory's chord too); Generated: the arp, the step sequencer and every generator. Saved with the sound, as
       the Moog One keeps its MIDI I/O with the preset (and has MIDI I/O LOAD FROM PRESET to keep them as set: that
       setting now covers these three too); INTO SYNTH keeps the Synth's own.
     - Sample-accurate: the engine notes each event at the sample it plays (the step clock already splits render() at
       every step); `desktop/hostrate.c` puts it at that engine sample's host sample (x fs / 44100, plus the converter's
       latency: with the sound, not before it) and sends each host block's events, sorted, with
       audioMasterProcessEvents from inside processReplacing. canDo "sendVstEvents" / "sendVstMidiEvent" is answered by
       the desktop layer (`editor.c`): `wrapper/vst2_wrap.c` is untouched, so the MPC build's ABI and behaviour are
       unchanged; there nothing enables the engine's buffer and it sends nothing.
     - Pairs: each note-on is remembered with the note and channel it went out on; its note-off always follows (its
       key or step's end; All Notes Off / All Sound Off, so PANIC; the Synth off; a change of way; a state or preset
       loaded; MIDI OUT off; SENDS or CHANNEL changed: those end at once on the old channel). A key let go under the
       sustain pedal or HOLD ends when they let go. One note and channel held by two sources (keys and sequencer, or two
       Synths on one channel) is counted per instance: the second retriggers, the last end sends the note-off.
     - Fixed buffers only (2048 events per render, note-ons refused past 512 so note-offs always fit; 4096 waiting in
       hostrate); no allocation, lock or file in the audio path.
     - SUSTAIN PEDAL per Synth (`sus_pedal`, On by default = All, as before): SETUP page 3, a click toggles; any
       combination of the three. Off while the pedal is down: that Synth lets go. Saved with the sound (a split's pedal
       is part of the performance), kept by INTO SYNTH.
     Tested offline: every generator's note-ons (arp, sequencer, Euclid, Turing, balls, particles, LIFE, orbits,
     pendulums, rain, Markov) go out at the engine's own sample, note and velocity, on the chosen channel; every way a
     note is cut short leaves nothing hanging and never two note-ons; the pedal holds only the chosen Synths. load_test:
     the DLL answers the canDos, calls audioMasterProcessEvents from inside processReplacing, sorted, paired, and at
     48 kHz places the note-on where its sound is. Old sounds unchanged (golden check, 61 sounds; randomiser golden).
     CPU on the PC (offline_bench, O2, 128-frame block, three Synths playing arp, sequencer and Euclid, 4 keys, best of
     five): MIDI OUT off 54.97 us, on 55.05 us (614 events in 3000 blocks): not measurable. Left: the MPC
     page (MPC ignores plugin MIDI out); MIDI out of CCs, pitch bend and the pedal itself (only notes go out); Carla not
     yet tried by the owner (`docs/SETTINGS.md` says how).
     Merged 2026-10-30 with multi-out (item 38) and the MATRIX page (item 39): the SETUP page has three pages: 1 ON ..
     LEVEL; 2 PAN, SENDS, SYNTH EFFECT, OUTPUT, MIDI IN (ALL), MASTER LEVEL, PLAY ALL SEQS; 3 MIDI OUT, MIDI OUT SENDS,
     MIDI OUT CHANNEL, SUSTAIN PEDAL, CHORD LEARN (every row a plain click). load_test checks MIDI OUT's timing at 48 kHz
     with a Synth on its own pair too (its converter running: the same host sample as on MAIN).
  41. (2026-10-30) Library parts and user libraries (audit package 12c / 12d and the timbre names; `desktop/parts.c`,
     `desktop/parts_test.c` in load_test, `docs/PATCHES.md`; offline and the desktop window). Every one a plain click:
     - EFFECT PRESETS: PRESETS on the SYNTH EFFECT, MASTER FX 1 and MASTER FX 2 MORE pages opens the library's new PARTS
       tab on that slot: SAVE (a name, then notes; the time kept), a click on one then LOAD (type and settings), INIT
       (the controls' defaults, the type kept), NOTES, DELETE, ALL TYPES; ON and the sends never change; one undo step.
       Kept by type name and control name, so they move between the Synth effect and the master slots. 13 original
       factory presets (chorus, ensemble, delays, flanger, phaser, crusher, room, hall, plate, space).
     - MATRIX PRESETS (PARTS > MATRIX): SAVE the focused Synth's rows in use with notes and the time; ADD (into the free
       rows) or REPLACE (each row where it was saved, the rest emptied); one undo step.
     - SYNTH NAMES: a click on a Synth's head on the SETUP page names it (12 characters); the engine keeps it in the
       state (`syn_name`, `s2_syn_name`, `s3_syn_name` after the tunings, written only when set, so older sounds write
       the same state as before), read by the window without a lock; shown under the SYNTH buttons and on the home line;
       saved with patches, snapshots, projects and SAVE S1/2/3; undo like any edit; a factory preset clears them.
     - USER LIBRARIES (SETTINGS > LIBRARY): `<` `>` switch, NEW EMPTY, NEW + FACTORY (every factory sound as a patch),
       RENAME, COPY SOUND TO. Each is a folder (patches, snapshots, sets, tags, effect and matrix presets); Default is the
       old patch folder, untouched; the others in `Documents\ONE-ish\Libraries` (`ONEISH_LIB_DIR`). The settings file
       keeps the one in use (`library=`).
     - DUPLICATE NAMES (SETTINGS > LIBRARY): Ask (default, as before), Overwrite, Auto Rename, Ignore, for IMPORT and
       COPY SOUND TO (`import_dup=`).
     No new params; nothing in the audio path (files only on the window's thread; a name change is one chunk command,
     as the tunings'). The library's tabs are a little narrower to fit PARTS. Left: export / import of sets, effect and
     matrix presets and libraries from the window (they are plain files: OPEN PATCH FOLDER); deleting a library (delete
     its folder); the MPC page.
  42. (2026-10-31) CABLES by dragging, and each cable's settings (owner, 2026-10-31: "build them all but also leave
     the original implementation of clicking a source jack and then a knob"). Done 2026-10-31 (the desktop window only,
     offline; `desktop/cables.c`, `quickmod.c` qm_connect, `editor.c` ed_hit; load_test `cables_drag_test`). Every way
     of item 36 stays (a click on a jack then a knob, the middle amount knob, a click on a cable then X, CABLES OFF and
     Esc); dragging is added, and each new action is also a plain click.
     - DRAG TO CONNECT: every source with a place on the panel now has its jack always (LFO 1-4 and the envelopes on
       their title strips, NOISE, the wheels, X / Y / GATE on the pad, the ribbon; the generators' when used), as the
       bay's do, and Pitch always has a destination jack in the bay. Press on a jack and move more than 4 px: a live
       sagging cable in the source's colour follows the mouse; a knob (or switch, or MORE-page item) the matrix can move
       gets a white ring as the mouse passes over it, as does a destination jack. Let go there: a new row at +25% in the
       focused Synth (with TOGETHER, in each Synth edited together, as quick assign), one undo step, the host told; the
       new cable is selected (its box opens) so its middle knob can be dragged. A row that already joins the two is kept
       and selected. Let go anywhere else: nothing happens ("Nothing connected" on the help line). A press and let go
       without moving does what it did: quick assign from that source, then click a knob. With all 16 rows in use the
       ring is red and the help line says, plainly, that the matrix is full and how to free a row; nothing is made.
     - RE-PATCH: two small dots on each cable, near its source and near its destination, are grab handles (a jack press
       makes a new cable, so the handles sit a little way along). Drag the destination's dot to another knob or
       destination jack: the row's DESTINATION moves there; the source's dot to another source's jack: its SOURCE
       changes. Let either go on empty panel (or the bay): the row is removed (UNDO brings it back). Over anything else,
       or its own end, nothing changes. One undo step a drag; the Synths edited together follow. The cable being
       re-patched is left out of the cached layer for the drag (the layer is drawn once at the start and once at the
       end).
     - SETTINGS ON THE CABLE: a selected cable's label is now a small box beside its knob (placed clear of the centre
       screen, so the help line explaining its controls stays readable): the row's number and source > destination, ON
       / OFF (the row's ENABLE; off, the cable is faint), X; AMOUNT (a bar, the value, < >), VIA (a list of the matrix's
       controllers, None by default) and VIA AMOUNT, XFORM (its list) and its amount. Values drag up / down (1% a pixel,
       Shift fine) or step 5% with < >. Each edit is one undo step, writes the MATRIX page's own params (the page stays
       the only truth and redraws), told to the host as a knob is.
     - The help line says what letting go would do in each state: dragging, over a knob ("Let go: Velocity > Ladder
       Cutoff, connected at +25% ..."), the matrix full, re-patching ("Let go: row 1 moves from Ladder Cutoff to Ladder
       Res ..."), over empty panel ("the cable is removed ... UNDO brings it back"), not a destination; afterwards what
       was done, for six seconds.
     load_test (offline): a drag from VELOCITY to the ladder CUTOFF makes row 1 Velocity > Ladder Cutoff at +25% as one
     undo step, selected, UNDO / REDO; let go over nothing: no row, no step; a plain click on the jack still starts quick
     assign and a knob click connects; the destination end dragged to RESONANCE moves the row (one step); the source end
     to LFO 1 changes its source; the end let go on empty panel removes the row, UNDO restores it; every box control
     (AMOUNT < > and its drag, VIA's list, VIA AMOUNT < >, XFORM's list, its amount < >, ON / OFF) changes the right
     param as one undo step with the host told; 16 rows in use: the help line says so while dragging and after, nothing
     made. Paint on the PC (load_test, 2026-10-31, the machine busy with other jobs, so the numbers moved a lot between
     runs): the full repaint with cables 11.1-14.6 ms in the quieter runs (target 16; unchanged from item 36's 11.5);
     the amount knob dragged 1.2 ms (target 2; the box's value line repaints, not all of the box); a live cable's move
     repaints its own area (about 1100 x 660 px for a cable from the bay to the top right) in 5.8-7.2 ms with the
     cached layer not drawn again. In the busiest runs every paint timing, the panel's own included, went over its
     target. Screenshots: load_test's `_cables_dragging`, `_cables_box`, `_cables_box2`, `_cables_box_off`,
     `_cables_repatch`, `_cables_full`. Nothing in the audio path changes. Left: VIA as a second thin cable (VIA shows
     in the box and its list only); dragging a cable to a generator source that no row uses yet (GENERATIVE's DEST
     does it); hover highlighting of a whole cable; the MPC page.
  44. (2026-10-31) IMPORT .M1P: Moog One preset files into user patches (owner's decision 2026-10-31: Moo Won ships no
     Moog presets; the owner downloads or buys .m1p files and Moo Won converts them on his PC). Done 2026-10-31 (the
     desktop window; `desktop/m1p.c` the converter, `desktop/m1p_map.h` its tables, `desktop/m1p_import.c` the button,
     the dialog and the report; `docs/PATCHES.md` "Importing Moog One presets"). Every one a plain click:
     - IMPORT .M1P on the PATCHES page: a file dialog (opens in `Documents\Moog One presets` when it exists; one, many
       or Ctrl+A all files), each preset a user patch in the library in use, named after the preset, GROUP Imported with
       its TYPE, CATEGORY, MOOD, the Synths' names and its comment in the notes; DUPLICATE NAMES as for IMPORT.
     - The format, as read: plain text, "key value" a line (numbers, true / false, quoted words, lists). The oscillator,
       mixer and envelope keys are publicly documented; the rest was worked out from the value ranges of 47 files the
       owner downloaded (read only, never copied into the project): cutoffs are MIDI note numbers, times seconds, LFO
       rates Hz, oscillator frequency semitones. About 560 settings a preset convert (the Synths and their on/off,
       oscillators, mixer, noise, both filters and their routing, envelopes, LFOs, the panel's mod amounts, glide,
       unison, voices, key range, bend, the arp's main settings, the effects' types, the matrix with VIA and
       transforms); the sequencer, the effects' controls, sync divisions, CV / MIDI setups are listed in the report,
       not guessed. Rows the files don't settle are marked "a guess: check by ear" in the report.
     - Level check: each sound played once (a chord) in a separate engine instance on the window's thread; over -1
       dBFS, Volume down towards -4 dBFS (up to four tries); silence reported. About 0.15 s a file on the PC.
     - REPORT opens `Import report.txt` beside the patches; the message line has the summary.
     The owner's 47 files: all imported, none silent, 11 turned down; put in the user library "Moog One imports"
     (SETTINGS > LIBRARY to switch to it). Tests (offline): `test/m1p_check.c` on our own synthetic files in `test/m1p/`
     (every kind of conversion, the matrix, the labels, unknown keys reported, empty / other text / binary / too big /
     nothing known, the level trim); load_test `m1p_tests`: IMPORT .M1P and REPORT clicked with the dialog bypassed
     (TEST_PATCH PT_M1P_PICK), the patches, tags, sound, message, report, AUTO RENAME and IGNORE. Nothing in the audio
     path. Left: the sequences, the effects' own controls and sync divisions (their scales need more files to read),
     importing a whole folder from the window (the dialog's Ctrl+A does a folder at a time).
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
