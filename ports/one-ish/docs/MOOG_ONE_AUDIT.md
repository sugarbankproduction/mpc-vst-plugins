# Moog One vs ONE-ish: feature and parameter gap audit

Date: 2026-10-27. Sources: the Moog One owner's manual (edition 8/2/19, 158 pages; page numbers below are the
printed page numbers, which match the PDF pages) read in full, and ONE-ish as of commit 1706085:
`ports/one-ish/params.json` (108 params), `src/engine.c` (header, constants, voice, MIDI and state code),
`src/osc.h`, `ladder.h`, `svf.h`, `env.h`, `lfo.h`, `modmatrix.h`, `fx.h`.

Manual wording is summarised, not quoted; only parameter names and ranges are copied.

## How to read this

**Status:** Have = ONE-ish does it (perhaps in a different form); Partial = some of it, or a fixed version of
something the Moog One makes adjustable; Missing = not there.

**Tags in the feature column:**
- **[HW]** hardware-only on the Moog One (jacks, panel, display, fans, storage). On an MPC plugin these are either
  the host's job or impossible; they are listed for completeness and are not build candidates.
- **[UI]** screen, menu or librarian behaviour, not sound. Some could be done on an MPC skin page; none change the sound.
- **[ANALOG]** analog circuit behaviour that software can only approximate (it can be modelled, never matched).
- **[LIC]** a licensed or trademarked algorithm (the Eventide reverbs). It cannot be copied; only an original
  effect in the same spirit can be built.
- **[MULTI]** only meaningful once ONE-ish has multitimbral parts.

**Effort:** S = a few hours, contained change (under ~100 lines); M = one to three days, new code in one module;
L = more than three days or a new subsystem.

**Per-voice CPU impact**, relative to one ONE-ish voice today (PLAN.md's offline estimate is about 5.3 us per voice per
128-sample block on the PC; nothing is measured on the MPC yet): none = no audio-path work; low = under ~10% of a
voice; medium = ~10-40%; high = over ~40%, or the feature adds whole voices. Effects and other post-voice work run
once, not per voice, so they read "none/voice" plus their shared cost.

**Not in the Moog One but in ONE-ish (kept for reference):** ladder Drive (`flt_drive`), Ladder Drive and Amp Level as
matrix destinations, the effects' CTRL 1/2 as offsets on their targets.

---

## 0. Architecture and overview (p8-10)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| All-analog voice path; digital FX can be bypassed for a 100% analog path [ANALOG] | p8, p96, p140 | Partial | Virtual-analog models: PolyBLEP oscillators, zero-delay-feedback ladder with one saturator, TPT SVF; no oversampling | L (to deepen the models) | medium-high |
| Polyphony | 8 or 16 voices (p8, p140) | Partial | `voices` Mono/2/4/6/8 (MAX_VOICES 8) | S for more counts; 16 is CPU-bound | high (each voice) |
| Three-part multitimbral: up to 3 Synths per Preset, split, layered or zoned [MULTI] | p8-9 | Missing | One part. On the MPC, 3 instances on 3 tracks get close | L | high (voices are shared) |
| Per-Synth Timbre: own sound, note range, voice count, MIDI channel, arp, sequencer [MULTI] | p9 | Missing | - | L | none |
| Hardware voices shared between Synths at will [MULTI] | p9 | Missing | - | M | none |
| One Synth Effect per Synth plus two Master Effects on sends | p9, p96-101 | Partial | One fixed insert chain: mod FX, delay, reverb | M | none/voice; shared medium |
| Voice: 3 VCOs, ring mod, noise, external input, mixer to SVF pair and ladder, VCA | p9-10 | Partial | 3 oscillators + noise to SVF and ladder to amp; no ring mod, no external input, one SVF | see sections 3-7 | - |
| Each Synth: 4 LFOs and 3 EGs per voice (manual wording p10) | p10 | Have | 3 EGs and 4 LFOs per voice: an LFO runs per voice when it restarts per note or the matrix moves it per voice, else every voice follows one free-running LFO (2026-10-27, offline) | - | - |
| Modulation: hard-wired, Quick Assign (DEST buttons), Mod Matrix with controllers and transforms | p10, p67 | Partial | Hard-wired pitch and cutoff amounts, 8-slot matrix with "via"; no transforms, no quick assign | M | low |
| Programmable: every knob and hidden parameter stored in Presets | p8 | Have | Full state string per instance (`get_state`/`set_state`), 13 programs | - | none |

## 1. Front panel, centre console, keyboard (p10-18)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Traditional knobs, soft knobs, master encoder; lit and red buttons [HW] | p11 | Missing | MPC knobs and Q-Links (host) | - | none |
| MORE pages: a second layer of parameters per module [UI] | p11, every module | Partial | One flat parameter list grouped in `sections` | M (skin pages) | none |
| DEST button on each LFO and EG: next touched control becomes a destination [UI] | p11, p43, p47, p68 | Missing | Matrix slots set by hand | M | none |
| HOME screen: preset, timbre names, key ranges, synth on/off, tempo, tags [UI] | p12-13 | Missing | MPC's own plugin page | - | none |
| SETTINGS, MOD, DELETE, SNAPSHOT, SAVE, COMPARE buttons [UI] | p13-14 | Missing | See sections 15, 20, 21 | - | none |
| SHIFT: fine resolution on soft knobs, latching shift [UI] | p14 | Partial | Host parameter resolution | - | none |
| New initialised preset (SHIFT + COMPARE) | p14 | Have | "Init" program | - | none |
| Panel Focus: SYNTH 1/2/3, multi-select for joint edits, SHIFT = solo [UI][MULTI] | p14-15 | Missing | - | M | none |
| Keyboard Control: per-Synth ON, LOW/HIGH RANGE key zone, both = full G0..C10 [MULTI] | p15 | Missing | MPC track note range can stand in | S (with parts) | none |
| Expression Assign buttons: MW, PAD X, PAD Y, KB, VEL, AT, EXP 1 [UI] | p15, p68 | Missing | Matrix slots set by hand | M | none |
| Selecting presets from the browser [UI] | p15-16 | Partial | VST program list (13) | - | none |
| 61-key keybed, velocity and aftertouch [HW] | p17, p140 | Have | Via MIDI: velocity to amp, channel pressure as a matrix source | - | none |
| Selectable velocity curves | p17, p116 | Missing | See section 19 | S | none |
| OCTAVE buttons; both = default; hold both = MIDI panic | p17, p112 | Partial | CC 120/123 (all off) and 121 handled; no octave shift parameter | S | none |
| Oscillator OLED waveform displays [UI] | p10, p22 | Missing | Could be drawn on the MPC skin | M | none |

## 2. Rear panel (p19-21)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MAIN L/R and SUB L/R balanced outs, each Synth to either or both [HW] | p19 | Partial | One stereo output into the MPC mixer | - | none |
| INSERT 1-4 send/return jacks, stereo-normalled pairs [HW] | p19, p56 | Missing | - | - | none |
| EXT INPUT: XLR/TRS combo, TRIM -9 dB to +65 dB; LINE input [HW] | p20 | Missing | An MPC instrument plugin gets no audio input | - | none |
| CV IN 1-2, CV OUT 1-4; pedal jacks usable as CV inputs [HW] | p20-21 | Missing | - | - | none |
| MIDI IN/THRU/OUT (DIN), USB MIDI, USB host, LAN [HW] | p20-21 | Partial | MIDI in from the host only | - | none |
| Pedal jacks: SUSTAIN, EXP 1, EXP 2, assignable | p21, p140 | Partial | Sustain (CC 64) and sostenuto (CC 66); CC 11 and CC 4 are the matrix sources Exp 1 and Exp 2 (2026-10-27, offline) | S | none |

## 3. Oscillators (p22-26)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Three VCOs per voice | p22 | Have | `osc[3]` per voice | - | - |
| Two generators per VCO running at once: tri/saw plus pulse | p22 | Have | `src/osc.h`: both waves from one phase, blended by `oN_mix` (2026-10-27) | - | - |
| OCTAVE | 32', 16', 8', 4', 2' (p22) | Have | `o1_oct`..`o3_oct` | - | none |
| FREQUENCY, continuous | -7 to +7 semitones (p22) | Have | `oN_freq` -7..+7 st, continuous; matrix "Osc n Pitch" | - | - |
| BEAT FREQ: constant offset in Hz, same beat rate across the keyboard | -7 to +7 (Hz, p23, p140) | Have | `oN_beat` -7..+7 Hz, added after the pitch | - | - |
| WAVE button / VCO MODE | TRI, SAW (p23, p26) | Have | `oN_wave` Tri / Saw | - | - |
| WAVE ANGLE, TRI mode: saw to triangle to ramp, rise and fall both scale with pitch | knob, centre = triangle (p23) | Have | `oN_angle` in Tri mode: variable-slope triangle, PolyBLAMP corners | - | - |
| WAVE ANGLE, SAW mode: reset time of the saw, darker as it lengthens [ANALOG] | 6 us to 1 ms (p24) | Have | `oN_angle` in Saw mode: reset 6 us .. 1 ms (log), PolyBLAMP | - | - |
| PULSE WIDTH: narrow, square at centre, wide | knob (p24) | Have | `oN_pw` 5..95%, PolyBLEP at both edges, DC removed | - | - |
| MIX: tri/saw only, equal blend, pulse only | knob (p24) | Have | `oN_mix` 0..100% | - | - |
| Wave angle and pulse width modulatable (PWM etc.) | p22 | Have | Matrix destinations "Osc n Angle", "Osc n PW", "Osc n Wave Mix", ramped per sample | - | - |
| HARD SYNC on OSC 2 and OSC 3 to OSC 1 | on/off (p24-25) | Have | `o2_sync`, `o3_sync`, band-limited reset | - | - |
| HARD SYNC SRC (MORE) | OSC 2: VCO 1 or VCO 3; OSC 3: VCO 1 or VCO 2 (p26) | Have | `o2_sync_src` Osc 1/Osc 3, `o3_sync_src` Osc 1/Osc 2; osc 2 from osc 3 looks ahead to osc 3's wrap (one render order), a restart counts as a wrap so a chain 1 > 2 > 3 follows; 2 and 3 syncing each other: osc 3 takes osc 2's wrap one sample late (2026-10-27, offline) | - | none |
| PITCH TRACKING (MORE) | 0% to 200% (p26) | Have | `oN_track` 0-200%: scales the key's distance from the key track centre (0%: the centre note's pitch on every key) (2026-10-27, offline) | - | none |
| NOTE-ON RESET (MORE): phase to zero on each note | off/on (p26) | Have | `oN_reset`: a band-limited restart at phase 0 on every note-on, legato ones too; an oscillator hard-synced to it restarts with it (2026-10-27, offline) | - | none |
| BEND UP OFFSET per oscillator | -24 to +24 st (p26) | Have | `oN_bend_up_off`, whole semitones, added to `bend_up` (2026-10-27, offline) | - | none |
| BEND DOWN OFFSET per oscillator | -24 to +24 st (p26) | Have | `oN_bend_dn_off`, added to `bend_down` (2026-10-27, offline) | - | none |
| MORE page wave image and values [UI] | p25 | Missing | - | M | none |
| Analog VCO character: slight drift and warm-up instability [ANALOG] | p7 (warm-up note) | Missing | Ideal digital oscillators | S-M (slow per-oscillator random pitch wander) | low |
| Clean top end under sync and FM | implied (analog) | Partial | PolyBLEP; no oversampling | M-L | medium-high (2x oversampling) |

