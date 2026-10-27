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

Updated 2026-10-27. Owner's instruction: build ONE-ish as a **local desktop VST2 first** and test it; port and adjust
for MPC afterwards. Work through the steps without stopping for approval at each one.

- **Done and pushed (branch `one-ish` on the owner's fork):** Phases 1-8 offline; 13 presets in the program list
  (`presets/make_presets.py` -> `presets.json`); fixes from the phase 6-8 code review. The desktop `ONE-ish.dll` is
  installed in `%USERPROFILE%\VST2` and plays in Carla.
- **Now (owner, 2026-10-27): match the Moog One's features, and give the desktop VST a front-panel window laid out
  like the instrument** (original artwork, no maker's names or marks; reference photos in `DOWNLOADS/`, not
  committed). `ports/one-ish/docs/MOOG_ONE_AUDIT.md` lists every Moog One feature against ONE-ish (79 have,
  84 partial, 301 missing, 78 of those hardware-only) and the build order, worked through in this order:
  1. Tier 1, the sound: dual-wave oscillators (wave angle, pulse width, mix, beat freq) with linear FM and ring mod;
     glide; envelope extras (delay, hold, loop, latch, sync, curves, velocity); the SVF pair (spacing, link cutoff,
     key tracking to 200%) and the panel's cutoff-modulation section; unison and detune.
  2. The front-panel window, laid out from the photos, with the keyboard, wheels and X/Y pad.
  3. Tier 2: LFOs 3-4 and their options, 16-slot matrix with transforms and the missing sources/destinations, noise
     colours and noise envelope, voice/keyboard options, stereo voices and pan, analog drift.
  4. Tier 3: the full effect list (original designs; the Eventide reverbs are licensed, so original reverbs in their
     spirit). Tier 4: arpeggiator, chord, then a decision with the owner on the sequencer and multitimbral parts.
  Parameters may be renamed or reordered freely until the first release (`GEN_VST_RELOCK=1`); presets are regenerated.
- **Then, the MPC port (in order):**
  1. MPC builds for armv7 + aarch64 and `tools/test_port.sh` x86 + aarch64, from a copy outside Dropbox (CLAUDE.md).
  2. Phase 9: touchscreen page (layout.conf, drawn from scratch, Moog One manual's panel as layout reference only, no
     Moog names/logos), Q-Links; then MPC builds, skin preview and a code review.
  3. Phase 10: decide with the owner whether multitimbral parts make sense on MPC (each track can host its own ONE-ish).
  4. Phase 11: package with `tools/release.py` locally; publishing and the final name are the owner's decisions.
- **Needs a real MPC (owner):** `tools/bench.sh` at 4 and 8 voices to set the default voice count; loading on the
  device. Offline estimate: about 5.3 us per voice per block on the PC (8 voices: 42 us of the 2902 us budget there).
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
