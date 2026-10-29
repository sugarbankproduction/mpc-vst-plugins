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
  committed). `ports/one-ish/docs/MOOG_ONE_AUDIT.md` lists every Moog One feature against ONE-ish (2026-10-29: 364 have,
  40 partial, 71 missing, 27 of those hardware-only) and the build order, worked through in this order:
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
     slight bevel).
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
     the factory presets sound as before (within 1 LSB). Left: the window's pages for the new controls, effect presets. Tier 4: arpeggiator and chord memory done 2026-10-27
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
     keyboard settings, ribbon and link cutoff never touched. Left: per part (needs multitimbral), an MPC control.
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
     0.1 us per block, 8 modulation lanes 0.24 us per voice. Left: OUTPUT to Synth 1/2/3 [MULTI], an RTZ button, the
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
     Left: per-Synth outputs, solo, sequencer OUTPUT to another Synth, the sustain pedal's target, a range set by
     pressing a key, the SETTINGS page, NRPN; three Synths on the MPC is the owner's later call (`ONEISH_SYNTHS`).
  13. Audio input and the EXT channel (audit package 11, first half). Done 2026-10-29 (offline and the desktop DLL): the
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