## 4. Oscillator common: pitch mod, ring mod, wave mod, FM (p27-28)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| PITCH MOD: LFO 1 AMT to all three oscillators | 0 to 10 (p27) | Have | `pitch_lfo` (+-100% = +-12 st) | - | - |
| PITCH MOD: MOD EG AMT to all three oscillators | -10 to +10 (p27) | Have | `pitch_env` (+-100% = +-24 st) | - | - |
| RING MOD as its own mixer channel | p27, p31 | Have | `ring_level`, with its own SVF/Ladder buttons | - | - |
| RING MOD SOURCES, tapped from the tri/saw generators only | 1x2, 2x3 (p27) | Have | `ring_src` 1 x 2 / 2 x 3, from the tri/saw waves only | - | - |
| WAVE ANGLE / LFO 3 AMT | 0 to 10 (p28) | Have | `angle_lfo3` 0..100%: at 100% LFO 3 moves every oscillator's wave angle +-50% of its range (2026-10-27, offline) | - | - |
| PULSE WIDTH / LFO 3 AMT | 0 to 10 (p28) | Have | `pw_lfo3` 0..100%: at 100% +-45% pulse width (2026-10-27, offline) | - | - |
| FM AMT, linear FM | 0 to 10 (p28) | Have | `fm_amt`: the carrier's frequency x (1 + 2 x amount x modulator), per sample, not through-zero; matrix "FM Amount" | - | - |
| FM ROUTE | 1 to 2, 1 to 3, 3 to 1 (p28) | Have | `fm_route` 1 > 2, 1 > 3, 3 > 1 (3 > 1 uses osc 3's previous sample) | - | - |

## 5. Noise (p29-30)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Noise as a mixer source | p29 | Have | `noise_level`, per-voice xorshift white noise | - | - |
| Noise EG: ATTACK | 1 ms to 10 s (p29) | Missing | Noise follows the amp envelope only | S | low |
| Noise EG: RELEASE | 1 ms to 10 s (p29) | Missing | - | S | low |
| Noise EG: SUSTAIN button (off: release acts as decay after the attack) | on/off (p29) | Missing | - | S | none |
| Noise EG as a matrix source; its stages as destinations | p29, p75-76 | Missing | - | S | low |
| Colours: white, red (-6 dB/oct), purple (+6 dB/oct) | p30 | Partial | White only | S (one-pole lowpass, first difference) | low |
| COLOR pair select | Red+White, Red+Purple, White+Purple (p30) | Missing | - | S | none |
| COLOR MIX between the two selected colours (also a matrix destination) | knob (p30, p76) | Missing | - | S | low |
| Pink noise by mixing red and white | p30 | Missing | Follows from the above | - | - |

## 6. Mixer (p31-32)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Six input channels: OSC 1-3, RING MOD, NOISE, EXT | p31 | Partial | Five: oscillators 1-3, ring mod, noise (EXT is hardware-only) | - | - |
| Level knob per channel (oscillators, noise) | 0 to max (p31-32) | Have | `o1_level`..`o3_level`, `noise_level` | - | - |
| RING MOD level | p31 | Have | `ring_level`; matrix "Ring Level" | - | - |
| SVF and LADDER bus buttons per channel (either, both, neither) | p31-32 | Have | `oN_svf`/`oN_lad`, `noise_*`, `ring_*`: two buttons per channel | - | - |
| EXT INPUT LEVEL [HW] | 0% to 100% (p32) | Missing | No audio input on an MPC instrument plugin | - | none |
| EXT INPUT ROUTE [HW] | None, SVF, Ladder, Both (p32) | Missing | - | - | none |
| EXT INPUT SOURCE [HW] | None, Mic/Line, Line, Mic/Line+Line (p32) | Missing | - | - | none |
| MORE page bar-graph of levels and buses [UI] | p32 | Missing | - | - | none |
| Mixer levels as matrix destinations | p76 | Have | "Osc n Level", "Noise Level" | - | - |

## 7. Filters (p33-39)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Three analog filters: an SVF pair acting as one, plus a ladder | p33 | Have | Two TPT SVF elements as one (`src/svf.h` svf2_t) + ladder (2026-10-27, offline) | - | - |
| SVF CUTOFF | 20 Hz to 20 kHz (p33) | Have | `svf_cut` (log 20 Hz-20 kHz) | - | - |
| SVF RESONANCE | 0 to 10 (p33) | Have | `svf_res` (peak up to +20 dB; does not self-oscillate) | - | - |
| SVF MODE | Notch, BP, HP, LP (p34) | Have | `svf_mode` LP/BP/HP/Notch | - | - |
| SVF slope from the two elements | -12 dB or -24 dB (spec p140) | Have | 24 dB = `svf_pair` Series; 12 dB = Parallel at spacing 0 (exactly the old single SVF, the default) | - | - |
| SVF SPACING: element B's cutoff above or below A | -100% to +100% (p38) | Have | `svf_space`, +-100% = +-4 octaves | - | low (second element, ~10 multiplies a sample; none at spacing 0 in parallel) |
| SVF SERIES/PARALLEL (inside the SVF) | Series, Parallel (p38) | Have | `svf_pair`; series gives each element the square root of the damping so the peak matches one element's; parallel averages the two | - | low |
| LADDER CUTOFF | 20 Hz to 20 kHz (p34) | Have | `flt_cut` | - | - |
| LADDER RESONANCE, self-oscillates to a sine | 0 to 10 (p34) | Have | `flt_res` (feedback up to 4.4) | - | - |
| LADDER MODE | HP, LP (p35) | Have | `flt_mode` | - | - |
| LADDER SLOPE | 24, 18, 12, 6 dB (p35) | Have | `flt_slope` | - | - |
| Transistor-ladder character: per-stage non-linearity, how resonance thins the bass [ANALOG] | p34 | Partial | Linear ZDF ladder, one input saturator, half bass compensation | L | medium-high (per-stage saturation needs an iterative solve or 2x oversampling) |
| LINK CUTOFF: either knob moves both, offset kept, clamps recover | on/off (p35) | Have | `flt_link` (in set_param; a state restores both knobs as saved; presets set it Off first, see make_presets.py) | - | none |
| ROUTE: SVF and ladder side by side, or SVF into ladder | PAR, SER (p36) | Have | `flt_route` | - | - |
| MIX between filter outputs | SVF to ladder (p36) | Have | `flt_mix` | - | - |
| SVF MOD / LADDER MOD: which filter the three knobs edit [UI] | on/off each (p36-37) | Have | Separate amount knobs per filter do the same job | - | - |
| EG AMT per filter (filter envelope to cutoff) | -10 to +10 (p37) | Have | `lad_env`, `svf_env` | - | - |
| LFO 2 AMT per filter | 0 to 10 (p37) | Have | `lad_lfo`, `svf_lfo` (bipolar) | - | - |
| FM AMT per filter: audio-rate cutoff FM from an oscillator | 0 to 10 (p37) | Have | `lad_fm`, `svf_fm` (100% = +-4 octaves at a full-scale sample); per-sample g scaled by a fast 2^x, no tanf | - | medium when on (about +35% a voice with both on, PC 2026-10-27); none when off |
| SVF FM SOURCE (MORE) | OSC 1, OSC 2, OSC 3 (p38) | Have | `svf_fm_src` (a silent oscillator runs when it is the source) | - | none |
| LADDER FM SOURCE (MORE) | OSC 1, OSC 2, OSC 3 (p38) | Have | `lad_fm_src` | - | none |
| SVF KEY TRACK | -200% to +200% (p39) | Have | `svf_kbd` -200% to +200% | - | - |
| LADDER KEY TRACK | -200% to +200% (p39) | Have | `lad_kbd` -200% to +200% | - | - |
| Filter MORE page diagram [UI] | p38 | Missing | - | - | none |

## 8. Envelopes (p40-45)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Three EGs per voice: filter, amplifier, modulation | p40 | Have | `fenv_*`, `amp_*`, `menv_*` | - | - |
| DELAY TIME (MORE) | 0.0 ms to 10.0 s (p44) | Have | `*_delay`: 0 = off, then 1 ms to 10 s on the time knobs' log scale; holds the current level, then the attack starts (2026-10-27, offline) | - | none |
| ATTACK | 1 ms to 10 s, plus MIN (p41) | Have | `*_a` 1 ms to 10 s, log knob; curve set by `*_acurve` (default linear) | - | - |
| HOLD TIME (MORE) | 0.0 ms to 10.0 s (p44) | Have | `*_hold`: 0 = off, then 1 ms to 10 s; stays at the peak (2026-10-27, offline) | - | none |
| DECAY | 1 ms to 10 s (p41) | Have | `*_d`; curve `*_dcurve` (default 50% exp = the old one-pole decay, 99% in the set time) | - | - |
| SUSTAIN | 0 to 10 (p41) | Have | `*_s` 0-100% | - | - |
| RELEASE | 1 ms to 10 s (p41) | Have | `*_r`; curve `*_rcurve` (default 50% exp); now ends at exactly the set time | - | - |
| MIN setting below 1 ms, ignored by TIME SCALE | p40 | Partial | Shortest time 1 ms | S | none |
| ATTACK CURVE | 100% log to linear to 100% exp (p45) | Have | `*_acurve` -100..100%: one shared shaper (u = u m + b per sample), every curve takes the stage's whole time (2026-10-27, offline) | - | none (faster than before: control-rate envelopes step 16 samples at once) |
| DECAY CURVE | 100% log to linear to 100% exp (p45) | Have | `*_dcurve` -100..100%, default 50 (2026-10-27, offline) | - | none |
| RELEASE CURVE | 100% log to linear to 100% exp (p45) | Have | `*_rcurve` -100..100%, default 50 (2026-10-27, offline) | - | none |
| MULTI TRIG: legato notes restart the envelope or not | on/off (p42) | Have | `*_multi` (default on): off, a new note on a voice whose key is still down carries on from the envelope's current position (2026-10-27, offline) | - | none |
| RESET: restart the attack from zero (mono/unison with multi trig) | off/on (p44) | Have | `*_reset`: every restart from zero (also a stolen or replayed voice in poly) (2026-10-27, offline) | - | none |
| SYNC: stage times as tempo divisions | 1/64 to 64 (13 values, p42) | Partial | `*_sync`: the five time knobs pick from the delay's 11 divisions (1/32 to 4 bars, longest on the right) at the host tempo; time scale does not apply (2026-10-27, offline) | S | none |
| LOOP: repeat the whole DAHDSR (trapezoid) | on/off (p42) | Have | `*_loop`: after the decay it goes straight to the release, then the delay again, while held (2026-10-27, offline) | - | none |
| LATCH: hold at sustain after key up; with LOOP keeps cycling | on/off (p42) | Have | `*_latch`: key up ignored until the voice is replayed or stolen, latch is switched off or All Notes Off (2026-10-27, offline) | - | none |
| TIME SCALE, keeps the stage ratios; modulatable | 10% to 1000% (p44) | Partial | `*_scale` 10-1000%, log knob with 100% in the middle; not a matrix destination yet (2026-10-27, offline) | S | none |
| VEL MOD AMT: velocity scales the envelope output | -100% to +100% (p45) | Have | `*_vel`: amp default 100% (as before), filter and mod 0%; also scales the envelope as a matrix source (2026-10-27, offline) | - | none |
| DEST quick assign [UI] | p43 | Missing | - | M | none |
| Envelope picture with curves [UI] | p43, p45 | Missing | - | - | none |
| EG stage times and time scale as matrix destinations | p76 | Missing | See section 15 | M | low-medium |

## 9. LFOs (p46-49)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Four LFOs (hard-wired: 1 to pitch, 2 to cutoffs, 3 to wave shape) | p46, p27-28 | Have | `lfo1_*` .. `lfo4_*`; LFO 1 to pitch, LFO 2 to cutoffs, LFO 3 to wave angle and width (2026-10-27, offline) | - | - |
| LFOs per voice (manual p10) so they can restart per note | p10 | Have | Per voice when restarted (note-on reset, repeats) or modulated per voice; free-running otherwise (2026-10-27, offline) | - | - |
| RATE | 0 to 10 within the RANGE (p46) | Have | `lfoN_rate`, three decades on a log knob within the range (2026-10-27, offline) | - | - |
| RANGE | Slow 0.001-1 Hz, Low 0.01-10 Hz, Medium 0.1-100 Hz, High 1 Hz-1 kHz (p48) | Partial | `lfoN_range` Slow, Low (default), Medium; no High (1 Hz-1 kHz needs per-sample LFOs: the control rate is 2.76 kHz) (2026-10-27, offline) | S (High) | medium for High |
| WAVE: Triangle/Sine | p46 | Have | Triangle and Sine, each morphing into the other with `lfoN_var` (2026-10-27, offline) | - | - |
| WAVE: Pulse with variable width | p46 | Have | Square; `lfoN_var` narrows it from 50% to always low (2026-10-27, offline) | - | - |
| WAVE: Saw/Ramp | p46 | Have | Saw and Ramp, each morphing through a triangle into the other (2026-10-27, offline) | - | - |
| WAVE: S&H/Noise (smooth random) | p46 | Have | S&H and Noise (smooth random, the same random steps glided), each morphing into the other (2026-10-27, offline) | - | - |
| VARIATION: morph within the wave category | 0% to 100% (p48-49) | Have | `lfoN_var`, from the chosen wave towards its partner; a matrix destination (2026-10-27, offline) | - | - |
| POLARITY | Unipolar, Bipolar (p49) | Have | `lfoN_polarity` Bipolar / Unipolar (2026-10-27, offline) | - | - |
| START PHASE (used on reset, repeats, clock start) | 0% to 100% (p49) | Partial | `lfoN_phase`, on note-on reset and repeats; host transport start not read (2026-10-27, offline) | S | none |
| SYNC to the Master Clock | on/off (p49) | Have | `lfoN_sync`, host tempo | - | - |
| SYNC DIVISION | 64, 32, 16, 8, 4, 2, 1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64 (p49) | Have | 13 divisions, 16 bars .. 1/256 (64 .. 1/64 quarter-note beats), a table of the LFOs' own (2026-10-27, offline) | - | - |
| SYNC TIMING | Straight, Dotted, Triplet (p49) | Have | `lfoN_timing` Straight, Dotted (x1.5), Triplet (x2/3) (2026-10-27, offline) | - | - |
| Reset to start phase on Master Clock start when synced | p49 | Missing | Host transport not read | S | none |
| NOTE-ON RESET | off/on (p49) | Have | `lfoN_reset`: the voice restarts its LFO at the start phase, with its own random values (2026-10-27, offline) | - | - |
| DELAY TIME before the LFO starts | Off, 0.1 ms to 10.0 s (p50) | Have | `lfoN_delay` Off, 1 ms .. 10 s, per voice from the note-on; a restarted LFO waits at its start phase (2026-10-27, offline) | - | - |
| FADE IN TIME | Off, 0.1 ms to 10.0 s (p50) | Have | `lfoN_fade_in` Off, 1 ms .. 10 s (2026-10-27, offline) | - | - |
| FADE OUT TIME | Off, 0.1 ms to 10.0 s (p50) | Have | `lfoN_fade_out` Off, 1 ms .. 10 s, after the fade in (2026-10-27, offline) | - | - |
| REPEATS: a set number of cycles after note-on | Infinite, 1 to 32 (p50) | Have | `lfoN_repeats` Inf, 1..32; then the LFO stays at 0 until the next note-on (2026-10-27, offline) | - | - |
| SMOOTHING: one-pole lowpass on the LFO | 0% to 100% (p50) | Have | `lfoN_smooth`: cutoff 1 kHz at 1% down to 0.1 Hz at 100% (2026-10-27, offline) | - | - |
| DEST quick assign; wave picture [UI] | p47-48 | Missing | - | M | none |

## 10. VCA (p50-52)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| LEVEL of the active Synth | 0 to 10 (p51) | Have | `volume` | - | - |
| PAN of the active Synth | Left to Right (p51) | Missing | Centre only | S | none |
| VCA MORE: level and pan of all three Synths on one page [MULTI][UI] | 0-100%, 100% L to 100% R (p52) | Missing | - | S (with parts) | none |
| SPREAD: per-voice stereo placement around the pan | 0% to 100% (p52) | Missing | Voices sum to mono | M (stereo voice bus) | low |
| Per-voice pan as a matrix destination | p76, p96 | Missing | - | M | low |

## 11. Output (p53-56)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MASTER VOLUME, HEADPHONES (global, never stored) [HW] | 0 to 10 (p53) | Missing | MPC master and headphone levels | - | none |
| Per-Synth routing to MAIN and SUB outputs [HW][MULTI] | on/off each (p54) | Missing | Host routing | - | none |
| SUB VOLUME [HW] | -96 dB to +22 dB (p54) | Missing | - | - | none |
| Per-Synth routing to INSERT 1-4, one Synth per insert; EXT 1/4" to Insert 3, EXT XLR to Insert 4 [HW] | on/off (p54-55) | Missing | - | - | none |
| INSERT 1-4 VOLUME [HW] | -96 dB to +22 dB (p55) | Missing | - | - | none |
| Synth Effect applied before the insert send | p56 | Missing | - | - | none |

## 12. Performance Sets and text entry (p57-60)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Performance Sets: up to 128 sets of 64 preset pointers (banks A-H x 1-8) [UI] | p57, p141 | Partial | VST program list of 13 presets; MPC programs do the rest | - | none |
| Assign current preset by long press; dim/lit/dark buttons [HW][UI] | p57 | Missing | - | - | none |
| CURRENT SET, BROWSE SETS, ARRANGE SETS: move, copy, delete, add, name, notes [UI] | p58-59 | Missing | - | - | none |
| Remote MIDI program and bank selection of sets | p57 | Partial | Host program selection of the 13 presets | - | none |
| Text entry from the keyboard or a USB keyboard [UI] | p60, p143 | Missing | Host naming | - | none |

## 13. Master Clock (p60-62)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| TEMPO | 10 BPM to 280 BPM (p61) | Partial | Follows host tempo (`lfo_bpm`, 20-400 BPM) | - | none |
| TAP tempo [HW] | p61 | Missing | Host | - | none |
| NUDGE + / - and NUDGE MODE [HW] | Increment (1 or 0.1 BPM), Bend (2 or 1 BPM) (p62) | Missing | Host | - | none |
| SWING AMOUNT (for arp and sequencer) | 0% to 50% (none) to 100% (p62) | Missing | - | S | none |
| LOAD PRESET TEMPO | on/off (p62) | Missing | Host owns tempo | - | none |
| External clock overrides the Master Clock | p60 | Partial | Host tempo only; no transport start/stop | - | none |
| What can sync: arp, sequencer, LFOs, EGs, effects | p60 | Partial | LFOs and the delay | see 8, 9, 16, 18 | none |

## 14. Voice allocation: Hold, Chord, Polyphony (p63-66)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| HOLD: latch held notes; legato adds, non-legato replaces; with sustain it adds; latches the arp | on/off (p63) | Have | `hold`: as the manual; off lets go of what it latched (the pedals keep theirs); no arp yet (2026-10-27, offline) | - | none |
| CHORD memory: one key plays a stored interval stack, first note is the root | on/off (p63) | Missing | - | M | high (multiplies sounding voices) |
| CHORD LEARN, saved per Synth; silent or audible learn (Settings) [UI] | p63, p125 | Missing | - | M | none |
| Chord is applied after the arp and sequencer | p63 | Missing | - | S (with chord + arp) | none |
| MONO | on/off (p64) | Have | `voices` = Mono, legato, last-note priority | - | - |
| UNISON: all voices stack, divided among held keys | on/off (p64) | Partial | `unison`: fixed stacks of `uni_count` voices per note, a stack allocated and stolen whole (8 voices, count 4: two notes); stacks don't re-divide as keys are added (2026-10-27, offline) | M | high (each stacked voice costs a full voice) |
| DETUNE of the unison stack, uneven spread around the centre | 0 to 10 (p64) | Have | `uni_detune` 0-100%: voices at u*sqrt(abs(u)) of +-50 cents (u evenly -1..1), symmetric, the centre voice stays put; stacks scaled by 1/sqrt(n) in level (2026-10-27, offline) | - | none |
| UNISON COUNT (also works in poly mode) | 2 to POLY COUNT (p65) | Have | `uni_count` 2-8, capped at the voice count in poly; mono + unison stacks it on one note (2026-10-27, offline) | - | high |
| POLY COUNT | 2 to hardware voices (p65) | Have | `voices` Mono, 2, 3, 4, 5, 6, 7, 8 (2026-10-27, offline) | - | high per voice |
| VOICE STEALING MODE | No Stealing, Oldest, Quietest, Newest (p66) | Have | `steal_mode`, after an idle voice: Oldest (default, the old behaviour: quietest releasing, then oldest held by a pedal or HOLD, then oldest held), Quietest (lowest level x velocity of all), Newest (quietest releasing, then newest), No Stealing (quietest releasing, else the note is dropped) (2026-10-27, offline) | - | none |
| MONOPHONIC NOTE PRIORITY | Low, High, Last (p66) | Have | `note_priority`; a key that loses to a held one changes nothing; releasing the sounding key moves to the next by priority, without a retrigger (2026-10-27, offline) | - | none |
| PITCH VARIANCE: random pitch per new note | 0 to 50 cents (p66) | Have | `pitch_var` 0-50 cents, uniform, drawn per voice for each new note (2026-10-27, offline) | - | none |

## 15. Modulation (p67-77)

### 15a. Structure

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Hard-wired: pitch (LFO 1, Mod EG), wave (LFO 3 to angle and width), FM, cutoff (EG, LFO 2, FM) | p67 | Have | All of them; each amount is also a matrix destination (2026-10-27, offline) | - | - |
| Quick Assign: DEST and Expression Assign; ADD DESTINATION, EDIT MODULATIONS, DONE, CANCEL [UI] | p68-69 | Missing | - | M | none |
| Matrix slots (numbered list; the count is not stated in this manual) | p70 | Have | 16 slots (`mod1_*` .. `mod16_*`) (2026-10-27, offline) | - | low (control rate; empty slots skipped) |
| Per-slot DISABLE/ENABLE without deleting | p70 | Partial | Set the source to Off or the amount to 0 | S | none |
| SOURCE AMOUNT | -300% to +300% (p70) | Have | `modN_amt` -300% .. +300% (2026-10-27, offline) | - | - |
| CONTROLLER with its own amount (negative inverts; a controller alone acts as a source) | -100% to +100% (p71) | Have | `modN_via` with `modN_via_amt` -100..+100% (default +100%); a slot with only a controller uses it as the source (2026-10-27, offline) | - | - |
| Order: controller and transform first, source amount last | p70 | Have | As the manual (2026-10-27, offline) | - | - |
| TRANSFORM stage | see 15d (p71) | Have | `modN_xf` and `modN_xf_amt` (2026-10-27, offline) | - | - |
| DESTINATION chosen as module, then parameter | see 15e (p71) | Partial | One flat list of 15 destinations | S | none |
| DELETE a slot (later slots renumber); filter list by source or destination [UI] | p72 | Missing | - | - | none |
| SAVE / LOAD (add or replace) whole matrices as named presets with notes [UI] | p73-74 | Missing | - | M | none |

### 15b. Matrix sources (p75)

| Moog One source | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|
| NONE | Have | "Off" | - | - |
| LFO 1 | Have | "LFO 1" | - | - |
| LFO 2 | Have | "LFO 2" | - | - |
| LFO 3 | Have | "LFO 3" | - | - |
| LFO 4 | Have | "LFO 4" | - | - |
| FILTER EG | Have | "Filter Env" | - | - |
| AMPLIFIER EG | Have | "Amp Env" | - | - |
| MODULATION EG | Have | "Mod Env" | - | - |
| NOISE EG | Missing | - | S (with noise EG) | low |
| MOD WHEEL | Have | "Mod Wheel" (CC 1) | - | - |
| PITCH WHEEL | Have | "Pitch Wheel" (-1..1) | - | - |
| PAD X [HW] | Missing | Could map to a MIDI CC | S | none |
| PAD Y [HW] | Missing | Could map to a MIDI CC | S | none |
| PAD PRESSURE [HW] | Missing | Could map to a MIDI CC | S | none |
| PAD GATE [HW] | Missing | - | S | none |
| VELOCITY | Have | "Velocity" | - | - |
| AFTERTOUCH | Have | "Aftertouch" (channel pressure) | - | - |
| KEY PITCH | Have | "Keyboard" (-1..1 around middle C) | - | - |
| KB GATE | Have | "KB Gate" (the voice's key is down) | - | - |
| SUSTAIN | Have | "Sustain" (CC 64) | - | - |
| EXP 1 | Have | "Exp 1 (CC 11)" | - | - |
| EXP 2 | Have | "Exp 2 (CC 4)" | - | - |
| CV IN 1 [HW] | Missing | - | - | none |
| CV IN 2 [HW] | Missing | - | - | none |
| RANDOM (unipolar) | Have | "Random Uni" (0..1, per note) | - | - |
| RANDOM BIPOLAR | Have | "Random" (-1..1, per note) | - | - |
| CONSTANT | Have | "Constant" | - | - |

### 15c. Matrix controllers (p75)

| Moog One controller list | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|
| Same 26 entries as the sources (NONE, MOD WHEEL, PITCH WHEEL, PAD X/Y/PRESSURE/GATE, VELOCITY, AFTERTOUCH, KEY PITCH, KB GATE, SUSTAIN, EXP 1/2, CV IN 1/2, the four EGs, LFO 1-4, RANDOM, RANDOM BIPOLAR, CONSTANT) | Partial | `modN_via` offers the same list as the sources (19 + None); missing: pad X/Y/pressure/gate, CV, noise EG (2026-10-27, offline) | S | none |

### 15d. Matrix transforms (p71, p75)

| Moog One transform | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| NONE | - | Have | (no transform stage) | - | - |
| LIMIT LOW | 0% to 100% | Have | "Limit Low": a floor 0..100% across the source's range (0% does nothing) | - | - |
| LIMIT HIGH | 0% to 100% | Have | "Limit High": a ceiling (100% does nothing) | - | - |
| SQUARED | fixed | Have | "Squared" | - | - |
| CUBED | fixed | Have | "Cubed" | - | - |

### 15e. Matrix destinations (p76-77)

| Moog One destination | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|
| All oscillators: PITCH LFO 1 MOD (the hard-wired amount) | Have | "Pitch LFO1 Amt" | - | - |
| All oscillators: PITCH EG MOD | Have | "Pitch EG Amt" | - | - |
| All oscillators: TRIANGLE LFO 3 MOD | Have | "Angle LFO3 Amt" | - | - |
| All oscillators: PULSE LFO 3 MOD | Have | "PW LFO3 Amt" | - | - |
| All oscillators: FM MOD | Have | "FM Amount" | - | - |
| All oscillators: PITCH OFFSET | Have | "Pitch" (+-24 st) | - | - |
| Oscillator 1-3: FREQUENCY | Have | "Osc 1/2/3 Pitch" | - | - |
| Oscillator 1-3: BEAT | Have | "Osc n Beat" (+-7 Hz) | - | - |
| Oscillator 1-3: SAW (reset-time angle) | Have | "Osc n Angle" | - | - |
| Oscillator 1-3: TRIANGLE (angle) | Have | "Osc n Angle" | - | - |
| Oscillator 1-3: PULSE (width) | Have | "Osc n PW" | - | - |
| Oscillator 1-3: MIX | Have | "Osc n Wave Mix" | - | - |
| Oscillator 1-3: OCTAVE (step-mod list, p91) | Missing | - | S | none |
| Noise: COLOR MIX | Missing | Needs colours | S | none |
| Noise: ATTACK, SUSTAIN, RELEASE (noise EG) | Missing | Needs noise EG | S | none |
| Mixer: OSC 1, 2, 3 LEVEL | Have | "Osc n Level" | - | - |
| Mixer: RMOD LEVEL | Missing | Needs ring mod | S | none |
| Mixer: NOISE LEVEL | Have | "Noise Level" | - | - |
| Mixer: EXT LEVEL [HW] | Missing | No audio input | - | none |
| VCA: LEVEL | Have | "Amp Level" (x0..x2) | - | - |
| VCA: PAN | Missing | Needs stereo voices | M | low |
| Filters: SVF/LADDER MIX | Have | "Filter Mix" | - | - |
| SVF: CUTOFF | Have | "SVF Cutoff" (+-5 oct) | - | - |
| SVF: RESONANCE | Have | "SVF Res" | - | - |
| SVF: SPACING | Have | "SVF Spacing" | - | - |
| SVF: EG AMOUNT | Have | "SVF EG Amt" | - | - |
| SVF: LFO 2 AMOUNT | Have | "SVF LFO2 Amt" | - | - |
| SVF: FM AMOUNT | Have | "SVF FM Amt" | - | - |
| Ladder: CUTOFF | Have | "Ladder Cutoff" | - | - |
| Ladder: RESONANCE | Have | "Ladder Res" | - | - |
| Ladder: EG AMOUNT | Have | "Ladder EG Amt" | - | - |
| Ladder: LFO 2 AMOUNT | Have | "Ladder LFO2 Amt" | - | - |
| Ladder: FM AMOUNT | Have | "Ladder FM Amt" | - | - |
| EG 1-3: DELAY, HOLD | Have | "... Env Delay", "... Env Hold" (per voice) | - | - |
| EG 1-3: ATTACK, DECAY, RELEASE | Have | Per voice: a voice with envelope modulation gets its own envelope settings; a stage is recalculated when its time moves by over 1% (2026-10-27, offline) | - | low-medium: about 0.9 us per voice per block with three envelopes swept by an LFO, none when unused |
| EG 1-3: SUSTAIN | Have | "... Env Sustain" (per voice) | - | - |
| EG 1-3: TIME SCALE | Have | "... Env Time Scale" (per voice) | - | - |
| EG 1-3: MULTI TRIGGER, SYNC, LOOP, LATCH (step-mod list, p91) | Missing | - | S | none |
| LFO 1-4: RATE | Have | "LFO n Rate" (+-100% = the whole knob; per voice) | - | - |
| LFO 1-4: RATE (SEMITONES) | Have | "LFO n Rate (st)" (+-100% = +-24 st) | - | - |
| LFO 1-4: VARIATION | Have | "LFO n Variation" | - | - |
| LFO 1-4: DELAY | Have | "LFO n Delay" | - | - |
| LFO 1-4: FADE IN TIME, FADE OUT TIME (RAMP UP/DOWN TIME in the step-mod list) | Have | "LFO n Fade In", "LFO n Fade Out" | - | - |
| LFO 1-4: WAVEFORM (step-mod list, p91) | Missing | - | S | none |

## 16. Arpeggiator (p78-81)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Arpeggiator per Synth, saved with the Timbre | p78 | Missing | (MPC has its own arpeggiator and note repeat) | M | none |
| ON | on/off (p78) | Missing | - | (in M) | none |
| RATE, free | 10 BPM to 280 BPM (p78) | Missing | - | (in M) | none |
| OCTAVE ORDER: each note through all octaves before the next note | on/off (p78) | Missing | - | S | none |
| PENDULUM: play the pattern back reversed | on/off (p79) | Missing | - | S | none |
| OCTAVE range | 1, 2, 3, 4 (p79) | Missing | - | S | none |
| DIRECTION of the octaves | Up, Down, Up/Down, Down/Up (p79) | Missing | - | S | none |
| PATTERN | Up, Down, Order, Random (p80) | Missing | - | S | none |
| SYNC to the Master Clock | on/off (p81) | Missing | - | S | none |
| CLOCK DIVISION | 4, 2, 1, 1/2, 1/4, 1/8, 1/16, 1/32 (p81) | Missing | - | S | none |
| CLOCK TIMING | Straight, Dotted, Triplet (p81) | Missing | - | S | none |
| GATE LENGTH | 1% to 100% (p81) | Missing | - | S | none |
| FOLLOW SWING | off/on (p81) | Missing | - | S | none |
| NOTE RESET: restart at once on a new note, or wait for the next division | off/on (p81) | Missing | - | S | none |
| PENDULUM MODE: end notes played once or twice | Once, Twice (p81) | Missing | - | S | none |
| RANDOM MODE | Non-repeating, Repeating (p81) | Missing | - | S | none |
| HOLD latches the arpeggiator | p63 | Missing | - | S | none |

## 17. Sequencer (p82-95)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Polyphonic step sequencer per Synth, up to 64 steps in pages of 16 | p82 | Missing | (MPC's own sequencer) | L | none |
| ON (whether the sequencer plays the Synth) | on/off (p82) | Missing | - | (in L) | none |
| RATE, free | 10 BPM to 280 BPM (p82) | Missing | - | (in L) | none |
| CURRENT PAGE / ACTIVE STEP LEDs [HW] | p82 | Missing | - | - | none |
| RETURN TO ZERO, START/STOP, RECORD, ADV back/forward [HW][UI] | p83 | Missing | - | M | none |
| REST per step | on/off (p84) | Missing | - | S | none |
| TIE per step (no envelope retrigger) | on/off (p84) | Missing | - | S | none |
| STEP MOD: record knob moves as per-step offsets while running | on/off (p84) | Missing | MPC automation is the nearest thing | M | none |
| TRANSPOSE from the keyboard (keys then do not sound) | on/off (p84) | Missing | - | S | none |
| NOTE TRIG: play while keys are held, stop on release | on/off (p85) | Missing | - | S | none |
| STEP EDIT screen with lanes (one note per lane per step) [UI] | p85-86 | Missing | - | L | none |
| Per-step NOTE | Off, C0 to G10 (p88) | Missing | - | (in L) | none |
| Per-step VELOCITY | 1 to 127 (p88) | Missing | - | (in L) | none |
| Per-step GATE | 1% to 100% (p88) | Missing | - | (in L) | none |
| Per-step RATCHET (repeats inside one step) | 1 to 4 (p88) | Missing | - | S | none |
| Modulation lanes: one destination each, bipolar % offset per step, CONTROLLER and AMOUNT per lane | p89-91 | Missing | - | M | low |
| Step-mod destination list (CATEGORY/DESTINATION) | p91 (see 15e) | Missing | - | (in M) | none |
| SYNC | off/on (p92) | Missing | - | S | none |
| CLOCK DIVISION (also divides the free rate) | 4, 2, 1, 1/2, 1/4, 1/8, 1/16, 1/32 (p92) | Missing | - | S | none |
| CLOCK TIMING | Straight, Dotted, Triplet (p92) | Missing | - | S | none |
| LENGTH | 1 to 64 (p93) | Missing | - | S | none |
| FOLLOW SWING | off/on (p93) | Missing | - | S | none |
| NOTE RESET (with Note Trig) | off/on (p93) | Missing | - | S | none |
| RESTART ON HOLD | off/on (p93) | Missing | - | S | none |
| RESTART ON TRIG | off/on (p93) | Missing | - | S | none |
| TRANSPOSE MODE | Start Key, Interval (from middle C) (p93) | Missing | - | S | none |
| ONE SHOT | off/on (p94) | Missing | - | S | none |
| RECORDING MODE | Replace, Overdub, Focused (p94) | Missing | - | M | none |
| OUTPUT: which Synth(s) the sequence plays [MULTI] | Synth 1/2/3, All Synths (p94) | Missing | - | S | none |
| CLEAR SEQUENCE, DUPLICATE X-Y [UI] | p94 | Missing | - | S | none |
| LOAD / SAVE sequences with names, notes, sort by name/date [UI] | p95 | Missing | - | M | none |

## 18. Effects (p96-111)

Rebuilt 2026-10-27 (offline; Tier 3): the manual's structure, a SYNTH EFFECT slot then MASTER EFFECTS on two sends.
Code: `src/fx.h` (routing), `src/fx/synth_fx.h`, `src/fx/master_delay.h`, `src/fx/reverb.h`. All original designs.

### 18a. Effects architecture

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Effects can all be switched off for a pure dry path | p96 | Have | `modfx_on` and `mfx_on` Off; sends at 0 or mix 0 are exactly dry (tested) | - | none |
| SYNTH EFFECT per Synth: one chosen type, mono in, stereo out, MIX always first | p96 | Have | `modfx_type` (15 types), `modfx_mix`; stereo in (the mono voice sum on both sides until voices pan), stereo out | - | none/voice; shared 1.5-7 us per block |
| Synth Effect ON button | on/off (p96) | Have | `modfx_on` | - | none |
| CTRL 1 and CTRL 2 knobs, re-assignable to any effect parameter | p96-97 | Have | `modfx_ctrl1`/`modfx_ctrl2`: +-100% of the target's range on top of its knob; target `modfx_c1_dst`/`modfx_c2_dst`, Auto = the manual's pre-assignment per type | - | none |
| Effect presets per type: load, save, initialise [UI] | p97-98 | Missing | - | M | none |
| Up to 5 effects at once (3 synth + 2 master) | p96 | Partial | 3 at once: one synth effect (one part) plus master delay and reverb | M | shared medium |
| MASTER EFFECTS: SEND 1 and SEND 2 per Synth into a master bus [MULTI] | p99 | Have | One part: `mfx_send1` into the delay, `mfx_send2` into the reverb; returns added to the dry signal | - | shared |
| Master Effects ON button | on/off (p99) | Have | `mfx_on` | - | none |
| MASTER BUSS MODE | Dual Mono (two effects), Stereo (one effect) (p100) | Partial | Fixed: effect 1 is the delay, effect 2 the reverb, each with stereo input | S | shared |
| STEREO WIDTH at the end of the master chain | 0% to 100% to 200% (p100) | Have | `mfx_width` 0-200%, mid/side on the returns (the dry path stays exact) | - | none/voice; shared low |
| EFFECT 1 -> 2 SERIES (parallel to series blend) | 0% to 100% (p100) | Have | `mfx_series`: the delay's return moves from the output into the reverb | - | none |
| Only one Eventide effect at a time [LIC] | p99 | Have | One reverb slot, original designs | - | - |
| SYNC per effect: time parameters become divisions | 1/64T..1/64., 1/32T.., 1/16T.., 1/8T.., 1/4T.., 1/2T.., 1, 2, 4 (21 values, p103) | Have | `modfx_sync`, `dly_sync`: the 21 divisions for delay times and LFO/vibrato rates (chorus/flanger times stay in ms) | - | none |
| Per-voice pan kept in the stereo master bus, lost in mono buses | p96, p99 | Partial | `fx_process()` takes stereo in; the voices are still a mono sum | (with section 10) | low |

### 18b. Effect types (Synth Effects and Master Effects share the list, p103-109)

ONE-ish's synth effect offers every type below except the vocoders; the master delay is the Stereo Delay and Ping
Pong (`dly_pp`). Shared knobs: `modfx_mix`, `modfx_fb` (and `_r`), `modfx_time` (and `_r`, chorus/flanger ms, range per
type), `modfx_dtime` (delay types, 0.1-2000 ms), `modfx_rate` (and `_r`, 0.01-20 Hz), `modfx_depth`, `modfx_tone`,
`modfx_spread`, `modfx_gain` (silent to +6 dB, 50% unity), `modfx_noise`, `modfx_sync`.

**Delay** (single tap, cubic interpolation, p103)

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX (CTRL 1) | 0% to 100% | Have | Synth type Delay: `modfx_mix`; master: `mfx_send1` | - | none/voice |
| FEEDBACK | 0% to 100% | Have | `modfx_fb`; master `dly_fb` | - | none/voice |
| TIME (CTRL 2) | 0.1 ms to 2000 ms | Have | `modfx_dtime`, `dly_time` 0.1 ms-2 s, Hermite (cubic) read, sample-exact | - | none/voice |
| GAIN | 0% to 100% | Have | `modfx_gain` | - | none/voice |
| SYNC | on/off | Have | `modfx_sync`, `dly_sync` | - | none/voice |

**Stereo Delay** (two taps, p103): ONE-ish's master delay

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX (CTRL 1), FEEDBACK (CTRL 2) | 0% to 100% | Have | `mfx_send1`, `dly_fb` | - | none/voice |
| TIME LEFT, TIME RIGHT | 0.1 ms to 2000 ms each | Have | `dly_time`, `dly_time_r` | - | none/voice |
| FEEDBACK LEFT, FEEDBACK RIGHT | 0% to 100% | Missing | One feedback for both | S | none/voice |
| MOD RATE LEFT/RIGHT, MOD DEPTH LEFT/RIGHT | 0% to 100% | Missing | - | S | none/voice; shared low |
| TONE (lowpass) | 0% to 100% | Have | `dly_tone` | - | none/voice |
| GAIN | 0% to 100% | Partial | The send level | S | none/voice |
| SYNC | on/off | Have | `dly_sync` (both times) | - | none/voice |

**Ping Pong** (alternating stereo taps, p104): the master delay with `dly_pp` On

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX (CTRL 1), FEEDBACK (CTRL 2) | 0% to 100% | Have | `mfx_send1`, `dly_fb` | - | none/voice |
| TIME LEFT, TIME RIGHT | 0.1 ms to 2000 ms | Have | `dly_time`, `dly_time_r` | - | none/voice |
| OFFSET between left and right | 0% to 100% | Have | `dly_offset`: the right side's repeats later by up to half the left time | - | none/voice |
| TONE | 0% to 100% | Have | `dly_tone` | - | none/voice |
| GAIN | 0% to 100% | Partial | The send level | S | none/voice |
| SYNC | on/off | Have | `dly_sync` | - | none/voice |

**Other delay types**

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| BBD DELAY (bucket-brigade emulation, p104): MIX (CTRL 1), FEEDBACK (CTRL 2), TIME, NOISE (clock noise), TONE, MOD (random wobble), GAIN, SYNC | 0-100%; time 0.1-2000 ms; sync on/off | Have | Synth type BBD Delay: bandwidth falling with the time, soft-clipped loop, hiss (`modfx_noise`), random clock wander (`modfx_depth`) | - | none/voice; shared low (4.8 us) |
| TAPE DELAY (tape emulation, p104): MIX (CTRL 1), FEEDBACK (CTRL 2), TIME, FLUTTER (rate), WOW (depth and slow rate), TAPE BIAS (highpass and bias mix), GAIN, SYNC | 0-100%; time 0.1-2000 ms | Have | Synth type Tape Delay: `modfx_flutter`, `modfx_wow`, `modfx_bias` (loop low cut and record saturation), head loss on `modfx_tone` | - | none/voice; shared low (5.1 us) |
| ECHO (delay with spring-style diffusion, p105): MIX (CTRL 1), TONE (CTRL 2), TIME, FEEDBACK, ROOM, DECAY, MOD RATE, MOD DEPTH, GAIN, SYNC | 0-100%; time 0.1-2000 ms; mod rate 0.1-10 Hz | Have | Synth type Echo: three diffusers and a cross-fed tank (`modfx_room`, `modfx_decay`), modulated read (`modfx_rate`, `modfx_depth`) | - | none/voice; shared low (4.9 us) |

**Flanger** (single tap, p105)

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX | 0% to 100% | Have | `modfx_mix` (type Flanger) | - | none/voice |
| FEEDBACK | -100% to +100% | Have | `modfx_fb` | - | none/voice |
| TIME (CTRL 1) | 0.1 ms to 15 ms | Have | `modfx_time`: the longest delay; the sweep goes down from it | - | none/voice |
| RATE (CTRL 2) | 0.1 Hz to 10 Hz | Have | `modfx_rate` 0.01-20 Hz | - | none/voice |
| DEPTH | 0% to 100% | Have | `modfx_depth` | - | none/voice |
| TONE (lowpass) | 0% to 100% | Have | `modfx_tone` | - | none/voice |
| NOISE | 0% to 100% | Have | `modfx_noise` | - | none/voice |
| SPREAD (stereo) | 0% to 100% | Have | `modfx_spread`: right LFO up to half a cycle on | - | none/voice |
| GAIN | 0% to 100% | Have | `modfx_gain` | - | none/voice |
| SYNC | on/off | Have | `modfx_sync` | - | none/voice |

**Stereo Flanger** (one line per channel, p105)

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX, FEEDBACK LEFT, FEEDBACK RIGHT, TIME LEFT (CTRL 1), TIME RIGHT (CTRL 2), RATE LEFT, RATE RIGHT, DEPTH, PHASE OFFSET, TONE, GAIN, SYNC | mix 0-100%; feedback -100 to +100%; time 0.1-25 ms; rate 0.01-20 Hz; phase 0-100% | Have | Synth type Stereo Flanger: `_r` knobs for the right side, phase offset `modfx_spread` | - | none/voice; shared low (4.1 us) |

**Chorus** (single tap, p106)

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX | 0% to 100% | Have | `modfx_mix` (type Chorus) | - | none/voice |
| FEEDBACK | -100% to +100% | Have | `modfx_fb` | - | none/voice |
| TIME | 0.1 ms to 50 ms | Have | `modfx_time`; the sweep swings +-half of it times the depth | - | none/voice |
| RATE (CTRL 1) | 0.1 Hz to 20 Hz | Have | `modfx_rate` 0.01-20 Hz | - | none/voice |
| DEPTH (CTRL 2) | 0% to 100% | Have | `modfx_depth` | - | none/voice |
| TONE | 0% to 100% | Have | `modfx_tone` | - | none/voice |
| SPREAD | 0% to 100% | Have | `modfx_spread` | - | none/voice |
| GAIN | 0% to 100% | Have | `modfx_gain` | - | none/voice |
| SYNC | on/off | Have | `modfx_sync` | - | none/voice |

**Other chorus types**

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| DUAL CHORUS (two taps, bi-phase, p106): MIX (CTRL 1), FEEDBACK (CTRL 2), TIME offset, DEPTH, RATE, TONE, VIBRATO RATE, VIBRATO DEPTH, GAIN, SYNC | mix 0-100%; feedback -100 to +100%; time 3-60 ms; rate Low/Med/High; vibrato rate 0.1-10 Hz | Have | Synth type Dual Chorus: left and right taps swing in opposite directions; `modfx_vrate`, `modfx_vdepth`; the rate is a continuous knob, not Low/Med/High | - | none/voice; shared low (4.0 us) |
| ENSEMBLE (three taps 120 degrees apart, p106): MIX (CTRL 1), FEEDBACK (CTRL 2), TIME, RATE, DEPTH, TONE, VIBRATO RATE, VIBRATO DEPTH, SPREAD, GAIN, SYNC | feedback 0-100%; time 3-60 ms; rate Low/Med/High; vibrato rate 0.1-10 Hz | Have | Synth type Ensemble: three sine-swept taps per side, vibrato, spread | - | none/voice; shared low-medium (7.2 us) |
| STEREO CHORUS (one line per channel, p107): MIX (CTRL 1), FEEDBACK LEFT (CTRL 2), FEEDBACK RIGHT, TIME LEFT, TIME RIGHT, RATE LEFT, RATE RIGHT, DEPTH, TONE, GAIN, SYNC | feedback -100 to +100%; time 1-50 ms; rate 0.01-20 Hz | Have | Synth type Stereo Chorus: `_r` knobs for the right side | - | none/voice; shared low (4.2 us) |

**6 Stage Phaser** (six all-pass stages, modelled on a Moog pedal phaser per the manual, p107)

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX (CTRL 1) | 0% to 100% | Have | `modfx_mix` (type 6-Stage Phaser) | - | none/voice |
| FEEDBACK (CTRL 2) | 0% to 100% | Have | `modfx_fb` (-100 to +100%) | - | none/voice |
| SWEEP (zero point) | 0% to 100% | Have | `modfx_sweep`: the lowest point 40 Hz-4 kHz; depth sweeps up to 5 octaves above | - | none/voice |
| RATE | 0.01 Hz to 20 Hz | Have | `modfx_rate` | - | none/voice |
| DEPTH | 0% to 100% | Have | `modfx_depth` | - | none/voice |
| TONE (lowpass on the mix) | 0% to 100% | Partial | `modfx_tone` on the wet signal, not on the mix | S | none/voice |
| NOISE | 0% to 100% | Have | `modfx_noise` | - | none/voice |
| GAIN | 0% to 100% | Have | `modfx_gain` | - | none/voice |
| SYNC | on/off | Have | `modfx_sync` | - | none/voice |

**Other modulation and character types**

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| 12 STAGE PHASER (p107): same parameters as the 6-stage | as 6-stage | Have | Synth type 12-Stage Phaser | - | none/voice; shared low (4.6 us) |
| TIME PHASER (four-stage phaser into a delay, p108): MIX (CTRL 1), PHASER FEEDBACK (CTRL 2), SWEEP, RATE, DEPTH, TONE, NOISE, DELAY FEEDBACK, TIME, GAIN, SYNC | 0-100%; rate 0.01-20 Hz; time 0.1-2000 ms | Have | Synth type Time Phaser: `modfx_dtime`, `modfx_dfb` for the delay | - | none/voice; shared low (4.4 us) |
| BIT CRUSHER (crossover filter, dual crushing, p108): MIX (CTRL 1), DEPTH (CTRL 2), RATE, GAIN | depth 2-24 bit; rate 128 Hz-48 kHz | Partial | Synth type Bit Crusher: `modfx_bits` 2-24, `modfx_srate` 128 Hz-44.1 kHz; one band (no crossover) | S | none/voice; shared low (1.5 us) |
| RESONATOR (three-filter bank, p108): MIX (CTRL 1), RESONANCE (CTRL 2), CUTOFF LOW/MID/HIGH, LEVEL LOW/MID/HIGH, FILTER TYPE, MOD RATE, MOD DEPTH, GAIN | cutoffs 10 Hz-22 kHz (defaults 500 Hz, 1 kHz, 5 kHz); type LP/BP/HP; mod rate 0.01-10 Hz | Have | Synth type Resonator: three state-variable filters, `modfx_cut1-3`, `modfx_lvl1-3`, `modfx_ftype`, `modfx_reso`, `modfx_rate`/`modfx_depth` sweep | - | none/voice; shared low (2.4 us) |
| 10 BAND VOCODER (p109): MIX (CTRL 1), SIBILANCE (CTRL 2), SENSITIVITY (mic gate), TONE, NOISE [HW: needs a mic input] | 0-100% | Missing | No audio input on an MPC instrument plugin | L | none/voice; shared medium |
| 16 BAND VOCODER (p109): same parameters [HW: needs a mic input] | 0-100% | Missing | - | L | none/voice; shared medium-high |

### 18c. Eventide Master Effects (master bus only, one at a time) [LIC]

These are licensed algorithms. Only original reverbs in the same spirit can be built; none can match them. ONE-ish's
master reverb (`rev_type`: Plate, Room, Hall) shares one set of knobs; decay is a time (`rev_decay`, 0.25-10 s at
size 100%, scaled by size).

**Eventide Plate** (p110) against ONE-ish's plate (Dattorro figure-of-eight tank)

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| (the reverb itself) [LIC] | - | Have | Dattorro plate, `rev_type` Plate on `mfx_send2` | - | none/voice; shared medium (8.4 us) |
| DISTANCE (early-reflection delay) | 0 to 100 | Missing | - | S | none/voice |
| DIFFUSION (early network feedback) | 0 to 100 | Have | `rev_diff` scales the input diffusers (100% = the paper's values) | - | none/voice |
| MOD LEVEL | 0 to 100 | Have | `rev_mod` (50% = the paper's excursion) | - | none/voice |
| TONE (input lowpass) | -100 to 100 | Have | `rev_tone` 0-100% (open at 100) | - | none/voice |
| HIGH DAMP | 100 to 0 | Have | `rev_damp` | - | none/voice |
| LOW DAMP | 100 to 0 | Have | `rev_lowdamp`: a low cut (below about 250 Hz) in the loop | - | none/voice |
| PRE DELAY | 0 ms to 1500 ms | Have | `rev_pre` 0-1500 ms | - | none/voice |
| SIZE | 0 to 100 | Have | `rev_size` 50-200%: tank lengths and the decay time | - | none/voice |
| DECAY | 0.25 s to 10 s, Infinite, Freeze | Partial | `rev_decay` 0.25-10 s, `rev_freeze` (holds, input shut); no Infinite | S | none/voice |

**Other Eventide reverbs**

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| EVENTIDE ROOM [LIC] (p109): early/late balance (unnamed first row, 20 detents from all-early to all-late), DIFFUSION, MOD LEVEL, HIGH FREQ (post-crossover lowpass), HIGH LEVEL, LOW LEVEL, PRE DELAY, SIZE, DECAY | 0-100; high freq 1-5 kHz; high level -100 to 100; low level 0-1; pre-delay 0-500 ms | Partial | `rev_type` Room (original): early reflections and a 4-line feedback delay network; `rev_early` balance, diffusion, size, decay, pre-delay; damping instead of the band levels; no modulation | S | none/voice; shared medium (6.2 us) |
| EVENTIDE HALL [LIC] (p110): LOW DECAY, HIGH DECAY, MOD LEVEL, MID LEVEL, HIGH LEVEL, LOW LEVEL, PRE DELAY, SIZE, DECAY | low/high decay 0.1-5.0x, Infinite, Freeze; levels -100 to 100; pre-delay 0-1500 ms; decay 0-20 s, Infinite, Freeze | Partial | `rev_type` Hall (original): early reflections, four diffusers, an 8-line network with four modulated lines; high/low damping instead of band decays and levels | M | none/voice; shared medium-high (17.5 us) |
| EVENTIDE SHIMMER [LIC] (p111): A PITCH, B PITCH, PITCH DECAY (with Pitch Freeze, Pitch+Reverb Freeze), MID DECAY, LOW DECAY, HIGH DECAY, DELAY, SIZE, DECAY | pitches about +-24 semitones in cents (printed "-2410C - +2410"); 0-100; delay 50-1000 ms | Missing | Deferred: pitch shifters in the loop (CPU) | L (pitch shifters in the feedback loop) | none/voice; shared high |
| EVENTIDE BLACKHOLE [LIC] (p111): MOD DEPTH, MOD RATE, FEEDBACK, RESONANCE, HIGH LEVEL, LOW LEVEL, PRE DELAY, SIZE, GRAVITY | 0-100; feedback 0-100, Infinite, Freeze; levels -100 to 100; pre-delay 0-2000 ms; gravity -100 to 100 | Missing | Deferred (CPU) | L (very large modulated tank) | none/voice; shared high |

## 19. Left Hand Controller (p17-18, p112-117)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| OCTAVE SHIFT | -2 to +2 (p112) | Have | `octave_shift` (an engine param; follows live, keyboard tracking too) (2026-10-27, offline) | - | none |
| MIDI panic (hold both octave buttons) | p112 | Have | CC 120/123/121 handled | - | - |
| GLIDE on/off | p112 | Have | `glide`; each voice glides from its own last pitch (a fresh voice doesn't), at the control rate; keyboard tracking and the Keyboard source follow it (2026-10-27, offline) | - | none |
| GLIDE RATE | 0 ms to 10.0 s (p112) | Have | `glide_time` 0 ms, then 1 ms to 10 s on a log knob (2026-10-27, offline) | - | none |
| GLIDE TYPE | LCR (constant rate), LCT (constant time), EXP (exponential) (p116) | Have | `glide_type`: LCR takes the glide time per octave, LCT the glide time for any interval, EXP 95% of the way in the glide time (2026-10-27, offline) | - | none |
| GATED GLIDE: glide pauses while no key is down | off/on (p116) | Have | `glide_gated`: a voice's glide holds while its key is up (the pedal counts as down) (2026-10-27, offline) | - | none |
| LEGATO GLIDE: glide only on legato notes (mono only) | off/on (p116) | Have | `glide_legato`; in poly too (a note pressed while another key is held) (2026-10-27, offline) | - | none |
| GLISSANDO: glide in semitone steps | off/on (p117) | Have | `glissando` (2026-10-27, offline) | - | none |
| PITCH wheel, spring-loaded | p113 | Have | MIDI pitch bend | - | - |
| PITCH BEND UP (SHIFT = finer than semitones) | -24 to +24 st (p115) | Have | `bend_up` -24..+24 st, fine values allowed; the old `bend_range` is retired (an older project's value sets both directions) (2026-10-27, offline) | - | none |
| PITCH BEND DOWN | -24 to +24 st (p115) | Have | `bend_down` -24..+24 st (default -2: down a whole tone) (2026-10-27, offline) | - | none |
| MOD wheel, not spring-loaded | p113 | Have | CC 1 | - | - |
| X/Y PAD: X, Y, pressure, gate; X/Y HOLD saves the held value [HW] | p113-114 | Missing | Could map MIDI CCs (MPC has XYFX pads) | S | none |
| PAD POLARITY [HW] | Unipolar, Bipolar (p115) | Missing | - | S | none |
| VELOCITY CURVE, per Synth | Soft, Medium, Hard, Compress, Expand (p116) | Have | `vel_curve`: Soft sqrt(v), Medium v (default, as before), Hard v^2, Compress and Expand flatten or steepen around the middle; all keep both ends (2026-10-27, offline) | - | none |
| LOAD SAVED POS. (mod wheel, pad position with the Synth) | Off, Mod Wheel, Pad, Both (p117) | Missing | Wheel always starts at 0 | S | none |
| SEMITONE TRANSP. | -12 to Off to +12 (p117) | Have | `transpose` -12..+12 st (2026-10-27, offline) | - | none |
| KEY TRACK CENTER for all key tracking | C0 to G10, default middle C (p117) | Have | `key_center` MIDI note 0-127, shown as "C4 (60)": both filters' tracking, the Keyboard matrix source and oscillator pitch tracking (2026-10-27, offline) | - | none |
| HEADPHONES jacks [HW] | p114 | Missing | - | - | none |

## 20. Browser, presets and timbres (p118-121)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Preset store (tens of thousands), browse and load | p118-119, p140 | Partial | 13 factory programs; MPC saves plugin state with programs and projects | - | none |
| Tags: TYPE (Single, Layer, Split, Multi), CATEGORY, MOOD, GROUP; filter the browser by tags [UI] | p118-120 | Missing | - | - | none |
| Notes saved with a preset [UI] | p118, p120 | Missing | - | - | none |
| Load a Timbre from any preset into any Synth [MULTI] | p119 | Missing | - | M | none |
| SAVE PRESET / SAVE PRESET AS, unique names, overwrite prompt [UI] | p120 | Partial | Host saves the state | - | none |
| SAVE TIMBRE to a new single-Timbre preset [UI][MULTI] | p121 | Missing | - | - | none |
| A preset restores everything (sequences, matrices, arps, effects, held wheel and pad) | p119 | Partial | The state string restores every ONE-ish parameter | - | none |

## 21. Editing and saving (p122-123)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Edited indicator (SAVE lit dimly) [UI] | p122 | Missing | MPC marks unsaved projects | - | none |
| COMPARE edited vs saved (A/B) | p122 | Missing | - | S-M (engine-side compare buffer) | none |
| SNAPSHOT: timestamped micro-saves with notes, browse and load [UI] | p122-123 | Missing | Host undo | - | none |
| AUTOSAVE snapshot on preset change [UI] | p123 | Missing | - | - | none |

## 22. Settings (p124-139)

### 22a. Global hardware settings (p125-127)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| KNOB MODE [HW] | Absolute, Pass Through, Relative (p125) | Missing | MPC knob behaviour | - | none |
| CHORD MEMORY learn mode | Silent Learn, Audible Learn (p125) | Missing | Needs chord memory | S | none |
| LCD BRIGHTNESS, PANEL LED BRIGHTNESS [HW] | 0% to 100% (p125) | Missing | - | - | none |
| SHOW BANK PREVIEW, PRESET AUTOLOAD [UI] | off/on (p125) | Missing | - | - | none |
| DUPLICATE NAMES ON IMPORT / EXPORT [UI] | Ignore, Overwrite, Auto Rename, Manual Rename (p126) | Missing | - | - | none |
| STEP EDIT AUTO-SCROLL [UI] | off/on (p126) | Missing | - | - | none |
| NOISE REDUCTION between notes [ANALOG][HW] | -24 dB to 0 dB, default -12 dB (p126) | Missing | Not needed: no analog hiss (ONE-ish adds only a -100 dB floor) | - | none |
| SET DATE/TIME, formats [HW] | p127 | Missing | - | - | none |
| FAN MODE [HW] | Normal, Low (p127) | Missing | - | - | none |

### 22b. MIDI (p128-130)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| RECEIVE MIDI CLOCK | off/on (p128) | Partial | Host tempo via `lfo_bpm` | - | none |
| RECEIVE MIDI START/STOP | off/on (p128) | Missing | Host transport not read | S | none |
| MIDI I/O LOAD FROM PRESET [UI] | off/on (p129) | Missing | - | - | none |
| MIDI IN PORTS [HW] | DIN, USB, DIN & USB (p129) | Missing | Host | - | none |
| MIDI IN ROUTE and channels [MULTI] | All Synths (Off, 1-16) or Per Synth (Off, 1-16 each) (p129) | Partial | Omni: the channel nibble is ignored | S (with parts) | none |
| MIDI OUT PORTS, ROUTE, channels [HW] | DIN/USB; All or Per Synth (p129-130) | Missing | The plugin sends no MIDI | - | none |

### 22c. CV inputs and outputs, pedals (p131-132)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| CV IN 1/2, EXP IN 1/2: FUNCTION, SYNTHS, VOLTAGE MIN/MAX [HW] | None, Next/Previous Preset, Next/Previous Set; -5 V to +5 V (p131-132) | Missing | - | - | none |
| SUSTAIN IN FUNCTION | Off, Sustain Pedal, Sostenuto Pedal (p131) | Have | Both at once, as MIDI sends them: sustain CC 64, sostenuto CC 66 (holds the notes sounding as it goes down) (2026-10-27, offline) | - | none |
| SUSTAIN IN SYNTH target [MULTI] | Synth 1/2/3, pairs, All (p131) | Missing | - | S (with parts) | none |
| SUSTAIN SWITCH TYPE [HW] | Open, Closed (p131) | Missing | Host | - | none |
| CV OUT 1-4: SOURCE, OUT VOLTAGE MIN/MAX [HW] | Gate, Pitch Wheel, Mod Wheel, Aftertouch, Pad X/Y/Pressure/Gate; -5 V to +5 V (p132) | Missing | - | - | none |

### 22d. Utilities, library, tuning (p133-139)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| LIGHT SHOW, RESTORE DEFAULT SETTINGS, UPDATE FIRMWARE, version/build info, IP ADDRESS [HW] | p133-134 | Missing | - | - | none |
| USER SPACES: switch, create (factory or empty), rename, copy [UI] | p134-135 | Missing | - | - | none |
| EXPORT / IMPORT to USB: presets, sequences, sets, effect presets, matrix presets, globals, all [UI] | p136-137 | Missing | Host/project files | - | none |
| Edit CATEGORY, MOOD, GROUP tag lists [UI] | p137-138 | Missing | - | - | none |
| MASTER FREQUENCY (A above middle C) | 400 Hz to 480 Hz, default 440 (p139) | Have | `master_tune` (2026-10-27, offline) | - | none |
| MASTER FINE TUNE | -100 to +100 cents (p139) | Have | `fine_tune` (2026-10-27, offline) | - | none |

## 23. Specifications cross-check (p140-141)

Cross-reference only: these rows repeat items already counted above and are left out of the totals.

| Spec line | Where audited | Note |
|---|---|---|
| Polyphonic, tri-timbral analog; 8 or 16 voices | 0 | ONE-ish: one part, up to 8 voices |
| Keybed 61 keys with velocity and aftertouch (Fatar TP-8S) [HW] | 1 | - |
| Controllers: pitch and mod wheels, X/Y pad with pressure and hold, sustain, EXP 1, EXP 2 | 19, 22c | - |
| 73 knobs, 144 buttons, LCD [HW] | 1 | - |
| Oscillators x3: saw with variable reset or triangle with variable rise/fall, variable pulse, octave, frequency +-7 st, beat +-7 Hz, wave angle, PW, mix, FM, hard sync | 3, 4 | Beat range confirmed in Hz here |
| Ring modulator 1-2 or 2-3 | 4 | - |
| Dual-source noise with colour pairs, colour mix and a dedicated ARS envelope | 5 | - |
| Mixer: OSC 1-3, ring mod, noise, ext in | 6 | - |
| SVF: two 12 dB filters acting as one, notch/BP/LP/HP, 12 or 24 dB | 7 | 24 dB is only stated here |
| Ladder: HP or LP, 6/12/18/24 dB | 7 | - |
| Envelopes x3: DAHDSR, curves per stage, multi-trigger, loop, latch, sync | 8 | - |
| LFOs x4: dual-wave categories with variable shapes, fades, repeat count, start phase | 9 | - |
| Glide: LCR, LCT, EXP, gated, legato, glissando | 19 | - |
| Arpeggiator per Synth; 64-step sequencer per Synth with step mod | 16, 17 | - |
| Synth effects per Synth; master effects dual mono or stereo; Eventide on master only | 18 | - |
| Voice allocation: mono or poly per Synth, unison/mono detune (48 oscillators max), chord memory, stealing | 14 | 48 = 16 voices x 3 oscillators |
| Snapshot, compare; VCA level and pan per Synth; outputs, inserts, ext inputs, CV, pedals, MIDI, USB, LAN | 2, 10, 11, 21, 22 | - |

## 24. Appendices A and B (p143-158)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| USB computer keyboard for naming and Panel Focus shortcuts [HW][UI] | p143 | Missing | - | - | none |
| MIDI CONTROL OUTPUT: panel moves sent as CC, MIDI MAP or NRPN; ALWAYS SEND NRPN NUM [HW] | p144 | Missing | The plugin sends no MIDI | - | none |
| LEARN MIDI CC: map a CC to any panel control, 7- or 14-bit | p145-146 | Partial | Every parameter is a host-automatable VST parameter; MPC does the learning | - | none |
| EDIT MIDI MAPPING list, reserved CCs [UI] | p147-148 | Partial | Host side | - | none |
| NRPN input for every parameter per Synth (table p149-158: LFO wave and free/synced rate; oscillator octave, frequency, beat, saw, triangle, PW, mix, wave, sync; pitch mod; ring mod source; wave mod; FM; noise; polyphony; mixer; filters; cutoff mod; three envelopes with free and synced times, multi trig, sync, loop, latch; VCA; output) | p149-158 | Missing | Only CC 1, 64, 120, 121, 123 are read | M | none |
| 14-bit parameter resolution over MIDI | p145 | Missing | Host parameter resolution | S | none |

---

## Summary counts per module

Counted from the tables above (section 23 excluded). "Of which HW/UI" counts Missing rows tagged [HW] or [UI],
which are not build candidates for an MPC plugin.

| Module | Have | Partial | Missing | of which HW/UI |
|---|---|---|---|---|
| 0. Architecture and overview | 1 | 6 | 3 | 0 |
| 1. Front panel, centre console, keyboard | 2 | 4 | 9 | 7 |
| 2. Rear panel | 0 | 3 | 3 | 3 |
| 3. Oscillators | 12 | 2 | 6 | 1 |
| 4. Oscillator common (pitch mod, ring mod, wave mod, FM) | 6 | 0 | 2 | 0 |
| 5. Noise | 1 | 1 | 7 | 0 |
| 6. Mixer | 4 | 1 | 4 | 4 |
| 7. Filters | 22 | 1 | 1 | 1 |
| 8. Envelopes | 15 | 3 | 3 | 2 |
| 9. LFOs | 2 | 9 | 11 | 1 |
| 10. VCA | 1 | 0 | 4 | 1 |
| 11. Output | 0 | 0 | 6 | 5 |
| 12. Performance Sets | 0 | 2 | 3 | 3 |
| 13. Master Clock | 0 | 3 | 4 | 2 |
| 14. Voice allocation (Hold, Chord, Polyphony) | 4 | 4 | 4 | 1 |
| 15. Modulation (structure, sources, controllers, transforms, destinations) | 27 | 10 | 51 | 10 |
| 16. Arpeggiator | 0 | 0 | 17 | 0 |
| 17. Sequencer | 0 | 0 | 31 | 5 |
| 18. Effects (architecture, every type, Eventide) | 69 | 10 | 8 | 1 |
| 19. Left Hand Controller (glide, bend, pad, curves) | 9 | 2 | 8 | 3 |
| 20. Browser, presets and timbres | 0 | 3 | 4 | 3 |
| 21. Editing and saving | 0 | 0 | 4 | 3 |
| 22. Settings (global, MIDI, CV, utilities, library, tuning) | 0 | 3 | 23 | 18 |
| 24. Appendices A and B (USB keyboard, MIDI learn, NRPN) | 0 | 2 | 4 | 2 |
| **Total** | **175** | **69** | **220** | **76** |

Rows are features or parameters, so the totals weigh a 30-row sequencer the same as 30 oscillator parameters; read
them per module, not as one score. Leaving out [HW]/[UI] rows, 144 sound or behaviour items are missing.

## Prioritised build order

Ordered by how much each group matters to how a Moog One sounds and plays, cheapest wins first inside each tier.
Effort totals are rough. CPU is per voice unless marked shared.

### Tier 1: the Moog One sound (do these first)

1. **Oscillator wave shaping** (section 3-4): two generators per oscillator (tri/saw plus pulse), variable pulse width
   and PWM, triangle wave angle (saw to triangle to ramp), saw reset-time angle, the MIX blend, and the LFO 3 to angle
   and width hard-wires (once LFO 3 exists). This is the single biggest tonal difference: the Moog One's oscillators
   are its signature as much as the ladder. Effort: M-L (about 3-5 days). CPU: low-medium, roughly doubles oscillator
   cost (oscillators are a small part of a voice next to the two filters).
2. **Glide** (section 19): on/off, rate 0-10 s, LCR/LCT/EXP, legato glide, gated glide, glissando. Essential for
   leads and basses, nearly free. Effort: S-M (1 day). CPU: none.
3. **Envelope extras** (section 8): delay and hold stages, per-stage curves, loop, latch, tempo sync, time scale,
   velocity amount, multi-trig and reset switches, the MIN time. Hold alone gives the classic "envelope clipping" bite
   the manual recommends. Effort: M (2-3 days). CPU: none-low.
4. **Linear FM and ring modulator** (section 4, 6): FM 1 to 2, 1 to 3, 3 to 1 and a ring-mod mixer channel (1x2, 2x3)
   with its own filter routing. Effort: M (2 days). CPU: low for ring mod; medium for FM at 44.1 kHz. Oversampling
   both to stop aliasing would be high and is not advised on the MPC.
5. **The SVF pair** (section 7): a second SVF element with spacing and series/parallel (24 dB modes, dual peaks),
   link cutoff, key tracking to +-200%. Effort: M (2 days). CPU: low (about 10 multiplies a sample a voice).
6. **Unison** (section 14): unison on/off, detune, unison count, plus pitch variance. The Moog One's huge stacked
   leads depend on it. Effort: M (2 days). CPU: high by nature (each stacked voice is a whole voice); on the MPC keep
   the stack inside the voice budget (for example 2-4 stacked out of 8).

Tier 1 total: roughly 12-16 days.

### Tier 2: modulation depth and playability

7. **LFOs to four, per voice** (section 9): LFOs 3 and 4, variation morph, smooth noise, ranges (Slow to Medium),
   polarity, start phase, note-on reset, delay, fade in/out, repeats, smoothing, dotted/triplet sync with the full
   division list. Effort: M (2-3 days). CPU: low (control rate; per-voice LFOs cost a few operations each 16 samples).
8. **Matrix depth** (section 15): 16 slots, +-300% source amounts, controller amount and inversion, the four
   transforms, the missing sources (pitch wheel, noise EG, LFO 3/4, KB gate, sustain, expression CCs, unipolar random,
   constant) and destinations (wave shape, beat, ring mod level, pan, noise colour, the hard-wired amounts, EG stage
   times and time scale, LFO rate/variation/delay/fades). Effort: M (2-3 days). CPU: low; per-voice envelope time
   modulation is low-medium (coefficient updates per voice).
9. **Noise colours and noise envelope** (section 5): red/purple, colour pairs and mix, the AR(S) noise EG as a source.
   Effort: S (half a day). CPU: low.
10. **Voice and keyboard options** (sections 3, 14, 19, 22): note priority low/high/last, stealing modes, poly counts
   3/5/7, Hold, separate bend up/down to +-24 and per-oscillator bend offsets, velocity curves, semitone transpose,
   key-track centre, beat frequency, pitch tracking 0-200%, note-on reset switch, sync source select, master tune
   400-480 Hz and fine tune, sostenuto, expression pedal CCs. Effort: S each, about 2-3 days together. CPU: none.
11. **Stereo voices** (section 10): synth pan, voice spread, pan as a matrix destination. Effort: M (1-2 days). CPU:
   low (a stereo voice bus; the effects still take a mono sum, as on the Moog One).
12. **Analog character** (sections 3, 7): slow oscillator drift; later, per-stage ladder saturation. Effort: S for
   drift, L for the ladder. CPU: low for drift; medium-high for a non-linear ladder (only if the bench allows).

Tier 2 total: roughly 10-14 days (plus L for the ladder model).

### Tier 3: effects

13. **Synth effect type slot** (section 18a-b), done 2026-10-27 offline (all types but the vocoders; master delay
   and reverb on two sends): one selectable effect per part (later per part plus master sends),
   adding the missing parameters to the existing types (chorus time and feedback, flanger time, phaser sweep, tone,
   gain, spread, sync for all time parameters, separate L/R delay times and ping-pong offset), then new types: dual
   chorus, ensemble, stereo chorus/flanger, 12-stage and time phaser, BBD, tape, echo, bit crusher, resonator.
   Effort: M-L (4-6 days). CPU: none per voice; each effect is shared and low to low-medium.
14. **Master reverbs** (section 18c), done 2026-10-27 offline (plate, room, hall; freeze): original room and hall designs, plate size/diffusion/tone/low damp/longer
   pre-delay and freeze. Effort: M-L (3-5 days). CPU: shared medium to medium-high.
15. **Shimmer and blackhole-style reverbs** (still deferred): Effort: L. CPU: shared high. Defer; likely too heavy next to 8 voices.

### Tier 4: performance tools and structure (lowest priority on an MPC)

16. **Arpeggiator** (section 16): full feature set is M (2-3 days), CPU none. The MPC already has an arpeggiator and
   note repeat, so this is mainly for preset portability.
17. **Chord memory** (section 14): M (1-2 days); voice cost scales with chord size.
18. **Multitimbral parts** (sections 0, 10-14, 22b): up to 3 parts with key zones, per-part MIDI channel, shared voice
   pool, per-part effects and master sends. Effort: L (1-2 weeks). CPU: high (all parts share one voice budget, and
   each part effect adds shared cost). On the MPC, three ONE-ish instances on three tracks give most of this for free.
19. **Sequencer with modulation lanes** (section 17): L (1-2 weeks). CPU none. Recommend skipping: the MPC sequencer
   and automation cover notes and knob motion. A cut-down "step mod" lane (per-step offsets on one destination, synced)
   would be the only part worth considering (M).
20. **Compare buffer, matrix/effect preset libraries, tags, NRPN** (sections 15a, 18a, 20-24): mostly UI or host work;
   build only if the MPC page can show them.

Not build candidates: everything tagged [HW] (jacks, CV, inserts, panel, displays, storage, fans, firmware) and the
vocoders (they need a microphone input that an MPC instrument plugin does not get).

### What is likely too CPU-heavy for the MPC

Budget: about 2.9 ms per 128-sample block on Gen1, shared with the whole project; ONE-ish's per-voice cost is not yet
measured on the device (`tools/bench.sh`), so every "high" item below must be benchmarked before it ships.

- **16-voice polyphony** and **large unison stacks**: each voice is a full voice; stay at the benched voice count and
  let unison spend it.
- **Audio-rate filter FM** (cutoff FM from an oscillator): needs per-sample filter coefficients for two filters per
  voice; high. If attempted, use a tan approximation and only on one filter.
- **Oversampling** (for clean FM, sync and a driven ladder): 2x roughly doubles the voice cost; high.
- **Per-stage non-linear ladder**: medium-high; only if the bench leaves room.
- **Shimmer, blackhole and long multiband halls**: shared but high; one at a time at most, and probably not with 8
  voices playing.
- **16-band vocoder**: shared medium-high, and it needs an audio input anyway.
- **Three multitimbral parts each with its own effect chain**: three effect chains plus the voices; high.

Everything in Tiers 1-2 except unison, FM oversampling and the non-linear ladder is low or none per voice and should
fit comfortably.
