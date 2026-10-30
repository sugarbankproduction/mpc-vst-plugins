# Moog One vs ONE-ish: feature and parameter gap audit

Refreshed 2026-10-27 against the code (desktop-first). Sources: the Moog One owner's manual (edition 8/2/19, 158 pages;
page numbers below are the printed page numbers, which match the PDF pages) read in full, and ONE-ish as of commit
cfb10b8 (branch `one-ish`): `ports/one-ish/params.json` (461 params), `src/engine.c`, `src/osc.h`, `ladder.h`,
`svf.h`, `env.h`, `lfo.h`, `modmatrix.h`, `seq.h`, `macro.*`, `randomize.h`, `src/fx.h` and `src/fx/*`, the desktop
window (`desktop/editor.c`, `editor_draw.c`, `gen_layout.py`, `patches.c`), `presets.json` (35 factory presets),
`docs/PATCHES.md` and `PLAN.md`. Every row below was checked against that code, not against the row's earlier text.
The 64-step sequencer (section 17) was built after cfb10b8 (2026-10-27, `docs/SEQUENCER.md`); its rows, and the ones
it changed in sections 13, 14 and 22b, were updated against that code. Packages 8 (in part), 9 and 13 (MIDI channel,
16 voices, three Synths; 2026-10-28, `docs/MULTITIMBRAL.md`) updated the rows they touched and the counts below, and
so did package 11 (the desktop's audio input and the EXT channel, 2026-10-29).

Manual wording is summarised, not quoted; only parameter names and ranges are copied.

## How to read this

**Status:** Have = ONE-ish does it (perhaps in a different form); Partial = some of it, or a fixed version of
something the Moog One makes adjustable (the row says what is missing); Missing = not there.

**Tags in the feature column** (judged for the desktop VST2, the product being finished first):
- **[HW]** a plugin genuinely can't do it, or it is the host's job: the instrument's jacks (CV, inserts, pedal
  wiring, mic trim), panel hardware (LEDs, fans, LCD), its own storage, USB, LAN and firmware, and the host-owned
  master volume, tempo and transport. Listed for completeness; not build candidates.
- **[UI]** window, menu or librarian behaviour, not sound. On the desktop these are build candidates: the plugin
  window can offer them.
- **[ANALOG]** analog circuit behaviour that software can only approximate (it can be modelled, never matched).
- **[LIC]** a licensed or trademarked algorithm (the Eventide reverbs). It cannot be copied; only an original
  effect in the same spirit can be built.
- **[MULTI]** only meaningful once ONE-ish has multitimbral parts (3 Synths per preset).

Things the earlier audit called hardware-only that a desktop VST2 **can** do are now untagged candidates: an audio
input into the instrument (the mixer's EXT channel, the vocoders), several stereo outputs (per-Synth routing to MAIN and
SUB), MIDI out of the plugin's own notes, and the X/Y pad (the window has one).

**Effort:** S = a few hours, contained change (under ~100 lines); M = one to three days, new code in one module;
L = more than three days or a new subsystem.

**Per-voice CPU impact**, relative to one ONE-ish voice today (about 8 us per voice per 128-sample block on the PC for
a busy patch, PLAN.md; nothing is measured on the MPC yet): none = no audio-path work; low = under ~10% of a voice;
medium = ~10-40%; high = over ~40%, or the feature adds whole voices. Effects and other post-voice work run once, not
per voice, so they read "none/voice" plus their shared cost.

**Not in the Moog One but in ONE-ish (kept for reference, not counted):** ladder Drive (`flt_drive`), Ladder Drive
and Amp Level as matrix destinations, the effects' CTRL 1/2 as offsets on their targets; Analog Drift, Spread and every
continuous effect control as matrix destinations (the effects from global sources, an effect pass per control step),
Sostenuto as a source, and a ribbon (CC 16 position, CC 17 touch; spring or hold, centre or left origin) as a source
with its own quick assignment; the ANALOG CHARACTER voice cards (`an_*`); the MACRO channel (Mutable Instruments
Plaits, 24 models, `mac_*`, a sixth mixer channel); open-source synth effect types (Rings, Clouds, Airwindows tape,
density, console) and the Space reverb (Airwindows Galactic); the GENERATIVE section (`gen_*`: Euclid, probability,
velocity random, Turing machine; 2026-10-29 the bouncing balls and particles, `phys_*`, playing in each Synth's KEY and
SCALE; 2026-10-30 rain and ripples, `rain_*`, and a Markov melody that learns from the keys played, `mkv_*`, with four
matrix sources: Rain Drop, Ripple Energy, Markov Step, Markov Height; GAME OF LIFE, GRAVITY ORBITS and PENDULUMS, `life_*`, `orb_*`, `pend_*`, with five matrix sources); 14 built-in chord shapes; the patch randomiser (RANDOM page, mutate, locks, undo, WILD, arp / GENERATIVE / sequencer ticks);
user patch files (2026-10-27, offline).

---

## 0. Architecture and overview (p8-10)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| All-analog voice path; digital FX can be bypassed for a 100% analog path [ANALOG] | p8, p96, p140 | Partial | Virtual-analog models: PolyBLEP oscillators, zero-delay-feedback ladder (optionally per-stage tanh, `an_ladder`), TPT SVF pair; HQ MODE (`an_hq` Off / 2x / 4x, per Synth, the ANALOG page) runs the voice's oscillators and filters oversampled with half-band decimation (`src/hq.h`, 2026-10-30, offline and the desktop window). The effects bypass exactly (`modfx_on`, `mfx_on` Off) | L (to deepen the models) | medium-high (HQ: 1.8x / 3x a voice) |
| Polyphony | 8 or 16 voices (p8, p140) | Have | `voices` Mono, 2-16 (MAX_VOICES 16; the voice cards 1-8 as before, 9-16 between them); an instance's three Synths share one 16-voice pool (2026-10-28, offline and the desktop window) | - | high (each voice) |
| Three-part multitimbral: up to 3 Synths per Preset, split, layered or zoned [MULTI] | p8-9 | Have | Three Synths per instance (`src/synths.h`, `docs/MULTITIMBRAL.md`): Synth 1's keys as before, Synths 2 and 3 as `s2_` / `s3_` copies; split and layer by key range, three factory performances (2026-10-28, offline and the desktop window) | - | high (voices are shared) |
| Per-Synth Timbre: own sound, note range, voice count, MIDI channel, arp, sequencer [MULTI] | p9 | Have | Each Synth has every sound setting, `key_lo`/`key_hi`, `voices`, `syn_ch`, its own arp, chord memory, generative steps and sequencer (2026-10-28, offline and the desktop window) | - | none |
| Hardware voices shared between Synths at will [MULTI] | p9 | Have | One pool of 16: each Synth within its POLY COUNT, voices of any Synth stolen when the pool is full (released first, then by the new note's Synth's stealing mode; No Stealing drops the note) (2026-10-28, offline and the desktop window) | - | none |
| One Synth Effect per Synth plus two Master Effects on sends | p9, p96-101 | Have | A synth effect per Synth (`modfx_*`, its own on, type, controls, mix; its own matrix moves it) and two master effects of any type on the Synths' sends (`mfx_send1/2` per Synth); five at once (2026-10-28, offline and the desktop window) | - | none/voice; shared medium |
| Voice: 3 VCOs, ring mod, noise, external input, mixer to SVF pair and ladder, VCA | p9-10 | Have | 3 oscillators, ring mod, noise (and MACRO) and the EXT channel (the desktop plugin's stereo audio input, 2026-10-29) into the SVF pair and the ladder, then the VCA (on the MPC the input is silent: the wrapper's instrument mode has none) | - | none |
| Each Synth: 4 LFOs and 3 EGs per voice (manual wording p10) | p10 | Have | 3 EGs and 4 LFOs per voice: an LFO runs per voice when it restarts per note or the matrix moves it per voice, else every voice follows one free-running LFO (2026-10-27, offline) | - | - |
| Modulation: hard-wired, Quick Assign (DEST buttons), Mod Matrix with controllers and transforms | p10, p67 | Have | Hard-wired amounts, a 16-slot matrix with controllers and the four transforms; QUICK ASSIGN (`desktop/quickmod.c`, 2026-10-30, the desktop window): DEST buttons and Alt-click / right-click on the sources, a click on a knob connects it (see 15a); CABLES (`desktop/cables.c`, 2026-10-30, the desktop window): the rows drawn as patch cables over the panel, an amount knob on each, X to remove | - | low |
| Programmable: every knob and hidden parameter stored in Presets | p8 | Have | Full state per instance (`get_state`/`set_state`, non-default values only, within the chunk: 8 KB on the MPC, 256 KB on the desktop (`VST2_CHUNK_MAX`, 2026-10-27)), 35 factory programs, user patch files (`patches.c`) | - | none |

## 1. Front panel, centre console, keyboard (p10-18)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Traditional knobs, soft knobs, master encoder; lit and red buttons [HW] | p11 | Missing | The window's drawn knobs, buttons and PRESET knob stand in (mouse) | - | none |
| MORE pages: a second layer of parameters per module [UI] | p11, every module | Have | The window's MORE triangles open each module's second layer on the centre screen (sub-pages past 16 items) | - | none |
| DEST button on each LFO and EG: next touched control becomes a destination [UI] | p11, p43, p47, p68 | Have | A DEST button in the title strip of LFO 1-4, the three envelopes and NOISE (Noise EG): lit while picked; each knob clicked then is connected in the first free matrix row of the Synths in focus, its drag the amount; DEST again or Esc ends it; QUICK ASSIGN (`desktop/quickmod.c`, 2026-10-30, the desktop window) | - | none |
| HOME screen: preset, timbre names, key ranges, synth on/off, tempo, tags [UI] | p12-13 | Partial | The name line shows the patch (F/U, `*` when edited); each page's title the Synth in focus; the SETUP page the three Synths' on/off, channels and key ranges (2026-10-28, offline and the desktop window); the home line under the name its tags, NOTES and the host's tempo (2026-10-29, offline and the desktop window; `docs/PATCHES.md`); no timbre names | S | none |
| SETTINGS, MOD, DELETE, SNAPSHOT, SAVE, COMPARE buttons [UI] | p13-14 | Have | MATRIX (the MOD screen), the PATCHES page's SAVE / DELETE, and COMPARE and SNAPSHOT on the console (2026-10-29, offline and the desktop window; `docs/PATCHES.md`); SETTINGS on the console (2026-10-29, the desktop window; `docs/SETTINGS.md`) | - | none |
| SHIFT: fine resolution on soft knobs, latching shift [UI] | p14 | Have | Shift-drag is the fine resolution on every knob (`editor.c`); a latching shift isn't needed with a keyboard modifier | - | none |
| New initialised preset (SHIFT + COMPARE) | p14 | Have | The "Init" program | - | none |
| Panel Focus: SYNTH 1/2/3, multi-select for joint edits, SHIFT = solo [UI][MULTI] | p14-15 | Have | SYNTH 1/2/3 buttons on the console: the panel, MORE pages, matrix, step editor and randomiser show and edit that Synth; Ctrl-click edits several together (the last picked shown); shared sections marked SHARED (2026-10-28, offline and the desktop window). Solo: Shift-click a SYNTH button or its S half (`syn_solo`, not saved; only the soloed Synths heard, a 10 ms glide); its M half switches the Synth's ON (a double-click on the button solos, 2026-10-30), red while off; keys 1-3 pick a Synth (2026-10-29, offline and the desktop window) | - | none |
| Keyboard Control: per-Synth ON, LOW/HIGH RANGE key zone, both = full G0..C10 [MULTI] | p15 | Partial | `syn_on`, `key_lo`, `key_hi` per Synth (SETUP page), as note names (2026-10-28, offline and the desktop window); set by value, not by pressing LOW RANGE and then a key | S | none |
| Expression Assign buttons: MW, PAD X, PAD Y, KB, VEL, AT, EXP 1 [UI] | p15, p68 | Have | No buttons: Alt-click or right-click the mod wheel, the pitch wheel, the X/Y pad (X, Y or its gate), the ribbon or the keyboard (velocity, aftertouch, the key, its gate, the pedals EXP 1 / EXP 2 / sustain / sostenuto, a constant), then click the knobs as with DEST; QUICK ASSIGN (`desktop/quickmod.c`, 2026-10-30, the desktop window) | - | none |
| Selecting presets from the browser (list, timbre pane, tag pane, tag filters) [UI] | p15-16 | Partial | The PATCHES page lists factory presets and user patches and loads on a click; PREV/NEXT and the PRESET knob step through them; the TAGS page filters the list and PREV/NEXT by TYPE, CATEGORY, MOOD and GROUP (2026-10-29, offline and the desktop window; `docs/PATCHES.md`); no timbre pane | see 20 | none |
| 61-key keybed, velocity and aftertouch [HW] | p17, p140 | Have | Via MIDI: velocity, channel pressure; the window's 61-key keyboard plays velocity by click height | - | none |
| Selectable velocity curves | p17, p116 | Have | `vel_curve` (see 19) | - | none |
| OCTAVE buttons; both = default octave; hold both = MIDI panic | p17, p112 | Have | `octave_shift` -2..+2 per Synth; the window's OCTAVE buttons: held 1 s back to the default octave, 2 s a MIDI panic (pedals up, All Notes Off, All Sound Off on every channel) (2026-10-28, the desktop window) | - | none |
| Oscillator OLED waveform displays [UI] | p10, p22 | Have | Each oscillator panel draws its wave from wave, angle, pulse width and mix (`draw_wave`), static as on the instrument | - | none |
| Mod wheel position saved with the preset | p18, p113 | Have | With LOAD SAVED POS (19) on Mod Wheel or Both, the wheel's position is saved with the sound (`wheel_pos`) and put back when it loads; the window's wheel follows (2026-10-29, offline and the desktop window) | - | none |

## 2. Rear panel (p19-21)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MAIN L/R and SUB L/R balanced outs, each Synth to either or both [HW] | p19 | Partial | One stereo output pair (per-Synth multi-out routing is a [MULTI] candidate, section 11) | - | none |
| INSERT 1-4 send/return jacks, stereo-normalled pairs [HW] | p19, p56 | Missing | - | - | none |
| EXT INPUT jacks: XLR/TRS combo, TRIM -9 dB to +65 dB; LINE input [HW] | p20 | Missing | The jacks and trim are hardware; a desktop VST2 instrument can take audio input from the host (see 6) | - | none |
| CV IN 1-2, CV OUT 1-4; pedal jacks usable as CV inputs [HW] | p20-21 | Missing | - | - | none |
| MIDI IN/THRU/OUT (DIN), USB MIDI, USB host, LAN [HW] | p20-21 | Partial | MIDI from the host only | - | none |
| Pedal jacks: SUSTAIN, EXP 1, EXP 2, assignable | p21, p140 | Have | Sustain and sostenuto (CC 66); the matrix sources Exp 1 and Exp 2; the SETTINGS page sets the sustain, EXP 1 and EXP 2 pedals' CC numbers (default 64, 11, 4; a moved pedal no longer answers to its old number) (2026-10-29, offline and the desktop window) | - | none |

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
| MORE page wave image and values [UI] | p25 | Have | The wave is drawn on the oscillator's panel (see 1); its MORE page lists the values | - | none |
| Analog VCO character: slight drift and warm-up instability [ANALOG] | p7 (warm-up note) | Have | `drift` 0-100%: each oscillator wanders at random within +-4 cents (and each voice's cutoffs within +-0.04 octave), a new target every 0.3-1.5 s, at control rate; 0 is exactly off (2026-10-27, offline). WARM-UP (2026-10-30, offline and the desktop window): `an_warm` On, `an_warm_amt`, `an_warm_time` 1-20 min (default 5): after the plugin starts or the switch goes on, each voice's oscillators sit off (up to about +-25 cents at 100%: one offset for the Synth, one per oscillator, fixed seed) and the cutoffs (+-0.12 octave), and the drift wanders up to 3 times deeper; all of it fades as exp(-3t / time) to the normal `drift` (offline: 5.8 cents off on average at the start, within 0.5 cent and still after 90 s at a 1-minute time). Off: the old samples. Fixed per-voice tuning, cutoff, resonance, envelope-time and level spreads and a non-linear ladder in the ANALOG section (`an_*`, 2026-10-27, offline) | - | low |
| Clean top end under sync and FM | implied (analog) | Have | PolyBLEP, and HQ MODE (`an_hq`, 2026-10-30, offline): 2x or 4x oversampled oscillators and filters with IIR half-band decimators (flat to 20 kHz, 106 dB down from 24.1 kHz). Offline, the power that is not a harmonic: hard sync (notes 84-103) -33..-40 dB Off, -61..-67 dB 2x, -71..-82 dB 4x; linear FM 40% (notes 84-96) -43..-51, -63..-73, -81..-87 dB; the note's level within 0.25 dB, its pitch within 0.01 cent | - | medium-high when on (PC: 1.8x a voice at 2x, 3x at 4x); none when Off |

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
| Noise EG: ATTACK | 1 ms to 10 s (p29) | Have | `noise_a` 1 ms-10 s (log knob), shaping the noise when `noise_env` is On (default Off) (2026-10-27, offline) | - | low |
| Noise EG: RELEASE | 1 ms to 10 s (p29) | Have | `noise_r` 1 ms-10 s | - | low |
| Noise EG: SUSTAIN button (off: release acts as decay after the attack) | on/off (p29) | Have | `noise_sus` | - | none |
| Noise EG as a matrix source; its stages as destinations | p29, p75-76 | Have | Source "Noise EG" (per voice, 0..1); destinations "Noise EG Attack", "Noise EG Sustain", "Noise EG Release" (see 15e) (2026-10-29, offline) | - | low |
| Colours: white, red (-6 dB/oct), purple (+6 dB/oct) | p30 | Have | Red: white through a 40 Hz one-pole low-pass; purple: its first difference; same RMS as white. Measured -6.1 / +5.8 dB/oct over 250 Hz-4 kHz (2026-10-27, offline) | - | low |
| COLOR pair select | Red+White, Red+Purple, White+Purple (p30) | Have | `noise_color_pair` | - | none |
| COLOR MIX between the two selected colours (also a matrix destination) | knob (p30, p76) | Have | `noise_color_mix` 0-100%, equal-power crossfade; matrix destination "Noise Color Mix" (2026-10-27, offline) | - | low |
| Pink noise by mixing red and white | p30 | Have | Red+White near 50% (about -2.5 dB/oct) | - | - |

## 6. Mixer (p31-32)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Six input channels: OSC 1-3, RING MOD, NOISE, EXT | p31 | Have | Oscillators 1-3, ring mod, noise, EXT (plus MACRO, beyond the Moog One): EXT is the desktop plugin's audio input, its mono sum, into every sounding voice's filters and VCA (2026-10-29, offline and the desktop DLL) | - | low |
| Level knob per channel (oscillators, noise) | 0 to max (p31-32) | Have | `o1_level`..`o3_level`, `noise_level` | - | - |
| RING MOD level | p31 | Have | `ring_level`; matrix "Ring Level" | - | - |
| SVF and LADDER bus buttons per channel (either, both, neither) | p31-32 | Have | `oN_svf`/`oN_lad`, `noise_*`, `ring_*`: two buttons per channel | - | - |
| EXT INPUT LEVEL | 0% to 100% (p32) | Have | `ext_level` (default 0: off). The desktop DLL declares two inputs (`desktop/editor.c`, not the shared wrapper) and converts the host's rate to 44.1 kHz (`desktop/hostrate.c`); the input arrives a fixed 128 samples late at 44.1 kHz (about 223 at 48 kHz), outside the reported latency, so notes play as before. Level 0, or no input connected, gives exactly the old samples (2026-10-29, offline and the desktop DLL) | - | low (about 1 us a voice per block on the PC while heard; none at level 0 or with no input) |
| EXT INPUT ROUTE | None, SVF, Ladder, Both (p32) | Have | `ext_route` (default Ladder), on the mixer's MORE page (2026-10-29) | - | low |
| EXT INPUT SOURCE [HW] | None, Mic/Line, Line, Mic/Line+Line (p32) | Missing | A choice of physical jacks; on the desktop the host picks the input | - | none |
| MORE page bar-graph of levels and buses [UI] | p32 | Have | The mixer's MORE page (its triangle): every level and bus, and a bar per source as tall as its level, its SVF and LADDER halves lit by its buttons (`desktop/more_pics.c`, 2026-10-29, desktop) | - | none |
| Mixer levels as matrix destinations | p76 | Have | "Osc n Level", "Noise Level", "Ring Level" | - | - |

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
| Transistor-ladder character: per-stage non-linearity, how resonance thins the bass [ANALOG] | p34 | Partial | `an_ladder` blends the ladder towards per-stage tanh saturation (zero-delay, state-evaluated; +21% a voice on the PC); default linear ZDF ladder with one input saturator, half bass compensation. HQ MODE (2026-10-30, offline) runs the ladder oversampled: a triangle through it at 100% drive and `an_ladder` 100% (notes 91-100) has its images at -24..-45 dB Off, -41..-64 dB at 2x, -78..-87 dB at 4x, and the state-evaluated stages track the circuit better at the higher rate (about 1 dB more compression). Left: an iterative (Newton) solve of the stages for HQ; not done, as 4x already takes the images below -77 dB | L (iterative solve) | medium-high (HQ: 1.8x / 3x a voice) |
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
| Filter MORE page diagram [UI] | p38 | Have | The filters' MORE page: IN to the SVF and the ladder (series: the SVF also into the ladder) and the MIX, each filter's mode, slope and response curve at its cutoff and resonance (the SVF pair with its spacing), LINKED (`desktop/more_pics.c`, 2026-10-29, desktop) | - | none |

## 8. Envelopes (p40-45)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Three EGs per voice: filter, amplifier, modulation | p40 | Have | `fenv_*`, `amp_*`, `menv_*` | - | - |
| DELAY TIME (MORE) | 0.0 ms to 10.0 s (p44) | Have | `*_delay`: 0 = off, then 1 ms to 10 s on the time knobs' log scale; holds the current level, then the attack starts (2026-10-27, offline) | - | none |
| ATTACK | 1 ms to 10 s, plus MIN (p41) | Have | `*_a` 1 ms to 10 s, log knob; curve set by `*_acurve` (default linear) | - | - |
| HOLD TIME (MORE) | 0.0 ms to 10.0 s (p44) | Have | `*_hold`: 0 = off, then 1 ms to 10 s; stays at the peak (2026-10-27, offline) | - | none |
| DECAY | 1 ms to 10 s (p41) | Have | `*_d`; curve `*_dcurve` (default 50% exp = the old one-pole decay, 99% in the set time) | - | - |
| SUSTAIN | 0 to 10 (p41) | Have | `*_s` 0-100% | - | - |
| RELEASE | 1 ms to 10 s (p41) | Have | `*_r`; curve `*_rcurve` (default 50% exp); ends at exactly the set time | - | - |
| MIN setting below 1 ms, ignored by TIME SCALE | p40 | Have | `eg_times` "MIN, 13 Syncs" (per Synth; the default "1 ms, 11 Syncs" is as before, so old sounds are unchanged): an attack, decay or release knob at its far left is MIN (0.2 ms), left alone by time scale and the voice cards, shown as "MIN" (CTRL page) (2026-10-29, offline) | - | none |
| ATTACK CURVE | 100% log to linear to 100% exp (p45) | Have | `*_acurve` -100..100%: one shared shaper (u = u m + b per sample), every curve takes the stage's whole time (2026-10-27, offline) | - | none |
| DECAY CURVE | 100% log to linear to 100% exp (p45) | Have | `*_dcurve` -100..100%, default 50 (2026-10-27, offline) | - | none |
| RELEASE CURVE | 100% log to linear to 100% exp (p45) | Have | `*_rcurve` -100..100%, default 50 (2026-10-27, offline) | - | none |
| MULTI TRIG: legato notes restart the envelope or not | on/off (p42) | Have | `*_multi` (default on): off, a new note on a voice whose key is still down carries on from the envelope's current position (2026-10-27, offline) | - | none |
| RESET: restart the attack from zero (mono/unison with multi trig) | off/on (p44) | Have | `*_reset`: every restart from zero (also a stolen or replayed voice in poly) (2026-10-27, offline) | - | none |
| SYNC: stage times as tempo divisions | 1/64 to 64 (13 values, p42) | Have | `*_sync`: with `eg_times` "MIN, 13 Syncs" the time knobs pick from the manual's 13 values, 1/64 to 64 beats (shown as 1/256 .. 16 bars), longest on the right; "1 ms, 11 Syncs" keeps the old 11 (1/32 to 4 bars); time scale does not apply (2026-10-29, offline) | - | none |
| LOOP: repeat the whole DAHDSR (trapezoid) | on/off (p42) | Have | `*_loop`: after the decay it goes straight to the release, then the delay again, while held (2026-10-27, offline) | - | none |
| LATCH: hold at sustain after key up; with LOOP keeps cycling | on/off (p42) | Have | `*_latch`: key up ignored until the voice is replayed or stolen, latch is switched off or All Notes Off (2026-10-27, offline) | - | none |
| TIME SCALE, keeps the stage ratios; modulatable | 10% to 1000% (p44) | Have | `*_scale` 10-1000%, log knob with 100% in the middle; matrix "... Env Time Scale" per voice (2026-10-27, offline) | - | none |
| VEL MOD AMT: velocity scales the envelope output | -100% to +100% (p45) | Have | `*_vel`: amp default 100% (as before), filter and mod 0%; also scales the envelope as a matrix source (2026-10-27, offline) | - | none |
| DEST quick assign [UI] | p43 | Have | Each envelope's DEST button (see 1); its stage knobs and switches are destinations too; QUICK ASSIGN (`desktop/quickmod.c`, 2026-10-30, the desktop window) | - | none |
| Envelope picture with curves [UI] | p43, p45 | Have | Each envelope's MORE page (its triangle): the DAHDSR shape, each stage as wide as its time knob turns, bent by its curve as `src/env.h` bends it, the sustain level, LOOP / LATCH / SYNC (`desktop/more_pics.c`, 2026-10-29, desktop) | - | none |
| EG stage times and time scale as matrix destinations | p76 | Have | Delay, attack, hold, decay, sustain, release and time scale of each EG, per voice (see 15e) | - | low-medium |

## 9. LFOs (p46-49)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Four LFOs (hard-wired: 1 to pitch, 2 to cutoffs, 3 to wave shape) | p46, p27-28 | Have | `lfo1_*` .. `lfo4_*`; LFO 1 to pitch, LFO 2 to cutoffs, LFO 3 to wave angle and width (2026-10-27, offline) | - | - |
| LFOs per voice (manual p10) so they can restart per note | p10 | Have | Per voice when restarted (note-on reset, repeats) or modulated per voice; free-running otherwise (2026-10-27, offline) | - | - |
| RATE | 0 to 10 within the RANGE (p46) | Have | `lfoN_rate`, three decades on a log knob within the range (2026-10-27, offline) | - | - |
| RANGE | Slow 0.001-1 Hz, Low 0.01-10 Hz, Medium 0.1-100 Hz, High 1 Hz-1 kHz (p48) | Have | `lfoN_range` Slow, Low (default), Medium, High. High runs per sample in each voice: on pitch (LFO 1 hard-wired, matrix Pitch / Osc n Pitch), both cutoffs (LFO 2 hard-wired, matrix cutoffs) and Amp Level, where the matrix slot is linear in it (source or controller, no transform); its other destinations follow it at the control rate (2.76 kHz) (2026-10-29, offline) | - | medium for a High LFO (PLAN.md) |
| WAVE: Triangle/Sine | p46 | Have | Triangle and Sine, each morphing into the other with `lfoN_var` (2026-10-27, offline) | - | - |
| WAVE: Pulse with variable width | 0% always off .. 100% always on (p46, p48) | Have | The wave "Pulse": its width is `lfoN_var` over the whole range, 0% always off, 50% a square, 100% always on; Square keeps its old narrowing (old sounds unchanged) (2026-10-29, offline) | - | none |
| WAVE: Saw/Ramp | p46 | Have | Saw and Ramp, each morphing through a triangle into the other (2026-10-27, offline) | - | - |
| WAVE: S&H/Noise (smooth random) | p46 | Have | S&H and Noise (smooth random, the same random steps glided), each morphing into the other (2026-10-27, offline) | - | - |
| VARIATION: morph within the wave category | 0% to 100% (p48-49) | Have | `lfoN_var`, from the chosen wave towards its partner; a matrix destination (2026-10-27, offline) | - | - |
| POLARITY | Unipolar, Bipolar (p49) | Have | `lfoN_polarity` Bipolar / Unipolar (2026-10-27, offline) | - | - |
| START PHASE (used on reset, repeats, clock start) | 0% to 100% (p49) | Have | `lfoN_phase`, on note-on reset and repeats, and for a synced LFO when the host transport starts (2026-10-29, offline) | - | none |
| SYNC to the Master Clock | on/off (p49) | Have | `lfoN_sync`, host tempo | - | - |
| SYNC DIVISION | 64, 32, 16, 8, 4, 2, 1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64 (p49) | Have | 13 divisions, 16 bars .. 1/256 (64 .. 1/64 quarter-note beats), a table of the LFOs' own (2026-10-27, offline) | - | - |
| SYNC TIMING | Straight, Dotted, Triplet (p49) | Have | `lfoN_timing` Straight, Dotted (x1.5), Triplet (x2/3) (2026-10-27, offline) | - | - |
| Reset to start phase on Master Clock start when synced | p49 | Have | When the host transport starts, every synced LFO (free-running and each voice's own) goes back to its start phase (2026-10-29, offline) | - | none |
| NOTE-ON RESET | off/on (p49) | Have | `lfoN_reset`: the voice restarts its LFO at the start phase, with its own random values (2026-10-27, offline) | - | - |
| DELAY TIME before the LFO starts | Off, 0.1 ms to 10.0 s (p50) | Have | `lfoN_delay` Off, 1 ms .. 10 s, per voice from the note-on; a restarted LFO waits at its start phase (2026-10-27, offline) | - | - |
| FADE IN TIME | Off, 0.1 ms to 10.0 s (p50) | Have | `lfoN_fade_in` Off, 1 ms .. 10 s (2026-10-27, offline) | - | - |
| FADE OUT TIME | Off, 0.1 ms to 10.0 s (p50) | Have | `lfoN_fade_out` Off, 1 ms .. 10 s, after the fade in (2026-10-27, offline) | - | - |
| REPEATS: a set number of cycles after note-on | Infinite, 1 to 32 (p50) | Have | `lfoN_repeats` Inf, 1..32; then the LFO stays at 0 until the next note-on (2026-10-27, offline) | - | - |
| SMOOTHING: one-pole lowpass on the LFO | 0% to 100% (p50) | Have | `lfoN_smooth`: cutoff 1 kHz at 1% down to 0.1 Hz at 100% (2026-10-27, offline) | - | - |
| DEST quick assign; wave picture [UI] | p47-48 | Have | The wave picture: each LFO's MORE page draws two cycles from its start phase with `src/lfo.h`'s own wave, variation, polarity and smoothing, with its rate (2026-10-29, desktop). DEST quick assign: each LFO's DEST button (see 1), its RATE and WAVE destinations too (QUICK ASSIGN (`desktop/quickmod.c`, 2026-10-30, the desktop window)) | - | none |
| Rate LEDs flash at the LFO, arp and sequencer rates; tempo LED on each beat [UI] | p46, p61, p78, p82 | Have | An LED by each LFO's RATE, over the arp's RATE, beside the sequencer's RATE (on each step it plays, from `seq_pos`), and TEMPO in the arp's row: rates from the params as the window reads them, the tempo and beat from the host's time info on the UI thread (120 BPM without), synced ones on the host's beat while it plays; only the LED repaints (`desktop/rate_leds.c`, 2026-10-29, desktop; about 0.05 ms) | - | none |

## 10. VCA (p50-52)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| LEVEL of the active Synth | 0 to 10 (p51) | Have | `volume`; matrix "Amp Level" | - | - |
| PAN of the active Synth | Left to Right (p51) | Have | `pan` -100..100%, equal power (centre exactly as the old mono sum, hard left +3 dB on the left); the synth effect's wet sits at the pan (2026-10-27, offline) | - | none |
| VCA MORE: level and pan of all three Synths on one page [MULTI][UI] | 0-100%, 100% L to 100% R (p52) | Have | The SETUP page: each Synth's `volume`, `pan`, sends, synth effect on, side by side; `master_level` after the mix (2026-10-28, offline and the desktop window) | - | none |
| SPREAD: per-voice stereo placement around the pan | 0% to 100% (p52) | Have | `spread`: each new note takes the next place in an alternating pattern (+-1, +-0.5, +-0.75, +-0.25) around the pan; a unison stack fans out by its place in the stack; mono stays on the pan (2026-10-27, offline) | - | low |
| Per-voice pan as a matrix destination | p76, p96 | Have | "Pan" (+-100% = the centre to a side) and "Spread" (+-the knob), per voice (2026-10-27, offline) | - | low |

## 11. Output (p53-56)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MASTER VOLUME, HEADPHONES (global, never stored) [HW] | 0 to 10 (p53) | Missing | The host's fader and the sound card | - | none |
| Per-Synth routing to MAIN and SUB outputs [MULTI] | on/off each (p54) | Have (desktop) | 2026-10-30, offline and the desktop DLL: eight outputs, MAIN (1-2) and a pair per Synth (3-4, 5-6, 7-8) rather than one shared SUB; each Synth's OUTPUT (`syn_out`: Main, Own Pair, Main + Own; SETUP page and SYNTH OUTPUT's MORE page). The master effects' returns stay on MAIN. The MPC build has one stereo output: every Synth on it | - | none/voice; converters while used (PLAN.md item 38) |
| SUB VOLUME [MULTI] | -96 dB to +22 dB (p54) | Partial | Each Synth's own pair carries its VOLUME and the master level; the host's fader on that pair is the SUB VOLUME | - | none |
| Per-Synth routing to INSERT 1-4, one Synth per insert; EXT 1/4" to Insert 3, EXT XLR to Insert 4 [HW] | on/off (p54-55) | Missing | - | - | none |
| INSERT 1-4 VOLUME [HW] | -96 dB to +22 dB (p55) | Missing | - | - | none |
| Synth Effect applied before the insert send [HW] | p56 | Missing | - | - | none |

## 12. Performance Sets and text entry (p57-60)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Performance Sets: up to 128 sets of 64 preset pointers (banks A-H x 1-8) [UI] | p57, p141 | Have | 128 sets of 64 factory presets or user patches, banks A-H x 1-8 on the SET page, saved in `sets.oneishsets` beside the patches (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| Assign current preset by long press; dim/lit/dark bank and preset buttons [UI] | p57 | Have | Holding a number 0.6 s assigns the current patch (ASSIGN and CLEAR too); the bank letters and numbers are lit (shown / playing), normal (holding patches) or dark (empty); a bank letter previews its eight patches without loading (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| CURRENT SET, BROWSE SETS, ARRANGE SETS: move, copy, delete, add, name, notes [UI] | p58-59 | Have | The SET, ALL SETS and ARRANGE pages (UP, DOWN, COPY, DELETE, ADD, NAME, NOTES) (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| Remote MIDI program and bank selection of sets | p57 | Have | Bank select MSB 1 + LSB n, program 0-63: set n + 1's A1-H8; MSB 0 + LSB n, program p: position 128n + p of the list (factory presets, then user patches A-Z); loaded on the window's thread (a hidden window when the editor is closed) (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| Text entry from the keyboard or a USB keyboard [UI] | p60, p143 | Have | The window's name prompts take typing (Windows text boxes) | - | none |

## 13. Master Clock (p60-62)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| TEMPO | 10 BPM to 280 BPM (p61) | Have | The host's tempo is the master clock (`lfo_bpm`, 20-400 BPM; the arp also reads the song position's tempo) | - | none |
| TAP tempo [HW] | p61 | Missing | Host | - | none |
| NUDGE + / - and NUDGE MODE [HW] | Increment (1 or 0.1 BPM), Bend (2 or 1 BPM) (p62) | Missing | Host | - | none |
| SWING AMOUNT (for arp and sequencer) | 0% to 50% (none) to 100% (p62) | Have | `clock_swing` (the master swing, 50: none), followed by the sequencer (FOLLOW SWING) and, with `arp_follow_swing`, the arp (synced; otherwise its own `arp_swing`) (2026-10-29, offline) | - | none |
| LOAD PRESET TEMPO [HW] | on/off (p62) | Missing | A plugin can't set the host's tempo | - | none |
| External clock overrides the Master Clock | p60 | Have | The host clock always applies | - | none |
| What can sync: arp, sequencer, LFOs, EGs, effects | p60 | Have | LFOs, EGs (`*_sync`), the synth effect and delay (`modfx_sync`, `dly_sync`), the arp and the sequencer (`seq_sync`) sync to the host (2026-10-27, offline) | - | none |
| Current tempo shown on the HOME screen [UI] | p60, p12 | Have | The host's tempo on the home line under the patch name, when the host reports one (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |

## 14. Voice allocation: Hold, Chord, Polyphony (p63-66)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| HOLD: latch held notes; legato adds, non-legato replaces; with sustain it adds; latches the arp | on/off (p63) | Have | `hold`: as the manual; off lets go of what it latched (the pedals keep theirs); latches the arp's keys (2026-10-27, offline) | - | none |
| CHORD memory: one key plays a stored interval stack, first note is the root | on/off (p63) | Have | `chord_on`, `chord_shape` (Custom = the learned chord in `chord_iv1..5`, +-24 st; or 14 built-in shapes), root first; voices as the chord needs (2026-10-27, offline) | - | high (multiplies sounding voices) |
| CHORD LEARN, saved per Synth; silent or audible learn (Settings) [UI] | p63, p125 | Have | `chord_learn` on/off per Synth: first key clears and is the root, up to 5 more; off stores it and picks Custom; saved with the preset; `learn_mode` Audible or Silent (2026-10-28, offline) | - | none |
| Chord is applied after the arp and sequencer | p63 | Have | Every arp / generative note and every sequencer note plays its chord (2026-10-27, offline) | - | none |
| MONO | on/off (p64) | Have | `voices` = Mono, legato, with the note priority below | - | - |
| UNISON: all voices stack, divided among held keys | on/off (p64) | Have | `unison`: stacks of `uni_count` voices; when the held keys need more voices than whole stacks leave, the poly count is shared out among them (16 voices, count 16: one key 16, two keys 8 each), the biggest stacks giving up their outermost voices (2026-10-28, offline) | - | high (each stacked voice costs a full voice) |
| DETUNE of the unison stack, uneven spread around the centre | 0 to 10 (p64) | Have | `uni_detune` 0-100%: voices at u*sqrt(abs(u)) of +-50 cents (u evenly -1..1), symmetric, the centre voice stays put; stacks scaled by 1/sqrt(n) in level (2026-10-27, offline) | - | none |
| UNISON COUNT (also works in poly mode) | 2 to POLY COUNT (p65) | Have | `uni_count` 2-16, capped at the voice count in poly; mono + unison stacks it on one note (2026-10-28, offline) | - | high |
| POLY COUNT | 2 to hardware voices (p65) | Have | `voices` Mono, 2-16 per Synth, all three within the shared 16 (2026-10-28, offline) | - | high per voice |
| VOICE STEALING MODE | No Stealing, Oldest, Quietest, Newest (p66) | Have | `steal_mode`, after an idle voice: Oldest (default: quietest releasing, then oldest held by a pedal or HOLD, then oldest held), Quietest (lowest level x velocity of all), Newest (quietest releasing, then newest), No Stealing (quietest releasing, else the note is dropped) (2026-10-27, offline) | - | none |
| MONOPHONIC NOTE PRIORITY | Low, High, Last (p66) | Have | `note_priority`; a key that loses to a held one changes nothing; releasing the sounding key moves to the next by priority, without a retrigger (2026-10-27, offline) | - | none |
| PITCH VARIANCE: random pitch per new note | 0 to 50 cents (p66) | Have | `pitch_var` 0-50 cents, uniform, drawn per voice for each new note (2026-10-27, offline) | - | none |

## 15. Modulation (p67-77)

### 15a. Structure

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Hard-wired: pitch (LFO 1, Mod EG), wave (LFO 3 to angle and width), FM, cutoff (EG, LFO 2, FM) | p67 | Have | All of them; each amount is also a matrix destination (2026-10-27, offline) | - | - |
| Quick Assign: DEST and Expression Assign; ADD DESTINATION, EDIT MODULATIONS, DONE, CANCEL; turning the destination knob sets the amount without moving the knob's own value [UI] | p68-69 | Partial | QUICK ASSIGN (`desktop/quickmod.c`, 2026-10-30, the desktop window): DEST and the expression sources (see 1); every knob clicked is ADD DESTINATION (the row with the same source and destination is reused), its drag the amount (up positive, down negative) without moving the knob; Esc or DEST again is DONE; each connection one undo step (Ctrl+Z is CANCEL, one at a time); Ctrl-drag later changes an amount; rings on the modulated knobs, a right-click to REMOVE or OPEN IN MATRIX. Not: EDIT MODULATIONS (the matrix filtered by the source) | S | none |
| Matrix slots (numbered list; the count is not stated in this manual) | p70 | Have | 16 slots (`mod1_*` .. `mod16_*`) (2026-10-27, offline) | - | low (control rate; empty slots skipped) |
| Per-slot DISABLE/ENABLE without deleting | p70 | Have | `modN_on` per slot (default On): off, the slot keeps its settings and does nothing (the effect pass too); on the MATRIX page a click on the row's number switches it (red: off) (2026-10-29, offline and the desktop window) | - | none |
| SOURCE AMOUNT | -300% to +300% (p70) | Have | `modN_amt` -300% .. +300% (2026-10-27, offline) | - | - |
| CONTROLLER with its own amount (negative inverts; a controller alone acts as a source) | -100% to +100% (p71) | Have | `modN_via` with `modN_via_amt` -100..+100% (default +100%); a slot with only a controller uses it as the source (2026-10-27, offline) | - | - |
| Order: controller and transform first, source amount last | p70 | Have | As the manual (2026-10-27, offline) | - | - |
| TRANSFORM stage | see 15d (p71) | Have | `modN_xf` and `modN_xf_amt` (2026-10-27, offline) | - | - |
| DESTINATION chosen as module, then parameter [UI] | see 15e (p71) | Partial | One flat list of 154 destinations in a single menu | S | none |
| DELETE a slot (later slots renumber); filter list by source or destination (SHIFT + DEST / SHIFT + MORE) [UI] | p72 | Missing | - | S-M | none |
| SAVE / LOAD (add or replace) whole matrices as named presets with notes and timestamps [UI] | p73-74 | Missing | - | M | none |

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
| NOISE EG | Have | "Noise EG" (0..1, per voice; 2026-10-27, offline) | - | - |
| MOD WHEEL | Have | "Mod Wheel" (CC 1) | - | - |
| PITCH WHEEL | Have | "Pitch Wheel" (-1..1) | - | - |
| PAD X | Have | "Pad X (CC 2)", 0..1; the window's pad sends it (2026-10-27, offline) | - | - |
| PAD Y | Have | "Pad Y (CC 3)", 0..1; the window's pad sends it, and still sends channel pressure too ("Aftertouch") (2026-10-27, offline) | - | - |
| PAD PRESSURE | Missing | - (a mouse has no pressure: a modifier key or the wheel could stand in) | S | none |
| PAD GATE | Have | "Pad Gate (CC 18)", 1 while the window's pad is touched (2026-10-27, offline) | - | - |
| VELOCITY | Have | "Velocity" | - | - |
| AFTERTOUCH | Have | "Aftertouch" (channel pressure) | - | - |
| KEY PITCH | Have | "Keyboard" (-1..1 around the key track centre) | - | - |
| KB GATE | Have | "KB Gate" (the voice's key is down) | - | - |
| SUSTAIN | Have | "Sustain" (CC 64) | - | - |
| EXP 1 | Have | "Exp 1 (CC 11)" | - | - |
| EXP 2 | Have | "Exp 2 (CC 4)" | - | - |
| CV IN 1 [HW] | Missing | - | - | none |
| CV IN 2 [HW] | Missing | - | - | none |
| RANDOM (unipolar) | Have | "Random Uni" (0..1, per note) | - | - |
| RANDOM BIPOLAR | Have | "Random" (-1..1, per note) | - | - |
| CONSTANT | Have | "Constant" | - | - |
| (beyond the Moog One) the generators | Have | "Ball 1 Height", "Ball 1 X", "Balls Speed", "Ball Hit", "Particle Count", "Particle Hit", "Turing Value", "Euclid Gate": 0..1 per Synth, smoothed at control rate, at the end of the source and via lists (2026-10-30, offline and the window) | - | none measurable |

### 15c. Matrix controllers (p75)

| Moog One controller list | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|
| Same 26 entries as the sources (NONE, MOD WHEEL, PITCH WHEEL, PAD X/Y/PRESSURE/GATE, VELOCITY, AFTERTOUCH, KEY PITCH, KB GATE, SUSTAIN, EXP 1/2, CV IN 1/2, the four EGs, LFO 1-4, RANDOM, RANDOM BIPOLAR, CONSTANT) | Partial | `modN_via` offers the same list as the sources (25 + None, with Noise EG, Sostenuto, Ribbon and Pad X, Pad Y, Pad Gate); missing: pad pressure (a mouse has none), CV (hardware) | S | none |

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
| Oscillator 1-3: OCTAVE (step-mod list, p91) | Have | "Osc n Octave": whole octaves within 32'..2', +-100% = the whole list (+25% one octave) (2026-10-29, offline) | - | none |
| Noise: COLOR MIX | Have | "Noise Color Mix" (+-100% = the whole knob) | - | - |
| Noise: ATTACK, SUSTAIN, RELEASE (noise EG) | Have | "Noise EG Attack" / "Release" (+-the whole knob), "Noise EG Sustain" (a switch: +50% on, -50% off); per voice, worked out again only when a time moves by 1% (2026-10-29, offline) | - | none |
| Mixer: OSC 1, 2, 3 LEVEL | Have | "Osc n Level" | - | - |
| Mixer: RMOD LEVEL | Have | "Ring Level" | - | - |
| Mixer: NOISE LEVEL | Have | "Noise Level" | - | - |
| Mixer: EXT LEVEL | Have | "Ext Level" (+-100% of the knob; per voice; STEP MOD reaches it too) (2026-10-29, offline) | - | none |
| VCA: LEVEL | Have | "Amp Level" (x0..x2) | - | - |
| VCA: PAN | Have | "Pan" (+-100% = the centre to a side) | - | - |
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
| EG 1-3: MULTI TRIGGER, SYNC, LOOP, LATCH (step-mod list, p91) | Have | "... Env Multi Trig", "... Env Sync", "... Env Loop", "... Env Latch", per voice: switches, +50% turns one on, -50% off; a note on a step reads that step's MULTI TRIG at once; latch modulated off after key up lets go (2026-10-29, offline) | - | none |
| LFO 1-4: RATE | Have | "LFO n Rate" (+-100% = the whole knob; per voice) | - | - |
| LFO 1-4: RATE (SEMITONES) | Have | "LFO n Rate (st)" (+-100% = +-24 st) | - | - |
| LFO 1-4: VARIATION | Have | "LFO n Variation" | - | - |
| LFO 1-4: DELAY | Have | "LFO n Delay" | - | - |
| LFO 1-4: FADE IN TIME, FADE OUT TIME (RAMP UP/DOWN TIME in the step-mod list) | Have | "LFO n Fade In", "LFO n Fade Out" | - | - |
| LFO 1-4: WAVEFORM (step-mod list, p91) | Have | "LFO n Wave": +-100% = the whole wave list, in whole steps, per voice (2026-10-29, offline) | - | none |

## 16. Arpeggiator (p78-81)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Arpeggiator per Synth, saved with the Timbre | p78 | Have | `arp_*`, saved with the preset; plays the engine's own voices (2026-10-27, offline) | - | none |
| ON | on/off (p78) | Have | `arp_on` (2026-10-27, offline) | - | none |
| RATE, free | 10 BPM to 280 BPM (p78) | Have | `arp_rate` 10-280 BPM when `arp_sync` is Off, with the division and timing (2026-10-27, offline) | - | none |
| OCTAVE ORDER: each note through all octaves before the next note | on/off (p78) | Have | `arp_oct_order` (2026-10-27, offline) | - | none |
| PENDULUM: play the pattern back reversed | on/off (p79) | Have | `arp_pendulum` (2026-10-27, offline) | - | none |
| OCTAVE range | 1, 2, 3, 4 (p79) | Have | `arp_oct` 1-4 (2026-10-27, offline) | - | none |
| DIRECTION of the octaves | Up, Down, Up/Down, Down/Up (p79) | Have | `arp_dir` (Up/Down: 0 +1 +2 +1) (2026-10-27, offline) | - | none |
| PATTERN | Up, Down, Order, Random (p80) | Have | `arp_pattern` (2026-10-27, offline) | - | none |
| SYNC to the Master Clock | on/off (p81) | Have | `arp_sync` (default On): host tempo; with the transport playing, the host's song position (`HAS_SONG_POS`), steps on the song grid (2026-10-27, offline) | - | none |
| CLOCK DIVISION | 4, 2, 1, 1/2, 1/4, 1/8, 1/16, 1/32 (p81) | Have | `arp_div`, named 1/1 .. 1/128 (4 .. 1/32 beats) (2026-10-27, offline) | - | none |
| CLOCK TIMING | Straight, Dotted, Triplet (p81) | Have | `arp_timing` (2026-10-27, offline) | - | none |
| GATE LENGTH | 1% to 100% (p81) | Have | `arp_gate`; 100% ties into the next step (2026-10-27, offline) | - | none |
| FOLLOW SWING | off/on (p81) | Have | `arp_follow_swing` (default Off: `arp_swing` as before): synced, the arp plays the master swing `clock_swing` (ARP page) (2026-10-29, offline) | - | none |
| NOTE RESET: restart at once on a new note, or wait for the next division | off/on (p81) | Have | `arp_reset` (default On); with the transport playing, steps stay on the song grid (2026-10-27, offline) | - | none |
| PENDULUM MODE: end notes played once or twice | Once, Twice (p81) | Have | `arp_pend_mode` (2026-10-27, offline) | - | none |
| RANDOM MODE | Non-repeating, Repeating (p81) | Have | `arp_rnd_mode` (2026-10-27, offline) | - | none |
| HOLD latches the arpeggiator | p63 | Have | `hold` or `arp_latch` latch the arp's keys (legato adds, a new set replaces; the sustain pedal keeps keys while down) (2026-10-27, offline) | - | none |

## 17. Sequencer (p82-95)

Built 2026-10-27 (offline and the desktop window; `docs/SEQUENCER.md`): everything but OUTPUT and multi-Synth starts
[MULTI] (2026-10-29: RTZ, editing every note of a step at once, the 15e step-mod-only destinations and performance notes since;
multi-Synth starts 2026-10-28 as PLAY ALL; OUTPUT 2026-10-30).

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Polyphonic step sequencer per Synth, up to 64 steps in pages of 16 | p82 | Have | 64 steps, 8 note lanes, 8 modulation lanes (`src/stepseq.h`); plays the engine's own voices, through chord memory, beside the arp (2026-10-27, offline) | - | none |
| ON (whether the sequencer plays the Synth) | on/off (p82) | Have | `seq_on`; it can run silently with ON off (2026-10-27, offline) | - | none |
| RATE, free | 10 BPM to 280 BPM (p82) | Have | `seq_rate` with `seq_sync` Off (2026-10-27, offline) | - | none |
| CURRENT PAGE / ACTIVE STEP LEDs [UI] | p82 | Have | the SEQUENCER module's four PAGE LEDs, the step editor's playing column, the `seq_pos` readout (2026-10-27, offline) | - | none |
| RETURN TO ZERO, START/STOP, RECORD, ADV back/forward [UI] | p83 | Have | `seq_play`, `seq_rec`; ADV: the step editor's clicks and arrow keys; RTZ on the step editor's title line (2026-10-29, offline and the desktop window) | - | none |
| REST per step (a rest may keep its notes) | on/off (p84, p87) | Have | REST (its notes kept) (2026-10-27, offline) | - | none |
| TIE per step (no envelope retrigger) | on/off (p84, p87) | Have | per note lane: the same note goes on, another takes over its voice legato (2026-10-27, offline) | - | none |
| STEP MOD: record knob moves as per-step offsets while running (a new lane per knob) | on/off (p84, p89) | Have | `seq_step_mod`: a knob's move becomes the step's offset in a lane for its destination (made if missing); the knob stays (2026-10-27, offline) | - | none |
| TRANSPOSE from the keyboard (keys then do not sound) | on/off (p84) | Have | `seq_transpose` (2026-10-27, offline) | - | none |
| NOTE TRIG: play while keys are held, stop on release (excludes START/STOP) | on/off (p85) | Have | `seq_note_trig`, excluding `seq_play` both ways (the host is told) (2026-10-27, offline) | - | none |
| STEP EDIT screen with lanes (one note per lane per step) [UI] | p85-86 | Have | the window's step editor (SEQ: NOTES and MODS) (2026-10-27, offline) | - | none |
| Per-step NOTE | Off, C0 to G10 (p88) | Have | 0-127 or none (2026-10-27, offline) | - | none |
| Per-step VELOCITY | 1 to 127 (p88) | Have | per note (2026-10-27, offline) | - | none |
| Per-step GATE (the last value set applies to new notes) | 1% to 100% (p88) | Have | per note, of the step's own length; the last one set goes to new notes (2026-10-27, offline) | - | none |
| Per-step RATCHET (repeats inside one step) | 1 to 4 (p88) | Have | per note (2026-10-27, offline) | - | none |
| Edit all notes of a step at once; delete a note, a step or a lane (SHIFT + DELETE) [UI] | p88, p90 | Have | ALL in the step editor: NOTE moves every note of the step together, VEL, GATE, RATCH set them all (the engine's `stepnotes`); DEL STEP (ALL lit) and SHIFT + Delete empty the step; a note, an offset and a lane as before (2026-10-29, offline and the desktop window) | - | none |
| Modulation lanes: one destination each, bipolar % offset per step, CONTROLLER and AMOUNT per lane | p89-91 | Have | 8 lanes, -100..+100% per step, a controller with an amount (2026-10-27, offline) | - | low |
| Step-mod destination list (CATEGORY/DESTINATION) | p91 (see 15e) | Have | the mod matrix's whole destination list (voices and effects), the step-mod-only ones too (octave, noise EG, EG switches, LFO wave: 2026-10-29); STEP MOD records the footage, EG switch, noise EG and LFO wave controls as offsets (a list's move as its steps); a lane past destination 255 saves in a two-byte form, older sequences as before (2026-10-29, offline) | - | none |
| SYNC | off/on (p92) | Have | `seq_sync`: host tempo, and its song position while it plays (2026-10-27, offline) | - | none |
| CLOCK DIVISION (also divides the free rate) | 4, 2, 1, 1/2, 1/4, 1/8, 1/16, 1/32 (p92) | Have | `seq_div`, named 1/1 .. 1/128 (2026-10-27, offline) | - | none |
| CLOCK TIMING | Straight, Dotted, Triplet (p92) | Have | `seq_timing` (2026-10-27, offline) | - | none |
| LENGTH | 1 to 64 (p93) | Have | `seq_length` (2026-10-27, offline) | - | none |
| FOLLOW SWING | off/on (p93) | Have | `seq_follow_swing` follows the master swing `clock_swing` (synced) (2026-10-27, offline) | - | none |
| NOTE RESET (with Note Trig) | off/on (p93) | Have | `seq_note_reset` (2026-10-27, offline) | - | none |
| RESTART ON HOLD | off/on (p93) | Have | `seq_restart_hold` (HOLD keeps a note-trig sequence running) (2026-10-27, offline) | - | none |
| RESTART ON TRIG | off/on (p93) | Have | `seq_restart_trig` (2026-10-27, offline) | - | none |
| TRANSPOSE MODE | Start Key, Interval (from middle C) (p93) | Have | `seq_tr_mode` (2026-10-27, offline) | - | none |
| ONE SHOT | off/on (p94) | Have | `seq_one_shot` (PLAY goes off at the end) (2026-10-27, offline) | - | none |
| RECORDING MODE | Replace, Overdub, Focused (p94) | Have | `seq_rec_mode`; step recording stopped, real time running (gate as held) (2026-10-27, offline) | - | none |
| OUTPUT: which Synth(s) the sequence plays [MULTI] | Synth 1/2/3, All Synths (p94) | Have | `seq_output` per Synth: Own, Synth 1/2/3, 1+2, 1+3, 2+3, 1+2+3 (All); the targets' own sounds, transpose and octave, while on; key ranges ignored; notes end where they started; the SEQUENCER title strip and its MORE page (2026-10-30, offline and the desktop window) | - | none |
| Start several Synths' sequences together (Panel Focus multi-select) [MULTI] | p83 | Have | PLAY ALL (`seq_all`, the SETUP page): every Synth that is on with its sequencer ON starts (or stops) on the same sample (2026-10-28, offline and the desktop window) | - | none |
| CLEAR SEQUENCE, DUPLICATE X-Y [UI] | p94 | Have | the step editor's CLEAR (asks) and DUP (2026-10-27, offline) | - | none |
| LOAD / SAVE sequences with names, notes, sort by name/date [UI] | p95 | Have | `.oneseq` files from the step editor (the Windows file dialog sorts by name or date); SAVE asks for performance notes, kept in the file and shown on LOAD (2026-10-29, the desktop window) | - | none |

## 18. Effects (p96-111)

Code: `src/fx.h` (routing), `src/fx/synth_fx.h`, `src/fx/master_delay.h`, `src/fx/reverb.h`, `src/fx/mi_effects.*`,
`src/fx/airwindows.h`. All original designs or MIT-licensed ports.

### 18a. Effects architecture

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Effects can all be switched off for a pure dry path | p96 | Have | `modfx_on` and `mfx_on` Off; sends at 0 or mix 0 are exactly dry (tested) | - | none |
| SYNTH EFFECT per Synth: one chosen type, mono in, stereo out, MIX always first | p96 | Have | `modfx_type` (22 types: 17 after the manual's list, 5 open-source), `modfx_mix`; mono in (the voices' sum), the wet at the synth pan, the dry keeps each voice's place | - | none/voice; shared 1.5-73 us per block by type |
| Synth Effect ON button | on/off (p96) | Have | `modfx_on` | - | none |
| CTRL 1 and CTRL 2 knobs, re-assignable to any effect parameter | p96-97 | Have | `modfx_ctrl1`/`modfx_ctrl2`: +-100% of the target's range on top of its knob; target `modfx_c1_dst`/`modfx_c2_dst`, Auto = the type's two most audible controls, never MIX (2026-10-30; the manual's pre-assignment before, kept for older states: see 18b) | - | none |
| Effect presets per type: load, save, initialise to the type's defaults [UI] | p97-98, p101 | Missing | - | M | none |
| Up to 5 effects at once (3 synth + 2 master) | p96 | Have | Three synth effects (one per Synth) and the two master effects (2026-10-28, offline and the desktop window) | - | shared medium |
| MASTER EFFECTS: SEND 1 and SEND 2 per Synth into a master bus [MULTI] | p99 | Have | Each Synth's `mfx_send1` / `mfx_send2` into the shared master effects (their sum, `fx_master_multi`); returns added to the mix (2026-10-28, offline and the desktop window) | - | shared |
| Master Effects ON button | on/off (p99) | Have | `mfx_on` | - | none |
| MASTER EFFECT 1 / 2 TYPE: any type from the synth-effect list, plus the Eventide reverbs (synth effect presets usable as master effects and back) | p96, p101 | Have | `dly_type` / `rev_type`: 23 types in either slot (Plate, Room, Hall, Space, Shimmer, Blackhole, Stereo Delay, Ping Pong and the manual's 15 synth types; the 5 open-source synth types stay synth-only); each slot has its own controls (slot 1 the `dly_*` params, slot 2 the `rev_*`), MIX and GAIN; switching a type fades the old one out, clears its memory a block at a time and fades the new one in (2026-10-27, offline). Effect presets: package 12c. On the window: a TYPE button per slot and a MORE page with only the controls its type uses (2026-10-29, desktop) | - | none/voice; shared, by type |
| MASTER BUSS MODE | Dual Mono (two effects), Stereo (one effect) (p100) | Have | `mfx_buss`: Dual Mono (both effects, on SEND 1 and SEND 2) or Stereo (effect 1 alone on SEND 1, stereo in). ONE-ish keeps each voice's place in Dual Mono's inputs too (the instrument's mono buss loses it), so the earlier sounds are unchanged | - | shared |
| STEREO WIDTH at the end of the master chain | 0% to 100% to 200% (p100) | Have | `mfx_width` 0-200%, mid/side on the returns (the dry path stays exact) | - | none/voice; shared low |
| EFFECT 1 -> 2 SERIES (parallel to series blend) | 0% to 100% (p100) | Have | `mfx_series`: effect 1's return moves from the output into effect 2 (Dual Mono only) | - | none |
| Only one Eventide effect at a time [LIC] | p99 | Have | Not needed: the reverbs are original designs, any of them in either slot | - | - |
| SYNC per effect: time parameters become divisions | 1/64T..1/64., 1/32T.., 1/16T.., 1/8T.., 1/4T.., 1/2T.., 1, 2, 4 (21 values, p103) | Have | `modfx_sync`, `dly_sync`: the 21 divisions for delay times and LFO/vibrato rates (chorus/flanger times stay in ms) | - | none |
| Per-voice pan kept in the stereo master bus, lost in mono buses | p96, p99 | Have | Voices are stereo (pan, spread); the synth effect takes the mono sum with its wet at the synth pan (as the manual), the master effects take the panned voices (2026-10-27, offline) | - | low |

### 18b. Effect types (Synth Effects and Master Effects share the list, p103-109)

ONE-ish's synth effect offers every type below (Stereo Delay and Ping Pong since 2026-10-27, the vocoders since
2026-10-29), and either master effect any of them; master effect 1 is a Stereo Delay by default. Shared knobs: `modfx_mix`, `modfx_fb` (and `_r`), `modfx_time` (and `_r`, chorus/flanger ms, range per
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
| FEEDBACK LEFT, FEEDBACK RIGHT | 0% to 100% | Have | `dly_fbl`, `dly_fbr` (synth: `modfx_fbl`, `modfx_fbr`): each side's share of FEEDBACK, 100% by default | - | none/voice |
| MOD RATE LEFT/RIGHT, MOD DEPTH LEFT/RIGHT | 0% to 100% | Have | `dly_rate`, `dly_rate_r`, `dly_depth`, `dly_depth_r`: a sine swing of each side's read, up to +-5 ms | - | none/voice; shared low |
| TONE (lowpass) | 0% to 100% | Have | `dly_tone` | - | none/voice |
| GAIN | 0% to 100% | Have | `dly_gain` (each master slot's return level, 50% = unity), as the synth effect's | - | none/voice |
| SYNC | on/off | Have | `dly_sync` (both times) | - | none/voice |

**Ping Pong** (alternating stereo taps, p104): the master delay with `dly_pp` On

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| MIX (CTRL 1), FEEDBACK (CTRL 2) | 0% to 100% | Have | `mfx_send1`, `dly_fb` | - | none/voice |
| TIME LEFT, TIME RIGHT | 0.1 ms to 2000 ms | Have | `dly_time`, `dly_time_r` | - | none/voice |
| OFFSET between left and right | 0% to 100% | Have | `dly_offset`: the right side's repeats later by up to half the left time | - | none/voice |
| TONE | 0% to 100% | Have | `dly_tone` | - | none/voice |
| GAIN | 0% to 100% | Have | `dly_gain` | - | none/voice |
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
| TONE (lowpass on the mix) | 0% to 100% | Have | `modfx_tone` filters the mixed signal (all of it from mix 50% up, fading out below so mix 0 stays dry), 6/12-stage and Time Phaser | - | none/voice |
| NOISE | 0% to 100% | Have | `modfx_noise` | - | none/voice |
| GAIN | 0% to 100% | Have | `modfx_gain` | - | none/voice |
| SYNC | on/off | Have | `modfx_sync` | - | none/voice |

**Other modulation and character types**

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| 12 STAGE PHASER (p107): same parameters as the 6-stage | as 6-stage | Have | Synth type 12-Stage Phaser | - | none/voice; shared low (4.6 us) |
| TIME PHASER (four-stage phaser into a delay, p108): MIX (CTRL 1), PHASER FEEDBACK (CTRL 2), SWEEP, RATE, DEPTH, TONE, NOISE, DELAY FEEDBACK, TIME, GAIN, SYNC | 0-100%; rate 0.01-20 Hz; time 0.1-2000 ms | Have | Synth type Time Phaser: `modfx_dtime`, `modfx_dfb` for the delay | - | none/voice; shared low (4.4 us) |
| BIT CRUSHER (crossover filter, dual crushing, p108): MIX (CTRL 1), DEPTH (CTRL 2), RATE, GAIN | depth 2-24 bit; rate 128 Hz-48 kHz | Have | Synth type Bit Crusher: `modfx_bits` 2-24, `modfx_srate` 128 Hz-44.1 kHz; `modfx_xover` (Off, 50 Hz-5 kHz): two crushers on a second-order split, the lows losing only bits, the highs bits and sample rate | - | none/voice; shared low (1.5 us) |
| RESONATOR (three-filter bank, p108): MIX (CTRL 1), RESONANCE (CTRL 2), CUTOFF LOW/MID/HIGH, LEVEL LOW/MID/HIGH, FILTER TYPE, MOD RATE, MOD DEPTH, GAIN | cutoffs 10 Hz-22 kHz (defaults 500 Hz, 1 kHz, 5 kHz); type LP/BP/HP; mod rate 0.01-10 Hz | Have | Synth type Resonator: three state-variable filters, `modfx_cut1-3`, `modfx_lvl1-3`, `modfx_ftype`, `modfx_reso`, `modfx_rate`/`modfx_depth` sweep | - | none/voice; shared low (2.4 us) |
| 10 BAND VOCODER (p109): MIX (CTRL 1), SIBILANCE (CTRL 2), SENSITIVITY (mic gate), TONE, NOISE | 0-100% | Have | Synth type and master type 10 Band Vocoder (`src/fx/vocoder.h`, an original filter bank): the audio input (EXT's signal) is the modulator, the voices (or the send) the carrier; 10 bands 160 Hz-6.4 kHz, each a 4th-order band-pass with an envelope follower; MIX `modfx_mix`, SIBILANCE `modfx_sibil` (the modulator above 6 kHz, straight through), SENSITIVITY `modfx_sens` (a gate on the modulator, -20 to -80 dBFS), TONE `modfx_tone` (the bands above 200 Hz x 100^TONE fall away), NOISE `modfx_noise` (noise into the carrier); master slots `dly_*` / `rev_*` the same (2026-10-29, offline and the desktop DLL) | - | none/voice; shared medium (about 12 us a block on the PC) |
| 16 BAND VOCODER (p109): same parameters | 0-100% | Have | Type 16 Band Vocoder: 16 bands 110 Hz-7.5 kHz, the same controls (2026-10-29) | - | none/voice; shared medium (about 18-19 us a block on the PC) |

Beyond the Moog One (2026-10-27, offline): open-source synth effect types, MIT-licensed
(`third_party/THIRD_PARTY.md`). **Modal Resonator**: Mutable Instruments Rings excited by the voices (its six models,
`modfx_model`; structure, position, brightness on TONE, damping on DECAY; tuned to the newest note with `modfx_follow`,
or middle C, plus `modfx_pitch`). **Granular**: Mutable Instruments Clouds (`modfx_gmode` granular, stretch, looping
delay, spectral; `modfx_quality`; position, size, pitch, density, texture, freeze; spread, feedback and reverb on
SPREAD, FEEDBACK and ROOM). **Tape Color**, **Density**, **Console**: Airwindows ToTape6, Density and Console7
(channel into buss) ported to C (`modfx_drive`, `modfx_bump`, `modfx_lowcut`). PC cost per 128-frame block, shared:
Rings 5-42 us by model (modal the most), Clouds 12-73 us by mode (spectral works in bursts of up to about 360 us),
Tape Color 19-25, Density 10-15, Console 14-16. Their 14 params are on the synth effect's MORE page, each shown only
for the types that use it (2026-10-29, desktop).

**Grains** (2026-10-30, offline and the desktop DLL; an original design, `src/fx/grains.h`): a synth effect type and a
master slot type, apart from Granular: a 5.9 s recording with FREEZE holding all of it, POSITION, SIZE (10-500 ms),
DENSITY (1-150 a second), PITCH, SPRAY, PITCH RANDOM, PITCH QUANTISE (semitones, octaves, octaves and fifths), REVERSE,
SPREAD, ENVELOPE (TEXTURE), FEEDBACK, MIX; a fixed pool of 64 grains. PC cost per 128-frame block, shared: 17 us at its
defaults, 77 us at full density (PLAN.md item 33).

**CTRL 1 / CTRL 2 on Auto** (2026-10-30, the owner's request): no longer the manual's pre-assignments in the tables
above (which put MIX on CTRL 1 for most types, a second MIX knob): each type's two most audible controls, never MIX
(`src/fx/common.h` FX_AUTO_CTRL; the old table is FX_AUTO_CTRL_V1, kept for older states, which keep their old
targets). The panel shows the target's name under each knob.

### 18c. Eventide Master Effects (master bus only, one at a time) [LIC]

These are licensed algorithms. Only original reverbs in the same spirit can be built; none can match them. ONE-ish's
reverbs (Plate, Room, Hall, Space, Shimmer, Blackhole) run in either master slot (`dly_type` / `rev_type`) on that
slot's knobs (slot 2's below as `rev_*`, slot 1's the same as `dly_*`); decay is a time (`rev_decay`: plate and room
0.25-10 s, hall and shimmer 0.1-20 s at size 100%, scaled by size); `rev_freeze` holds: Freeze, Infinite (no decay,
input open, a soft limiter in the loops) or Pitch Freeze (the shimmer's). Space (2026-10-27, offline) is Airwindows Galactic ported to C (MIT): DECAY sets how much of
the space each new sound replaces (100% or FREEZE: it holds), TONE its brightness, MOD LEVEL its detune, SIZE its
bigness; pre-delay, damping, diffusion and early/late don't apply to it (9 us per block on the PC).

**Eventide Plate** (p110) against ONE-ish's plate (Dattorro figure-of-eight tank)

| Parameter | Range | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| (the reverb itself) [LIC] | - | Have | Dattorro plate, `rev_type` Plate on `mfx_send2` | - | none/voice; shared medium (8.4 us) |
| DISTANCE (early-reflection delay) | 0 to 100 | Have | `rev_distance`: early reflections off the pre-delay line, spread out up to 3x as it rises (0: none, the plate as before) | - | none/voice |
| DIFFUSION (early network feedback) | 0 to 100 | Have | `rev_diff` scales the input diffusers (100% = the paper's values) | - | none/voice |
| MOD LEVEL | 0 to 100 | Have | `rev_mod` (50% = the paper's excursion) | - | none/voice |
| TONE (input lowpass) | -100 to 100 | Have | `rev_tone` 0-100% (open at 100) | - | none/voice |
| HIGH DAMP | 100 to 0 | Have | `rev_damp` | - | none/voice |
| LOW DAMP | 100 to 0 | Have | `rev_lowdamp`: a low cut (below about 250 Hz) in the loop | - | none/voice |
| PRE DELAY | 0 ms to 1500 ms | Have | `rev_pre` 0-1500 ms | - | none/voice |
| SIZE | 0 to 100 | Have | `rev_size` 50-200%: tank lengths and the decay time | - | none/voice |
| DECAY | 0.25 s to 10 s, Infinite, Freeze | Have | `rev_decay` 0.25-10 s; `rev_freeze` Freeze (holds, input shut) or Infinite (no decay, input open) | - | none/voice |

**Other Eventide reverbs**

| Effect type and parameters | Ranges | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| EVENTIDE ROOM [LIC] (p109): early/late balance (unnamed first row, 20 detents from all-early to all-late), DIFFUSION, MOD LEVEL, HIGH FREQ (post-crossover lowpass), HIGH LEVEL, LOW LEVEL, PRE DELAY, SIZE, DECAY | 0-100; high freq 1-5 kHz; high level -100 to 100; low level 0-1; pre-delay 0-500 ms | Have | `rev_type` Room (original): early reflections and a 4-line feedback delay network; `rev_early` on the manual's 21 detents, diffusion, `rev_mod` (two lines swing), `rev_hifreq` 1-5 kHz with `rev_hlevel` / `rev_llevel` above and below it, pre-delay (to 2 s), size, decay (a time, not 0-100) | - | none/voice; shared medium (6.2 us) |
| EVENTIDE HALL [LIC] (p110): LOW DECAY, HIGH DECAY, MOD LEVEL, MID LEVEL, HIGH LEVEL, LOW LEVEL, PRE DELAY, SIZE, DECAY | low/high decay 0.1-5.0x, Infinite, Freeze; levels -100 to 100; pre-delay 0-1500 ms; decay 0-20 s, Infinite, Freeze | Have | `rev_type` Hall (original): early reflections, four diffusers, an 8-line network with four modulated lines; `rev_ldecay` / `rev_hdecay` x0.1-5 below 250 Hz and above HIGH FREQ, `rev_llevel` / `rev_mlevel` / `rev_hlevel`, decay 0.1-20 s, Infinite and Freeze | - | none/voice; shared medium-high (17.5 us) |
| EVENTIDE SHIMMER [LIC] (p111): A PITCH, B PITCH, PITCH DECAY (with Pitch Freeze, Pitch+Reverb Freeze), MID DECAY, LOW DECAY, HIGH DECAY, DELAY, SIZE, DECAY | pitches about +-24 semitones in cents (printed "-2410C - +2410"); 0-100; delay 50-1000 ms | Have | `rev_type` Shimmer (original): the hall's network fed by a 0.1-0.4 s pitch loop through two delay-line pitch shifters at `rev_pitch` / `rev_pitch_b` (+-2400 cents), climbing again each pass by `rev_pdecay`; `rev_freeze` Pitch Freeze (the loop holds, the reverb carries on) or Freeze (both); `rev_ldecay` / `rev_hdecay`, pre-delay, size, decay 0.1-20 s (MID DECAY is DECAY) | - | none/voice; shared high (see the bench) |
| EVENTIDE BLACKHOLE [LIC] (p111): MOD DEPTH, MOD RATE, FEEDBACK, RESONANCE, HIGH LEVEL, LOW LEVEL, PRE DELAY, SIZE, GRAVITY | 0-100; feedback 0-100, Infinite, Freeze; levels -100 to 100; pre-delay 0-2000 ms; gravity -100 to 100 | Have | `rev_type` Blackhole (original): four long modulated diffusers into an 8-line network of 0.06-0.57 s lines, all swinging (`rev_mod`, `rev_rate`); `rev_fb` a 1-100 s decay with Infinite / Freeze; `rev_gravity` below 0 swells in, above 0 pulls the tail down; `rev_reso` a peak at its 700 Hz crossover, `rev_llevel` / `rev_hlevel`; pre-delay to 2 s, size | - | none/voice; shared high (see the bench) |

## 19. Left Hand Controller (p17-18, p112-117)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| OCTAVE SHIFT | -2 to +2 (p112) | Have | `octave_shift` (an engine param; follows live, keyboard tracking too) (2026-10-27, offline) | - | none |
| MIDI panic (hold both octave buttons) | p112 | Have | The engine handles CC 120/123 (all off) and 121 (reset controllers); holding a window OCTAVE button 2 s sends them on every channel (2026-10-28, the desktop window) | - | - |
| GLIDE on/off | p112 | Have | `glide`; each voice glides from its own last pitch (a fresh voice doesn't), at the control rate; keyboard tracking and the Keyboard source follow it (2026-10-27, offline) | - | none |
| GLIDE RATE | 0 ms to 10.0 s (p112) | Have | `glide_time` 0 ms, then 1 ms to 10 s on a log knob (2026-10-27, offline) | - | none |
| GLIDE TYPE | LCR (constant rate), LCT (constant time), EXP (exponential) (p116) | Have | `glide_type`: LCR takes the glide time per octave, LCT the glide time for any interval, EXP 95% of the way in the glide time (2026-10-27, offline) | - | none |
| GATED GLIDE: glide pauses while no key is down | off/on (p116) | Have | `glide_gated`: a voice's glide holds while its key is up (the pedal counts as down) (2026-10-27, offline) | - | none |
| LEGATO GLIDE: glide only on legato notes (mono only) | off/on (p116) | Have | `glide_legato`; in poly too (a note pressed while another key is held) (2026-10-27, offline) | - | none |
| GLISSANDO: glide in semitone steps | off/on (p117) | Have | `glissando` (2026-10-27, offline) | - | none |
| PITCH wheel, spring-loaded | p113 | Have | MIDI pitch bend (14-bit); the window's wheel springs back | - | - |
| PITCH BEND UP (SHIFT = finer than semitones) | -24 to +24 st (p115) | Have | `bend_up` -24..+24 st, fine values allowed; the old `bend_range` is retired (an older project's value sets both directions) (2026-10-27, offline) | - | none |
| PITCH BEND DOWN | -24 to +24 st (p115) | Have | `bend_down` -24..+24 st (default -2: down a whole tone) (2026-10-27, offline) | - | none |
| MOD wheel, not spring-loaded | p113 | Have | CC 1; the window's wheel stays where it is left | - | - |
| X/Y PAD: X, Y, pressure, gate; X/Y HOLD saves the held value | p113-114 | Partial | The window has a pad: X as CC 2, Y as CC 3 (and channel pressure), touch as CC 18: the matrix sources Pad X, Pad Y, Pad Gate (2026-10-27, offline); X/Y HOLD `pad_hold` (default On, as before; Off: let go, the pad returns to its rest, the window's dot too) (2026-10-29, offline and the desktop window); no pressure (a mouse has none) | S | none |
| PAD POLARITY | Unipolar, Bipolar (p115) | Have | `pad_polarity`: Unipolar (0..1, as before) or Bipolar (-1..1, the middle 0) for Pad X and Y (2026-10-29, offline) | - | none |
| VELOCITY CURVE, per Synth | Soft, Medium, Hard, Compress, Expand (p116) | Have | `vel_curve`: Soft sqrt(v), Medium v (default, as before), Hard v^2, Compress and Expand flatten or steepen around the middle; all keep both ends (2026-10-27, offline) | - | none |
| LOAD SAVED POS. (mod wheel, pad position with the Synth) | Off, Mod Wheel, Pad, Both (p117) | Have | `load_pos` per Synth: a saved sound holds the wheel's and pad's positions (`wheel_pos`, `pad_x_pos`, `pad_y_pos`, taken as the state is saved) and loading it puts them back (CTRL page) (2026-10-29, offline and the desktop window) | - | none |
| SEMITONE TRANSP. | -12 to Off to +12 (p117) | Have | `transpose` -12..+12 st (2026-10-27, offline) | - | none |
| KEY TRACK CENTER for all key tracking | C0 to G10, default middle C (p117) | Have | `key_center` MIDI note 0-127, shown as "C4 (60)": both filters' tracking, the Keyboard matrix source and oscillator pitch tracking (2026-10-27, offline) | - | none |
| HEADPHONES jacks [HW] | p114 | Missing | - | - | none |

## 20. Browser, presets and timbres (p118-121)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Preset store (tens of thousands), browse and load | p118-119, p140 | Have | 35 factory programs plus any number of user patch files (`Documents\ONE-ish\Patches`, `docs/PATCHES.md`), browsed together on the PATCHES page and with PREV/NEXT | - | none |
| Tags: TYPE (Single, Layer, Split, Multi), CATEGORY, MOOD, GROUP; filter the browser by tags [UI] | p118-120 | Have | Set in the save box (TYPE starts from the Synths' setup) or with TAG PATCH; the TAGS page filters the PATCHES list and PREV/NEXT, one tag per kind; the factory presets carry their own tags; patch format version 2 (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| Notes saved with a preset; SHOW NOTES on the HOME screen [UI] | p118, p120 | Have | Notes in the save box or TAGS > NOTES, saved in the patch (`notes=`); SHOW NOTES shows them in the help area; the home line says NOTES (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| Load a Timbre from any preset into any Synth [MULTI] | p119 | Have | PATCHES page INTO S1/2/3: a factory preset or user patch loads into the focused Synth only (its setup kept), the engine's `synthload` (2026-10-28, offline and the desktop window) | - | none |
| SAVE PRESET / SAVE PRESET AS, unique names, overwrite prompt [UI] | p120 | Have | The PATCHES page: SAVE, SAVE AS (asks before replacing a same-named patch; names compare case-insensitively), RENAME, DELETE (2026-10-27, offline) | - | none |
| SAVE TIMBRE to a new single-Timbre preset [UI][MULTI] | p121 | Have | PATCHES page SAVE S1/2/3: the focused Synth's sound as a user patch (2026-10-28, offline and the desktop window) | - | none |
| A preset restores everything (sequences, matrices, arps, effects, held wheel and pad) | p119 | Have | The state restores every ONE-ish parameter and the sequences (2026-10-27); the held mod wheel and pad with LOAD SAVED POS (2026-10-29, offline) | - | none |

## 21. Editing and saving (p122-123)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| Edited indicator (SAVE lit dimly) [UI] | p122 | Have | `*` after the patch name and SAVE lit while the sound differs from the loaded or saved one (`patches.c`; also restored with a project) | - | none |
| COMPARE edited vs saved (A/B); loading another preset clears both buffers | p122 | Have | COMPARE on the console plays the saved version, again the edit; loading another sound ends it (the edit autosaved); all three Synths and the sequences (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| SNAPSHOT: timestamped micro-saves with notes, browse, load, delete [UI] | p122-123 | Have | The SNAPSHOTS page: TAKE (with a note), LOAD, NOTES, DELETE, SAVE OVER (its patch), SAVE NEW; files in `Snapshots\` beside the patches (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
| AUTOSAVE snapshot of unsaved edits on preset change [UI] | p123 | Have | An `AUTO` snapshot of an edited sound whenever another sound loads (list, PREV/NEXT, INTO, sets, MIDI, snapshots); the newest 32 kept (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |

## 22. Settings (p124-139)

### 22a. Global hardware settings (p125-127)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| SETTINGS page: GLOBAL, LIBRARY and TUNING tabs, kept apart from presets [UI] | p124 | Have | The window's SETTINGS page (`desktop/settings.c`, `docs/SETTINGS.md`): GLOBAL, LIBRARY and TUNING tabs; its settings live in `Settings.txt` beside the Patches folder, never in a sound or project. TUNING shows master tune and fine tune, which stay part of the sound as before (2026-10-29, the desktop window) | - | none |
| KNOB MODE [HW] | Absolute, Pass Through, Relative (p125) | Missing | Mouse knobs always edit relatively | - | none |
| CHORD MEMORY learn mode | Silent Learn, Audible Learn (p125) | Have | `learn_mode` (shared; the SETUP page) (2026-10-28, offline) | - | none |
| LCD BRIGHTNESS, PANEL LED BRIGHTNESS [HW] | 0% to 100% (p125) | Missing | - | - | none |
| SHOW BANK PREVIEW, PRESET AUTOLOAD [UI] | off/on (p125) | Have | PRESET AUTOLOAD on the SETTINGS page: on, the PRESET knob and PREV/NEXT (tag filters too) load as they step (as before); off, they point at a patch ("> F 004 ... ?") and a click on the knob or the name loads it (2026-10-29, the desktop window; `docs/SETTINGS.md`); SHOW BANK PREVIEW on the SETTINGS page: on, the SET page's bank letters show their eight patches (as before, `docs/PATCHES.md`); off, only the numbers (2026-10-29, the desktop window) | - | none |
| DUPLICATE NAMES ON IMPORT / EXPORT [UI] | Ignore, Overwrite, Auto Rename, Manual Rename (p126) | Partial | IMPORT asks before overwriting a same-named patch; no auto-rename or ignore choice | S | none |
| STEP EDIT AUTO-SCROLL [UI] | off/on (p126) | Have | AUTO on the step editor's title line (lit by default): the page shown follows the step playing; a window setting for now, not saved (2026-10-29, the desktop window) | - | none |
| NOISE REDUCTION between notes [ANALOG][HW] | -24 dB to 0 dB, default -12 dB (p126) | Missing | Not needed: no analog hiss (ONE-ish adds only a -100 dB floor) | - | none |
| SET DATE/TIME, formats [HW] | p127 | Missing | The computer's clock | - | none |
| FAN MODE [HW] | Normal, Low (p127) | Missing | - | - | none |

### 22b. MIDI (p128-130)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| RECEIVE MIDI CLOCK | off/on (p128) | Have | SETTINGS page: on (default), the host's tempo and song position; off, both are ignored and the Synths run free at the page's own TEMPO (10-280 BPM) (2026-10-29, offline and the desktop window) | - | none |
| RECEIVE MIDI START/STOP | off/on (p128) | Have | `seq_host_ss` per Synth (default Off): the host's start and stop start and stop the sequence (PLAY follows, the host is told); synced LFOs restart on the start (2026-10-29, offline) | - | none |
| MIDI I/O LOAD FROM PRESET [UI] | off/on (p129) | Have | SETTINGS page: on (default, as before), a loaded preset or patch brings its MIDI channels; off, `midi_ch` and each Synth's `syn_ch` stay as set (a project still restores everything) (2026-10-29, the desktop window) | - | none |
| MIDI IN PORTS [HW] | DIN, USB, DIN & USB (p129) | Missing | Host | - | none |
| MIDI IN CHANNEL ALL (one channel for the whole preset) | Off, 1-16 (p129) | Have | `midi_ch`: Omni (the default: every channel, as before) or 1-16 for every Synth; the window plays on it (2026-10-28, offline) | - | none |
| MIDI IN ROUTE PER SYNTH: a channel for each Synth [MULTI] | Off, 1-16 each (p129) | Have | `midi_ch` Per Synth: each Synth's `syn_ch` (Omni or 1-16; default 1, 2, 3); a key's release goes where the key went (2026-10-28, offline and the desktop window) | - | none |
| MIDI OUT PORTS [HW] | DIN, USB, DIN & USB (p129) | Missing | Host | - | none |
| MIDI OUT ROUTE and channels: the Synths' notes (keys, arp, sequencer) out | All or Per Synth, Off, 1-16 (p129-130) | Missing | The plugin sends no MIDI; a desktop VST2 can send the arp's and sequencer's notes to the host (MPC ignores plugin MIDI out) | M | none |

### 22c. CV inputs and outputs, pedals (p131-132)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| CV IN 1/2, EXP IN 1/2: FUNCTION, SYNTHS, VOLTAGE MIN/MAX; LOAD FROM PRESET [HW] | None, Next/Previous Preset, Next/Previous Set; -5 V to +5 V (p131-132) | Missing | - | - | none |
| SUSTAIN IN FUNCTION | Off, Sustain Pedal, Sostenuto Pedal (p131) | Have | Both at once, as MIDI sends them: sustain CC 64, sostenuto CC 66 (holds the notes sounding as it goes down) (2026-10-27, offline) | - | none |
| SUSTAIN IN SYNTH target [MULTI] | Synth 1/2/3, pairs, All (p131) | Missing | - | S (with parts) | none |
| SUSTAIN SWITCH TYPE [HW] | Open, Closed (p131) | Missing | Host | - | none |
| CV OUT 1-4: SOURCE, OUT VOLTAGE MIN/MAX; LOAD FROM PRESET [HW] | Gate, Pitch Wheel, Mod Wheel, Aftertouch, Pad X/Y/Pressure/Gate; -5 V to +5 V (p132) | Missing | - | - | none |

### 22d. Utilities, library, tuning (p133-139)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| LIGHT SHOW, UPDATE FIRMWARE, IP ADDRESS [HW] | p133-134 | Missing | - | - | none |
| RESTORE DEFAULT SETTINGS [UI] | p133 | Have | SETTINGS > LIBRARY, after a question; patches and sounds untouched (2026-10-29, the desktop window) | - | none |
| Firmware / UI version, build date and info [UI] | p134 | Have | SETTINGS > LIBRARY: version, params, build date and time, and where the patches and settings file live (with buttons that open the folders) (2026-10-29, the desktop window) | - | none |
| USER SPACES: switch, create (factory or empty), rename, copy [UI] | p134-135 | Missing | One patch folder (`ONEISH_PATCH_DIR` can point elsewhere) | M | none |
| EXPORT / IMPORT to USB: presets, sequences, sets, effect presets, matrix presets, globals, all [UI] | p136-137 | Partial | One patch at a time to or from any folder (the PATCHES page); nothing else exists to export yet | M | none |
| Edit CATEGORY, MOOD, GROUP tag lists [UI] | p137-138 | Have | The TAGS page: ADD, RENAME, DELETE (the patches that carry a tag follow); `tags.oneishtags` beside the patches (2026-10-29, offline and the desktop window; `docs/PATCHES.md`) | - | none |
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
| Pedal inputs configurable through the Modulation Matrix | 2, 15b | - |

## 24. Appendices A and B (p143-158)

| Moog One feature | Range / options (manual) | Status | ONE-ish today | Effort | CPU per voice |
|---|---|---|---|---|---|
| USB computer keyboard for naming and Panel Focus shortcuts (SHIFT/CTRL + F5-F7) [UI][MULTI] | p143 | Have | The computer keyboard types names in the window; Shift+F5/F6/F7 pick the Synth in focus (Ctrl too: add it) (2026-10-28, offline and the desktop window); beyond the manual (2026-10-29, the desktop window): it plays notes (A S D F G H J K L ; ' white keys from C4, W E T Y U O P black, Z / X octave), keys 1-3 pick a Synth, R / M randomise / mutate (Shift: all three) | - | none |
| MIDI CONTROL OUTPUT: panel moves sent as CC, MIDI MAP or NRPN; ALWAYS SEND NRPN NUM [HW] | p144 | Missing | The host records knob moves as automation (`audioMasterAutomate`) | - | none |
| LEARN MIDI CC: map a CC to any panel control, 7- or 14-bit, CLEAR / ADD MAPPING | p145-146 | Have | SETTINGS > MIDI: LEARN, click any control (of the Synth in focus), move a CC; 14-bit found by itself (the CC's LSB, CC + 32, following it) and switchable to 7; a CC learned again or a control learned again replaces its old mapping; CANCEL. The map is kept in the settings file (the manual keeps it with the global settings, not per sound) and reaches every instance's engine as a fixed 128-entry table; a mapped CC moves its param as host automation would, and the host is told (2026-10-29, offline and the desktop window, `docs/SETTINGS.md`) | - | none |
| EDIT MIDI MAPPING list, reserved CCs [UI] | p147-148 | Have | SETTINGS > MIDI lists every mapping (CC, control, 7-BIT / 14-BIT, DELETE, CLEAR ALL) and the reserved CCs (0-3, 6, 16-18, 32, 38, 66, 96-101, 120-127 and the pedals'); a reserved CC is refused while learning, and a pedal moved onto a learned CC takes it (2026-10-29, the desktop window) | - | none |
| NRPN input for every parameter per Synth (table p149-158: LFO wave and free/synced rate; oscillator octave, frequency, beat, saw, triangle, PW, mix, wave, sync; pitch mod; ring mod source; wave mod; FM; noise; polyphony; mixer; filters; cutoff mod; three envelopes with free and synced times, multi trig, sync, loop, latch; VCA; output) | p149-158 | Have | Every number of the table for all three Synths (Synth 2 = Synth 1 + 3840, Synth 3 + 7680; MASTER VOLUME the shared master level; HEADPHONES not a plugin's), on any channel, CC 99/98 then 6/38, 14 bits: options in the manual's bands mapped onto ONE-ish's lists (LFO TRI/PULSE/SAW/S&H, SVF LP/HP/BP/NOTCH ...), free times and rates only while unsynced and synced ones only while synced, MONO as 1 voice / back to the default. The oscillators' SAW and TRIANGLE both set ONE-ish's one wave angle. The wrapper's own NRPN (number = param index) is off for ONE-ish (`"nrpn": false`) (2026-10-29, offline and the desktop DLL) | - | none |
| 14-bit parameter resolution over MIDI | p145 | Have | 14-bit learned CCs and NRPN data set params at 1/16383 of their range (2026-10-29, offline) | - | none |

---

## Beyond the manual: what a desktop VST2 is expected to have

Not Moog One features (this manual edition has no microtuning tables, aftertouch curves or MPE), so these rows are not
in the counts. Listed because the owner wants the desktop product finished, and some decide whether it works at all
outside Carla's fixed settings.

| Expectation | ONE-ish today | Effort | Note |
|---|---|---|---|
| Plays in tune at any host sample rate (44.1, 48, 88.2, 96 kHz) | Have (2026-10-27, desktop, offline): the engine still runs at 44.1 kHz and `desktop/hostrate.c` converts it to the host's rate (`desktop/resample.c`, a 72-tap windowed sinc: flat to 20 kHz, images 103 dB down), reporting the converter's latency (40 samples at 48 kHz, 79 at 96 kHz). A4 within 0.01 cent at 44.1/48/96 kHz, synced arp steps on the host's timeline (`desktop/rate_test.c`) | - | Wrapper and MPC unchanged. CPU about 40 us per 512-frame block for the converter (0.4% of real time at 48 kHz) |
| Full-resolution output | Have (2026-10-27, desktop, offline): float samples from the engine (`one_ish_render_float`, the int16 path's samples before its last step), through a desktop-only engine table the wrapper is compiled against; 44.1 kHz output matches the old 16-bit output sample for sample when quantised | - | Shared wrapper not changed (only renamed symbols at compile time, desktop build) |
| Undo / redo of any edit in the window | Have (2026-10-29, offline and the desktop window; `docs/PATCHES.md`): every edit from the window (knobs, buttons, lists, matrix, SETUP, step edits, randomiser, loads) is a step of the whole state; a drag or a wheel burst is one; 100 steps; Ctrl+Z / Ctrl+Y and the SNAPSHOTS page's UNDO / REDO | - | Host automation is not a step |
| Microtuning: Scala `.scl` / `.kbm` files | Have (2026-10-29, offline and the desktop window): SETTINGS > TUNING loads a scale and a keyboard map into the Synth in focus (read on the window's thread, `desktop/scala.c`); kept per Synth in the sound's state (`tun_scl`, `tun_kbm`; a 12-note just scale with a map about 220 bytes), in patches and with SAVE / INTO SYNTH; master tune and fine tune still apply; a 12-TET scale gives exactly the usual samples (`src/tuning.h`, `test/tuning_check.c`) | - | A table lookup per control step: +0.12 us a voice on the PC (8 voices 63.60 against 62.63 us per block); no scale, no change at all |
| The 14 open-source effect params on the window | Missing (generic host editor only) | S | `desktop/gen_layout.py` |
| MIDI learn in the plugin | Have (2026-10-29, the SETTINGS page's MIDI tab) | - | Same as the LEARN MIDI CC row (24) |
| Poly aftertouch / MPE | Not read (channel pressure only, as on the Moog One) | M | Not a Moog One feature; owner's call |

---

## Summary counts per module

Counted from the tables above (sections 0-22 and 24; section 23 and the "Beyond the manual" table excluded). "Missing
[HW]" and "Missing [UI]" count Missing rows by tag: [HW] rows are not build candidates for the desktop plugin, [UI]
rows are (the window can offer them). Rows tagged both [UI] and [MULTI] count as [UI].

| Module | Have | Partial | Missing | Missing [HW] | Missing [UI] |
|---|---|---|---|---|---|
| 0. Architecture and overview (p8-10) | 9 | 1 | 0 | 0 | 0 |
| 1. Front panel, centre console, keyboard (p10-18) | 12 | 3 | 1 | 1 | 0 |
| 2. Rear panel (p19-21) | 1 | 2 | 3 | 3 | 0 |
| 3. Oscillators (p22-26) | 20 | 0 | 0 | 0 | 0 |
| 4. Oscillator common: pitch mod, ring mod, wave mod, FM (p27-28) | 8 | 0 | 0 | 0 | 0 |
| 5. Noise (p29-30) | 9 | 0 | 0 | 0 | 0 |
| 6. Mixer (p31-32) | 8 | 0 | 1 | 1 | 0 |
| 7. Filters (p33-39) | 23 | 1 | 0 | 0 | 0 |
| 8. Envelopes (p40-45) | 21 | 0 | 0 | 0 | 0 |
| 9. LFOs (p46-49) | 23 | 0 | 0 | 0 | 0 |
| 10. VCA (p50-52) | 5 | 0 | 0 | 0 | 0 |
| 11. Output (p53-56) | 1 | 1 | 4 | 4 | 0 |
| 12. Performance Sets and text entry (p57-60) | 5 | 0 | 0 | 0 | 0 |
| 13. Master Clock (p60-62) | 5 | 0 | 3 | 3 | 0 |
| 14. Voice allocation: Hold, Chord, Polyphony (p63-66) | 12 | 0 | 0 | 0 | 0 |
| 15. Modulation (p67-77) | 80 | 3 | 5 | 2 | 2 |
| 16. Arpeggiator (p78-81) | 17 | 0 | 0 | 0 | 0 |
| 17. Sequencer (p82-95) | 33 | 0 | 0 | 0 | 0 |
| 18. Effects (p96-111) | 87 | 0 | 1 | 0 | 1 |
| 19. Left Hand Controller (p17-18, p112-117) | 17 | 1 | 1 | 1 | 0 |
| 20. Browser, presets and timbres (p118-121) | 7 | 0 | 0 | 0 | 0 |
| 21. Editing and saving (p122-123) | 4 | 0 | 0 | 0 | 0 |
| 22. Settings (p124-139) | 15 | 2 | 14 | 11 | 1 |
| 24. Appendices A and B (p143-158) | 5 | 0 | 1 | 1 | 0 |
| **Total** | **427** | **14** | **34** | **27** | **4** |

Rows are features or parameters, so the totals weigh a 33-row sequencer the same as 33 oscillator parameters; read
them per module, not as one score.

## The first build order (done)

The original audit (2026-10-27, commit 1706085) ordered the work in four tiers; all of it is built (offline and in the
desktop DLL): Tier 1 the sound (dual-wave oscillators, linear FM, ring mod, glide, envelope extras, the SVF pair, link
cutoff, filter FM, unison and pitch variance); Tier 2 modulation and playability (LFOs to four with every option but
the High range, the 16-slot matrix with transforms, noise colours and EG, voice and keyboard options, stereo voices,
analog character); Tier 3 effects (the synth effect slot with every type but the vocoders, master delay and reverb on
two sends); Tier 4 the arpeggiator and chord memory. Shimmer and blackhole reverbs, the vocoders, the sequencer and
multitimbral parts were left; the sequencer was built since (section 17, `docs/SEQUENCER.md`). What remains is in the
next section.

### What is likely too CPU-heavy for the MPC

Budget: about 2.9 ms per 128-sample block on Gen1, shared with the whole project; ONE-ish's per-voice cost is not yet
measured on the device (`tools/bench.sh`), so every "high" item below must be benchmarked before it ships. The desktop
has far more headroom; these limits decide the MPC build's defaults, not the desktop's.

- **16-voice polyphony** and **large unison stacks**: each voice is a full voice; stay at the benched voice count and
  let unison spend it.
- **Audio-rate filter FM** on both filters: about +35% a voice on the PC.
- **Oversampling** (HQ MODE, `an_hq`, 2026-10-30): on the PC 2x costs 1.8x a voice (8.5 to 14.7 us per voice per
  block), 4x 2.9x (24.5 us); with the non-linear ladder 10.5 to 19.5 / 34 us. Off by default; estimated on the MPC as
  1.8 / 3 voices' worth per HQ voice.
- **Per-stage non-linear ladder** (`an_ladder`): +21% a voice on the PC.
- **The MACRO channel** (Plaits): adds 0.5-2x a voice's cost to each voice; the **Rings / Clouds** effect types:
  5-73 us a block, shared.
- **Shimmer, blackhole and long multiband halls**: shared but high; one at a time at most.
- **16-band vocoder**: shared medium-high.
- **Three multitimbral parts each with its own synth effect**: three effect chains plus the voices.

## Remaining work for the desktop version (2026-10-27)

Everything above that is Missing or Partial and not hardware-only: 67 Missing rows (2 of them the sequencer's [MULTI]
rows) and 57 Partial rows (2 of those are [HW] and 3 are [ANALOG] approximations), plus the desktop expectations in
"Beyond the manual" (2026-10-28, after packages 2, 8 in part, 9, 10 and 13: 47 Missing rows that are not [HW] and
42 Partial; 2026-10-29, after packages 7 and 11: 42 and 41; after packages 8, 14a and 16: 34 and 35; then packages 3, 4 and 2's leftovers: 12 and 17; solo: 12 and 16). Grouped below into work packages that can be built and tested one at a time (desktop first, as
CLAUDE.md describes: engine change, `test/run.sh`, `desktop/build.sh`, `load_test.exe`, `host_test.exe`, install, play
in Carla). Each package gives its effort (S/M/L as defined at the top), its CPU cost, what must come first, and the
files it touches, so packages with no shared files can run in parallel. The rows of the sections each package names
are its checklist.

**Shared hot spots.** `src/engine.c` (one big file), `params.json` (every new param, then `GEN_VST_RELOCK=1` and the
presets regenerated) and `src/modmatrix.h` (the source and destination lists: new entries only at the end, so saved
slots keep their meaning) are touched by most engine packages: run those one after another, or agree beforehand the
order in which their new params and list entries land. `wrapper/vst2_wrap.c` is shared with every MPC port: change it
only behind a desktop-only switch. The window packages (`desktop/*`) and the effects packages (`src/fx/*`) can run
beside the engine work.

### Packages, in the recommended order

**1. Desktop correctness: any sample rate, full-resolution output** (beyond the manual). Effort L. CPU: none at
44.1 kHz; at 96 kHz every voice and effect costs about twice as much per second (the desktop has room).
- Done differently, 2026-10-27 (desktop, offline): rate conversion at the plugin edge instead of a run-time `SR`, so no
  DSP file changed. `desktop/hostrate.c` (see its header) renders the 44.1 kHz engine through the unchanged wrapper one
  128-frame block at a time and converts it to the host's rate (`desktop/resample.c`), with float output, the latency
  reported, host MIDI placed at the engine block nearest its time and the song position moved to each engine block.
  Tests: `desktop/resample_test.c` (quality, delay, CPU) and `desktop/rate_test.c` in `load_test.exe`. The plan below
  stays as the alternative if the engine should ever run at the host's rate itself (twice the engine's CPU at 96 kHz).
- Make the sample rate a run-time value (today `SR` is a constant in the engine and the effects, and Plaits, Rings and
  Clouds carry patched 44.1 kHz constants); take it from `effSetSampleRate` on the desktop; keep 44.1 kHz on the MPC.
- Float output instead of the wrapper's 16-bit blocks, and report any block latency to the host.
- Depends on: nothing. Do it first or not at all (owner's decision: Carla is set to 44.1 kHz), because it touches
  every DSP file and would conflict with any package running at the same time.
- Files: `src/engine.c`, `src/fx/*`, `src/macro.cc`, `third_party/patched/*`, `wrapper/vst2_wrap.c` (desktop switch).

**2. Finish and check the sequencer** (section 17). Effort L. CPU: none per voice. Built 2026-10-27 (offline and the
  desktop window, `docs/SEQUENCER.md`): 27 of its 33 rows Have, with the master swing `clock_swing` and FOLLOW SWING,
  "what can sync" complete (13) and chord memory after the sequencer (14). Left (S each): an RTZ button, editing a
  step's notes together and a step delete button, performance notes with a saved sequence, step edit auto-scroll
  (22a), the arp following the master swing (16), the host transport's start and stop starting and stopping the
  sequence (22b RECEIVE START/STOP). Done 2026-10-29 (offline and the desktop window; `test/seqx_check.c`, load_test):
  all of these (RTZ and AUTO on the step editor's title line, ALL and DEL STEP, notes in .oneseq files,
  `arp_follow_swing`, `seq_host_ss`); 32 of the 33 rows Have, OUTPUT [MULTI] left.
- Depends on: package 3 for the step-mod destinations the manual lists (oscillator octave, the EG switches, LFO
  waveform). OUTPUT to other Synths and starting several Synths together wait for package 13.
- Files: `src/stepseq.h`, `src/stepseq_run.h`, `desktop/seq_page.c`, `params.json`.

**3. Modulation engine extras** (sections 5, 15, 19, 20). Effort M. CPU: low (control rate).
- Done 2026-10-29 (offline and the desktop window; `test/modx_check.c`): the 22 destinations below (275 in all, after Ext Level; per
  voice; switches and lists move in whole steps), `modN_on` for every slot (the MATRIX page: click a row's number),
  `pad_polarity`, `pad_hold`, `load_pos` with `wheel_pos` / `pad_x_pos` / `pad_y_pos` (the CTRL page), STEP MOD on the
  new destinations; sequences with a lane past destination 255 save in a two-byte form (older ones as before). Old
  sounds give the same samples (`test/golden_check.c`); the CPU is unchanged (PLAN.md). Left: PAD PRESSURE (a mouse has
  none), the matrix page's other package 6 items.
- New matrix destinations: oscillator OCTAVE (x3), noise EG ATTACK / SUSTAIN / RELEASE, the EG switches MULTI TRIG /
  SYNC / LOOP / LATCH (x3), LFO WAVEFORM (x4). Step mod and the matrix both use them.
- A per-slot ENABLE param, so a slot can be switched off without losing its settings.
- The X/Y pad as the manual's four sources and controllers (PAD X, PAD Y, PAD PRESSURE, PAD GATE): CCs the engine
  reads (done 2026-10-27: Pad X, Pad Y, Pad Gate from CC 2, 3 and 18; Y also as aftertouch), PAD POLARITY, X/Y HOLD,
  and LOAD SAVED POS (the mod wheel and held pad positions saved with the sound and restored, sections 1, 19, 20).
- Depends on: nothing. Packages 2 and 6 use its results.
- Files: `src/modmatrix.h`, `src/engine.c` (`midi()`, `control_voice()`, the state), `params.json`, the pad in
  `desktop/editor.c`.

**4. LFO and envelope fidelity** (sections 8, 9). Effort M. CPU: none, except an LFO in the High range (medium for
that LFO: it needs a per-sample path).
- Done 2026-10-29 (offline; `test/lfoenv_check.c`): RANGE High (per sample on pitch, the cutoffs and the amp level;
  sidebands where they belong and none of the control rate's images), the wave "Pulse" (its width the variation,
  0-100%; Square as before), synced LFOs back to their start phase when the transport starts, `eg_times` "MIN, 13
  Syncs" (MIN 0.2 ms unscaled, the 13 synced values; the default keeps the old times, so old sounds are unchanged).
  The High range's cost is in PLAN.md. Left: High-range modulation of the other destinations is at the control rate.
- LFO RANGE High (1 Hz to 1 kHz); the pulse VARIATION over the manual's whole range, 0% (always off) to 100% (always
  on); synced LFOs restart at their start phase when the host transport starts.
- Envelope MIN times below 1 ms that time scale leaves alone; the manual's 13 synced values (1/64 to 64 beats).
- Depends on: nothing; run after package 3, or agree the `engine.c` regions (LFO and envelope code only).
- Files: `src/lfo.h`, `src/env.h`, the LFO and envelope parts of `src/engine.c`, `params.json`.

**5. Edit safety: compare, snapshots, autosave, undo** (sections 1, 21). Effort M. CPU: none.
- Done 2026-10-29 (offline and the desktop window; `desktop/library.c`, `docs/PATCHES.md`): COMPARE and SNAPSHOT on the
  console, the SNAPSHOTS page, autosaves (newest 32), undo / redo of any edit from the window (100 steps; Ctrl+Z /
  Ctrl+Y), all over the whole state (three Synths, sequences). Tested by `desktop/library_test.c` in load_test.
- COMPARE (hear the saved version, then back to the edit; loading another sound clears both); SNAPSHOT (timestamped
  saves of the sound with notes; browse, load, delete; a snapshot saved over the patch or as a new one); AUTOSAVE (a
  snapshot of unsaved edits when another sound loads); undo and redo of any edit, not only the randomiser's.
- Depends on: nothing (the window already keeps the loaded values to show `*`).
- Files: `desktop/patches.c`, `desktop/editor.c` (console buttons); snapshot files beside the patches.

**6. Quick assign and the matrix screen** (sections 0, 1, 8, 9, 15a). Effort M. CPU: none.
- Done 2026-10-30 (the desktop window; `desktop/quickmod.c`, load_test): the quick assign part. DEST buttons on LFO
  1-4, the three envelopes and NOISE; Alt-click or right-click the wheels, the X/Y pad, the ribbon, the keyboard (and
  GENERATIVE: sources added to the matrix later) for the expression sources; a click on a knob connects it in the
  focused Synths' matrix, its drag sets the amount; rings on the modulated knobs; right-click REMOVE / OPEN IN MATRIX.
  101 panel controls have a destination (the map is in PLAN.md item 22). Left: EDIT MODULATIONS, and the matrix
  page's items below.
- DEST buttons on the four LFOs and three EGs, and the Expression Assign buttons (MW, PAD X, PAD Y, KB, VEL, AT,
  EXP 1): the next control touched becomes the destination, and turning it sets the amount without moving the knob;
  ADD DESTINATION, EDIT MODULATIONS (the matrix filtered by that source), DONE, CANCEL.
- The matrix page: an enable button per row (package 3's param), DELETE with the rows below moving up, filters by
  source (shift-click a DEST) or destination (shift-click a MORE), destinations picked as module then parameter.
- Depends on: package 3.
- Files: `desktop/editor.c`, `desktop/editor_draw.c`, `desktop/gen_layout.py`.

**7. Window pictures and indicators** (sections 6, 7, 8, 9, 18). Effort M. CPU: none.
- The MORE pages' pictures: envelope shape with its curves, LFO wave with its variation, the filters' diagram, the
  mixer's bar graph; rate LEDs that flash at the LFO, arp and sequencer rates, and a tempo LED; a place on the window
  for the 14 open-source effect params (Rings, Clouds, Airwindows).
- Depends on: nothing. Shares `editor_draw.c` with package 6: one after the other, or split by function.
- Files: `desktop/editor_draw.c`, `desktop/gen_layout.py`.
- Done 2026-10-29 (desktop, offline pictures): `desktop/more_pics.c` draws the pictures under a page's items (the last
  three rows; such a page holds 10 items per sub-page): new MORE pages for the three envelopes, the mixer and the
  filters, and the LFO pages' wave. `desktop/rate_leds.c`: seven LEDs (LFO 1-4, arp, sequencer, TEMPO) on their own
  15 ms timer, repainting only the LEDs that change (load_test: 14 px squares, 0.05 ms each). The 14 open-source
  effect params on the synth effect's page by type. Left: DEST quick assign (package 6).

**8. Settings page and MIDI, part 1** (sections 1, 2, 14, 19, 22). Effort M. CPU: none.
- Done in part 2026-10-28 (offline and the desktop window): `midi_ch` (Omni, 1-16, Per Synth), silent or audible chord
  learn (`learn_mode`), the window's OCTAVE buttons held for the default octave and then a panic. The rest done
  2026-10-29 (offline and the desktop window; `docs/SETTINGS.md`): the SETTINGS page (GLOBAL, LIBRARY, TUNING) and its
  file `Settings.txt` beside the Patches folder, RESTORE DEFAULT SETTINGS, the version, RECEIVE MIDI CLOCK with an own
  tempo, MIDI I/O LOAD FROM PRESET, the sustain / EXP 1 / EXP 2 pedal CC numbers (engine: `src/midi_in*.h`, the
  "midicfg" command), PRESET AUTOLOAD. Left: the sustain pedal's target Synth [MULTI], bank preview (with
  performance sets).
- A SETTINGS page (GLOBAL, LIBRARY, TUNING) with settings kept outside the sounds, in a file beside the patches (UI
  thread only); RESTORE DEFAULT SETTINGS; the version.
- MIDI: the receive channel (Off, 1-16; today every channel plays), receive clock on or off, MIDI I/O load from
  preset; the pedal CC numbers (sustain, EXP 1, EXP 2).
- Silent or audible chord learn; a panic button, and on the window's keyboard both OCTAVE buttons back to the default
  octave and held for a panic; the preset-autoload switch.
- Depends on: nothing; the channel handling must exist before package 13.
- Files: `src/engine.c` (`midi()`), a new `desktop/settings.c`, `desktop/editor.c`.

**9. Voices: 16, and unison that re-divides** (sections 0, 14). Effort M. CPU: high per added voice (16 voices cost
about twice what 8 do; fine on the desktop, the MPC keeps its benched default).
- Done 2026-10-28 (offline): MAX_VOICES 16, `voices` and `uni_count` to 16, voice cards 9-16 between the first 8 (which
  are unchanged), unison stacks shared out as keys are added. The CPU per voice is in PLAN.md.
- MAX_VOICES 16 (voice cards for 16, the `voices` list to 16, the state); unison that stacks every voice on one key
  and shares them out as keys are added (8 voices: one key 8, two keys 4 each), as the manual describes.
- Depends on: nothing; must come before package 13 (three Synths share the pool).
- Files: `src/engine.c` (allocation, `create()`), `params.json`, `src/randomize.h` (voice ranges).

**10. Effects fidelity** (sections 0, 18). Effort L in all, in three parts that can run beside the engine work.
- 10a Master effect slots that take any effect type (the 20 synth types, plus the reverbs in one slot), with BUSS MODE
  Stereo (one effect, stereo in) or Dual Mono (two effects), the 1 -> 2 series kept. M-L; about 1 MB more memory for
  a second slot's delay lines; CPU shared, by type.
- 10b Parameter fidelity: stereo delay FEEDBACK L/R, MOD RATE/DEPTH L/R and GAIN (ping pong GAIN too); plate DISTANCE
  and INFINITE decay; room HIGH FREQ, band levels and modulation; hall band decays and levels, decay to 20 s and
  INFINITE; phaser TONE on the mix; a two-band bit crusher (crossover). M; CPU shared low.
- 10c Shimmer and blackhole style reverbs (original designs). L; CPU shared high.
- Depends on: nothing (10a before package 13 if the per-Synth effects reuse its slot design).
- Files: `src/fx.h`, `src/fx/*`, the effect parts of `src/engine.c` (`fx_field`, `fx_params`), `params.json`.
- Done 2026-10-27 (offline and the desktop build): 10a, 10b and 10c as above (section 18). Left: the desktop window's
  pages for the slots' new controls (they are on its OTHER page for now), effect presets (12c), the vocoders (11).
  The window's pages done 2026-10-29 (desktop): MASTER FX 1 and 2 (the slots' MORE pages) show each type's own
  controls (`gen_layout.py` MFX_USES, e.g. a hall 19, a ping pong 9), the panel a TYPE button per slot, the MASTER
  EFFECTS page the buss mode; the synth effect's page by type for the two-tap delays and the crusher's crossover too,
  its sync only for types with a time or a rate. No OTHER page is left (every param has a home).

**11. Audio input and vocoders** (sections 0, 6, 15e, 18). Effort M for the input, L for the vocoders. CPU: low per
voice for the EXT channel; shared medium (10-band) to medium-high (16-band) for the vocoders.
- The desktop plugin takes a stereo audio input from the host; the mixer's EXT channel: level, route (None, SVF,
  Ladder, Both) into every sounding voice's filters; the matrix destination EXT LEVEL.
- The 10- and 16-band vocoders as synth effect types (the input as the modulator, the voices as the carrier: MIX,
  SIBILANCE, SENSITIVITY, TONE, NOISE).
- Depends on: package 1 first is easier (the wrapper's input path is 16-bit today); 10a if the vocoders should also
  sit on the master bus.
- Files: `wrapper/vst2_wrap.c` (desktop switch), `src/engine.c` (a seventh mixer channel), `src/fx/synth_fx.h`.
- Done, the input (2026-10-29, offline and the desktop DLL): the wrapper is untouched. The desktop layer declares two
  inputs (`desktop/editor.c`) and `desktop/hostrate.c` converts them to 44.1 kHz (the converter run the other way,
  `resample.c` `rs_reset_io`) into a FIFO a block ahead of the engine, handing each engine block its input
  (`one_ish_set_input`, `src/synths.h`). The EXT channel (`ext_level`, `ext_route`; the MIXER's MORE page) is a seventh
  mixer channel; the matrix destination "Ext Level". The input is heard 128 samples late at 44.1 kHz (222.9 at 48 kHz),
  not reported as latency (notes play as before). CPU: the EXT channel about 1 us a voice per block on the PC while heard (8 voices 49.1 to 56.9 us), none at level 0 or silent; the input converter about 108 us per 512-frame host
  block at 48 or 96 kHz (1-2% of the block), nothing while the input is silent. Tests: `test/ext_check.c` (offline),
  `desktop/rate_test.c` (the DLL at 44.1, 48 and 96 kHz, 64 to 1000-frame blocks and in place), `resample_test.c`.
- Done, the vocoders (2026-10-29, offline and the desktop DLL): 10 and 16 Band Vocoder as synth effect types and in
  either master slot (`src/fx/vocoder.h`, an original channel vocoder), the input as the modulator; MIX, SIBILANCE,
  SENSITIVITY, TONE, NOISE (`modfx_sibil`, `modfx_sens`, `dly_sibil` / `dly_sens`, `rev_sibil` / `rev_sens`; CTRL 2's
  Auto target is SIBILANCE). CPU per 128-frame block: about 12 us (10 bands), 18-19 us (16 bands), none while no input
  plays. Left: SIBILANCE and SENSITIVITY as matrix destinations (only CTRL 1/2 move them), the master slots' window
  pages (OTHER for now), effect presets.

**12. Library and browser** (sections 1, 12, 13, 15a, 18a, 20, 22). Effort L in all, in four parts.
- 12a Tags (TYPE, CATEGORY, MOOD, GROUP) set on save and used as browser filters, editable tag lists; notes per patch
  with SHOW NOTES; the HOME line with tags and tempo. S-M. Done 2026-10-29 (offline and the desktop window; patch
  format version 2).
- 12b Performance Sets: 128 sets of 64 patches (banks A-H x 1-8 as window buttons, a long press assigns the current
  sound), the set pages (current, browse, arrange: move, copy, delete, add, name, notes), bank preview; user patches
  reachable by MIDI program and bank change. M. Done 2026-10-29 (offline and the desktop window): the bank and number
  buttons are on the SET page of the centre screen (the console has no room for sixteen more), COMPARE and SNAPSHOT
  took the console's name plate.
- 12c Libraries of parts: effect presets per type (load, save, initialise), matrix presets (load add or replace, with
  notes and timestamps), sequences (with package 2). M.
- 12d User spaces (switchable libraries); export and import of every kind of item, and of everything; the
  duplicate-name choices (ignore, overwrite, auto rename, ask). M.
- Depends on: nothing for 12a and 12b; 12c needs packages 2 and 6 for their screens.
- Files: `desktop/patches.c` (or a new `desktop/library.c`), `desktop/editor.c`, `docs/PATCHES.md`.

**13. Multitimbral: three Synths per preset** (sections 0, 1, 10, 11, 17, 18, 20, 22, 24). Effort L (two to three
weeks). CPU: high (the voices are shared, so the voice count stays; each Synth's own synth effect adds a shared
effect: up to 3 synth effects and 2 master effects at once).
- Done 2026-10-28 (offline and the desktop window; `docs/MULTITIMBRAL.md`). Owner's decision (2026-10-28, option A):
  every Synth's params are host parameters (1,529 in all: 468 per Synth, 125 shared), Synth 1's keys unchanged and
  Synths 2 and 3 as `s2_` / `s3_` copies. Built: three Synths each with every sound setting, its own arp, chord memory
  and sequencer; ON, key range, MIDI channel per Synth; one 16-voice pool with each Synth's poly count; a synth effect
  and two sends per Synth into the shared master effects; Panel Focus with multi-select and Shift/Ctrl+F5-F7; a SETUP
  page; INTO S1/2/3 and SAVE S1/2/3 on the PATCHES page; PLAY ALL; the randomiser per Synth; three factory
  performances. Solo and M | S buttons done 2026-10-29. Per-Synth outputs done 2026-10-30 (desktop: MAIN and a pair per Synth,
  PLAN.md item 38). Left: setting a range by pressing a key, sequencer OUTPUT to
  another Synth, the sustain pedal's target, timbre names on the home line, the master effects moved by Synth 2 and
  3's matrices (NRPN for Synths 2 and 3: done with package 14a, 2026-10-29).
- Three timbres per preset, each with every sound setting, its own arp and sequencer; split, layer and zones (per
  Synth ON, LOW and HIGH RANGE, full range); Panel Focus (one Synth, several together, solo; the computer keyboard's
  SHIFT/CTRL + F5-F7 shortcuts); a MIDI channel per Synth (MIDI IN ROUTE PER SYNTH); a shared voice pool with each
  Synth's poly count; level, pan and spread of all three on one page; a synth effect and two sends per Synth;
  sequencer OUTPUT to any Synth or all, several started together; the sustain pedal's target Synth; load a timbre
  from any preset into any Synth, save a timbre as its own preset; the HOME line with timbre names and key ranges;
  per-Synth stereo outputs (MAIN and SUB routing, SUB VOLUME) as extra VST2 output pairs; the randomiser per part;
  NRPN for Synths 2 and 3.
- Decide first (owner): three times today's 461 params as VST2 parameters (about 1400), or one panel's params that
  follow the Panel Focus (host automation then reaches only the focused Synth). The saved state outgrows the MPC's
  8 KB chunk (`STATE_MAX` = `VST2_CHUNK_MAX`, the wrapper's chunk; the desktop's is 256 KB since 2026-10-27).
- Depends on: packages 2, 8 (channels) and 9 (16 voices); 10a if the effect slots are reused.
- Files: most of `src/engine.c`, `params.json`, `wrapper/vst2_wrap.c` (chunk size, outputs), the window.

**14. MIDI, part 2: learn, NRPN, MIDI out** (sections 22b, 24). Effort M each. CPU: none.
- 14a MIDI learn in the plugin (click a control, move a CC; 7- or 14-bit; clear, add; the mapping list with reserved
  CCs), and NRPN input after the manual's table (p149-158) for Synth 1 (2 and 3 with package 13). Done 2026-10-29
  (offline and the desktop window; `docs/SETTINGS.md`): the SETTINGS page's MIDI tab, the map in the settings file
  and in each engine as a fixed table (`src/midi_in*.h`, set by the "midicfg" command), NRPN for all three Synths;
  the wrapper's own NRPN (param index) is off for ONE-ish (`vst.json` `"nrpn": false`). Left: 14b.
- 14b MIDI out of the plugin's own notes (keys, arp, sequencer) on a chosen channel, for desktop hosts (MPC ignores
  it). Needs the wrapper to send events to the host.
- Depends on: package 8 (the settings page holds the mapping).
- Files: `src/engine.c` (`midi()`), `desktop/settings.c`, `wrapper/vst2_wrap.c` (14b).

**15. Optional: analog depth, a desktop HQ mode** (sections 0, 3, 7). Effort L. CPU: high (2x oversampling about
doubles a voice).
- Oversampled oscillators and filters for a clean top end under sync and FM; a fuller transistor-ladder model; a
  warm-up drift after loading. A desktop-only quality switch; the MPC keeps today's models.
- Depends on: package 1 (sample-rate handling).
- Files: `src/osc.h`, `src/ladder.h`, `src/svf.h`, the voice loop in `src/engine.c`.
- Done 2026-10-30 (offline and the desktop window; `src/hq.h`, `test/hq_check.c`): HQ MODE (Off, 2x, 4x) and WARM-UP
  on the ANALOG page, per Synth, Off by default and exactly the old samples when off (sections 0, 3, 7 above). Not
  limited to the desktop in the engine: on the MPC it is the owner's call (a voice at 2x costs about 1.8 voices). Left:
  an iterative ladder solve for HQ.

**16. Optional: microtuning** (beyond the manual). Effort M. CPU: none (a table lookup per note).
- Done 2026-10-29 (offline and the desktop window; `docs/SETTINGS.md`): per Synth, in the sound's state, loaded on
  the SETTINGS page's TUNING tab; tested with 12-TET (identical samples), 19-EDO and 5-limit just intonation with a
  keyboard map (`test/scales/`). Keyboard tracking of the filters and the Keyboard source still count keys, not pitch.
- Scala `.scl` scales and `.kbm` keyboard maps, loaded from the window and saved with the sound; master tune and fine
  tune still apply.
- Depends on: package 8 (where a global tuning lives).
- Files: `src/engine.c` (`update_note_hz`), `desktop/settings.c`.

### What can run at the same time

| Lane | Packages, in order | Main files |
|---|---|---|
| Engine | 1, then 3, 4, 9, 13 | `src/engine.c`, `params.json`, `src/modmatrix.h`, `src/lfo.h`, `src/env.h` |
| Sequencer (built; small leftovers) | 2 | `src/stepseq.h`, `src/stepseq_run.h`, `desktop/seq_page.c` |
| Effects | 10a, 10b, then 11's vocoders, 10c | `src/fx.h`, `src/fx/*` |
| Window | 5, 7, then 6 (after 3) | `desktop/patches.c`, `desktop/editor_draw.c`, `desktop/editor.c` |
| Library and settings | 12a, 12b, 8, then 12c, 12d, 14 | `desktop/patches.c` or `library.c`, `desktop/settings.c` |
| Wrapper (shared with the MPC ports) | 1, then 11's input, 13's chunk and outputs, 14b | `wrapper/vst2_wrap.c`, behind a desktop switch |

Hardware-only rows ([HW]: 27 Missing, 2 Partial) stay out: the instrument's jacks, CV, inserts, panel hardware,
storage, USB, LAN and firmware, and what the host owns (master volume, tempo, tap and nudge, MIDI ports).
