# Making Moo Won's analog modelling more accurate: research report

Research only, 2026-11-02. No code was changed, nothing here has been built, and nothing has been listened to.
Every CPU figure marked "est." is a paper estimate, worked out from the bench numbers already in `PLAN.md`. It needs
measuring before anyone relies on it. Paper titles and links may name the original filter; our code, params and
presets must not.

## 1. The short version (no maths)

**Moo Won is already on the same footing as the good open-source synths.** Its oscillators and its ladder filter use
the methods those projects and the papers recommend. So there is no missing magic algorithm. The gains left are in
the parts of the signal path that are still "perfect", which real circuits never are.

**The three biggest gaps:**

1. **The second filter (SVF) is completely clean.** However hard it is pushed, it never compresses, never distorts
   and never whistles by itself. This is the largest gap between Moo Won and an analog voice.
2. **Nothing bends before the filter, and there is no feedback path.** Players get a loved thick, growling sound by
   feeding a synth's output back into its own mixer. Moo Won cannot do that today.
3. **We have nothing to measure against.** Today's analog settings were judged by ear and by internal checks. The
   first step should be a measuring kit, including a slow, accurate reference model of the ladder circuit.

**Something you can try today, with no code:** set **Amp Attack Curve** (and Filter Env Attack Curve) to about -25%.
The default, 0%, is a straight line. An analog attack is a capacitor charging: fast at first, then slowing. Negative
values on this control give that shape. Compare plucks and pads with it on and off.

**Things that are not worth doing:** power-supply sag, oscillators leaking into each other, a neural network model,
and an "exact" circuit solver. Section 6 explains why.

**How sure is this?** The maths of the ladder and of anti-aliasing is well established in peer-reviewed papers. What
is *not* well established is what real synths measure like: I searched and found almost no published measurements of
drift, VCA thump, supply sag or leakage. On those topics the sources are forum posts, and I say so wherever it matters.

**Suggested order:** (1) the measuring kit, (2) a saturating SVF, (3) a mixer drive and feedback control. Each new
sound would be a new setting that starts at 0, so old sounds stay byte-identical.

## 2. Where Moo Won stands today

Costs below are from `PLAN.md` (PC, 128-frame block, a busy patch): 8.4-8.6 us a voice, non-linear ladder 10.5 us,
HQ 2x 14.6 us (19.5 with the non-linear ladder), HQ 4x 24.5 us (34). The block budget is 2902 us, and `docs/BENCH.md`
wants p99 at or under 15% (435 us) for a PASS. The MPC's own cost per voice is **still unmeasured**.

### Already done well

| Part | What the code does | How it compares |
|---|---|---|
| Oscillators (`src/osc.h`) | 2-sample polyBLEP at jumps, polyBLAMP at corners, both also on sync resets | The JASA 2012 study finds this order perceptually alias-free on a saw up to a 2.1 kHz fundamental at 44.1 kHz, above most of the keyboard. Nothing needed for ordinary notes. |
| Saw reset (`osc_shape`) | SAW mode lengthens the reset from 6 us to 1 ms | Already a model of a finite capacitor discharge. |
| Ladder structure (`src/ladder.h`) | Zero-delay feedback, in tune to the top | Zavalishin's method, the standard reference. |
| Non-linear ladder | Per-stage tanh in the "cheap" secant form, evaluated at the stage states | The per-stage structure follows Huovilainen 2004; the solving trick is Mystran's published method. VCV Fundamental's filter credits the same three sources (Huovilainen, Zavalishin, Mystran). |
| Saturator (`ladder_sat`) | Pade tanh, no library call | Pade is suggested in the Mystran thread, and Huovilainen also advises ("table lookups or polynomial approximations"). |
| HQ mode (`src/hq.h`) | 2x and 4x with polyphase half-bands, measured | Aliasing at 2x and 4x is already measured and strong. Other projects also pick 2x (VCV) or a 4x-sub-step scheme (Surge). |
| Voice cards, drift, warm-up | Fixed per-voice spreads, slow wander, cold-start offsets | No source I found says more than this is clearly needed. |
| DC filter | 5 Hz high-pass before the amp | Real coupling-capacitor behaviour, added 2026-11-01. |
| Envelopes (`src/env.h`) | Curved stages, restart from current level | The curve control already covers the RC shape; only the default attack is a straight line. |

### Where it falls short (from the code)

| Gap | Evidence in the code | Why it matters |
|---|---|---|
| SVF is fully linear | `svf.h`: "does not self-oscillate"; resonance tops out at Q 10; no saturator or drive | +20 dB peak with no limit on level. A real filter compresses as it resonates. |
| Nothing saturates before the filters except the ladder's input | `lad_in` and `in` are plain sums in `render_voice_os` | No mixer overdrive; the SVF gets no drive at all. |
| No feedback path | No such path in `render_voice_os` | Cannot make the "feedback patch" sound. |
| Ladder's input and stages use different scales | `ladder_sat` knees at about 1; the stages at `1/LADDER_NL_HOT` (0.4). The input also passes through both `ladder_sat` and `t[0]` | In the circuit equations (Huovilainen eq. 13, 17) every pair has the same scale and the input has one tanh. It may be why self-oscillation comes out 8 dB quieter and 30 cents flat. Unproven until measured. |
| Non-linear ladder is off by default and aliases when pushed | -24..-45 dB at 1x, top notes, full drive (`PLAN.md`) | Mystran himself suggests about 4x for this method. |
| Perfectly matched stages | All four stages share one `G`; tanh is symmetric | Symmetric means no even harmonics from the filter. Real parts are never perfectly matched, though all the analyses here assume they are, so nobody has measured how much this matters. |
| Bass loss is half compensated | `bass = 1 + 0.5 k` | At k = 4 the pure circuit is about -14 dB (Stinchcombe); ours is about -4.4 dB (worked out from the code). A sound choice, but not the circuit's, and there is no way to get the circuit's. |
| Linear attack by default | `amp_acurve` default 0 | See section 1. |
| Cutoff jumps every 16 samples | `ladder_set`/`svf2_set` once per control step; no ramp for `G` (other values do ramp) | May be audible only on very fast, resonant envelopes. Needs measuring, not assuming. |
| Oscillators are not AC-coupled | Only a voice-level DC filter at 5 Hz; pulse DC is removed by formula | No droop on low pulses, no thump as the width moves. |
| Saw slightly dull at 1x | polyBLEP acts as a sinc-squared lowpass: about -1.5 dB at 10 kHz, -3.4 dB at 15 kHz, -6.3 dB at 20 kHz (my calculation) | Small. JASA gives a tiny correction filter for it. |
| Drift has one timescale, no shared part | `drift_step`: new target every 0.3-1.5 s, independent per oscillator; only WARM-UP has a shared part | Low priority. Real temperature effects are shared, but I found no measurements. |

## 3. Ranked improvements

Ranked by audible benefit per CPU, which is my judgement, not a measurement. The ear decides in the end; this is
just where to start. "Voice" percentages are of about 9.5 us. All new settings: default 0 (or Off), on the ANALOG
page.

### Rules every item below follows

- **Old sounds stay identical:** at 0, take today's code path (the way `lad_nl` already branches), so the golden
  check still passes and nothing changes.
- **ANALOG AMOUNT scales it:** 0% still gives exactly digital (`test/analog_check.c` already tests that).
- **Follow `docs/ADDING_A_PARAM.md`:** include the ANALOG page's keep-on-preset-load list and `rnd_kept` (MUTATE and
  random must not touch it), and states without the key read as 0.
- **Audio path:** state lives in `voice_t`, no allocation, locks or files, no libm tanh per sample. Everything runs
  inside the `for u < os` loop, so HQ improves it automatically.
- **A visible control for each** (no hidden gestures).

### 1. RC attack: zero cost, zero risk

- **You'd hear:** plucks and brass with more immediate fronts; pads that swell quickly and then ease in.
- **Technique:** a capacitor charges as 1 - e^-x and a comparator ends the attack partway. A lower switch point makes
  the shape closer to a straight line. In our units: switching at 86% of the rail is about -21%, at 95% about -33%.
- **Sources:** Atkinson on the W3C list and McCulloch on Cycling '74. **Both are forum/list posts**, though the RC
  physics itself is textbook. The real switch points of real chips: not found.
- **CPU:** none. **Risk:** none (a setting that exists today).
- **Test:** try it by ear. Only if liked: a new-patch default or factory presets, which is the owner's call since it
  changes those patches.
- **About overshoot above 100%:** I found no measured evidence. Do not build it on this basis.

### 2. Saturating SVF, with optional self-oscillation

- **You'd hear:** resonance that "sits" instead of getting ever louder; bite on loud chords; with the option, a
  self-oscillating whistle like the ladder's. Probably the largest audible change on this list.
- **Technique:** Mystran's secant method applied to the band path (he first used this idea on an SVF's diode limit).
  It keeps the same zero-delay structure as `ladder_tick_nl`. Add an input drive. Let damping go to about 0 only when
  this setting is above 0, so the old range is untouched. Zavalishin ch. 4 and ch. 6 cover the structure and
  saturators, and his section on the OTA ladder 1-pole covers the integrator-input position. Simper's SVF note gives
  the linear base we already use.
- **Sources' strength:** the method is solid. **Which saturator matches which classic filter is not.** In the KVR
  "Nonlinear SVF" thread, posters disagree on whether the feedback diodes or the OTA inputs matter most, and one built
  a Newton diode model and reported it did not sound right. I found no paper on it. So aim for "musically saturating",
  not a claim of emulating a specific circuit.
- **CPU:** est. +0.4-0.8 us per SVF element while it runs (+4-8%; a spaced or series pair doubles it). Basis: the
  ladder's 5 T and 4 divides cost 2.1 us, so about 0.4 us for one T and divide.
- **Risk:** medium. Level changes when on; self-oscillation aliases (one KVR poster warns strongly about this), so a
  soft limit on the band state may be needed; stays stable only if the solve is right.
- **Test:** `svf_nl_check` for level against input, self-oscillation pitch (within a few cents across 50 Hz-8 kHz),
  alias measure from `hq_check`, `robust_check` at extremes, golden unchanged.
- **Opt-in:** `an_svf` (Non-Linear SVF, 0-100%), plus `an_svf_drive`.

### 3. Mixer drive and feedback

- **You'd hear:** at low levels, thickness and glue. At high levels, compressed growl, with resonance that fights the
  signal. Patching a synth's output into its own input is a well-known player trick.
- **Technique:** one Pade soft clip on the mixer sum, then a one-sample-delayed feedback from the filter output back
  into the sum, through a level control. The ladder's input saturator keeps it bounded.
- **Sources:** player forum only (the forum thread in section 8 names the mixer loop and output-to-input). **Lore, no
  measurements.** The mechanism itself is simple physics.
- **CPU:** est. +0.3-0.5 us (+3-5%), and only when above 0.
- **Risk:** medium. At 1x the one-sample delay shifts pitch near the top, and the sound may depend on voice count, so
  make it per voice. A pre-amp tap means it does not fade with the amp envelope. That is a real choice with an
  audible difference: offer the tap point by ear in the experiment, and keep only one if possible.
- **Test:** level stays bounded at 100% feedback for all routings (`robust_check`); level and brightness printed like
  `analog_stats`; blind listening.
- **Opt-in:** `an_mix_drive`, `an_feedback`.

### 4. Ladder calibration against a reference model

- **You'd hear:** self-oscillation and heavy resonance closer to the circuit: pitch and level holding up, a more
  convincing drive.
- **Technique, step by step:**
  1. Build the slow reference (section 5), in a test program only.
  2. Try one saturation scale for input and stages, and the single input tanh, as in the published equations. Keep
     `ladder_sat` as a safety bound, set so it rarely acts.
  3. Offer the true bass loss: **Ladder Bass** from today's half compensation to none. Stinchcombe calls this droop a
     failing "seen by many", so keep it a choice.
  4. Only then consider the D'Angelo and Valimaki models. Their abstracts claim a closer match to SPICE distortion,
     realistic entry into self-oscillation, and only 12 extra basic operations per sample with the same number of
     nonlinear functions. **I could only read abstracts**: the IEEE papers are paywalled and the Aalto thesis refused
     my fetch. The owner may want to download the thesis in a browser so the method can be studied.
- **CPU:** steps 2 and 3 about 0 to +0.2 us on top of the non-linear ladder. Step 4 unknown.
- **Risk:** low to medium. A new mode must not replace the current `an_ladder` behaviour: saved sounds using it must
  stay the same, hence a new setting.
- **Test:** the section 5 curves, ours against the reference. Success means smaller gaps than today, at the same CPU.
- **Opt-in:** `an_lad_model` (Classic / Circuit), `an_bass`.

### 5. Oscillators AC-coupled (droop and leaky integrator in one)

- **You'd hear:** low pulses that sag across the cycle, a slight tilt on low saws, a soft thump as pulse width moves.
  Only clearly audible at low notes; maybe a touch more punch.
- **Technique:** a first-order high-pass per oscillator, the same as the existing DC filter. A leaky integrator acts
  much the same on a ramp, so this one setting covers both.
- **Sources:** basic circuit theory. **I found no measured corner frequency for real oscillators**, so choose it by
  ear (try 5-30 Hz) and do not claim it matches any instrument.
- **CPU:** est. +0.2-0.4 us for all three (2-4%). Only runs for running oscillators.
- **Risk:** low. Sync and ring mod tap points must be chosen with care: the ring mod reads `tri_out`, so leave that
  alone at first.
- **Test:** waveform plot of a low pulse (visible sag, the time constant as set); golden unchanged at 0.
- **Opt-in:** `an_osc_ac` (0 = off, or a corner frequency).

### 6. Filter-only 2x

- **You'd hear:** the non-linear ladder and new SVF cleaner on high notes and with drive, and more accurate, since
  the secant method improves with sample rate (Zavalishin 6.6; Mystran).
- **Technique:** run only the filters at 88.2 kHz, holding each oscillator sample, then use the existing `hq_dec2`.
  VCV's filter and Surge's Huovilainen filter also run the filter at 2x.
- **CPU:** est. +3-4.5 us (+35-45%), against +6.2 us for full 2x. On an MPC this could be the difference between
  usable and not.
- **Risk:** medium. The held samples add images into the filter, and sync and FM do not benefit. So it is an addition
  to HQ, not a replacement.
- **Test:** `hq_check` alias numbers with drive, comparing Off, Filter 2x and 2x; `hqbench`.
- **Opt-in:** a fourth option in `an_hq`, added last so old files keep their meaning.

### 7. Slightly mismatched ladder stages

- **You'd hear:** a little second harmonic and fullness at moderate drive; a slight difference from voice to voice.
  Might be very subtle.
- **Technique:** a small per-card offset inside each stage's T (asymmetric saturation, Zavalishin 6.12), and a small
  per-stage `G` difference. The DC filter already removes the resulting offset.
- **Sources:** the mechanism is sound, since the analyses all list perfect matching as an assumption (Huovilainen
  says so, as does Stinchcombe). OB-Xf's filter uses a polynomial for a "slightly mismatched" diode pair, so others
  do this too. **How large the real effect is: not found.**
- **CPU:** est. +0.1 us for the offset; about +0.3 us for per-stage `G` on the linear path.
- **Risk:** low. **Test:** second-harmonic level against drive; must be near zero at 0.
- **Opt-in:** `an_lad_match`, fixed per voice from the existing voice-card seed.

### 8. Smoother cutoff between control steps: measure first

- **You'd hear:** possibly cleaner fast resonant filter sweeps. Possibly nothing.
- **Technique:** ramp `G` per sample across the 16-sample step. For the SVF that means a divide per sample, which the
  FM path already does.
- **Sources:** my reading of our code. Parker et al. describe "steppy" resonant sweeps, but they blame aliasing, a
  different cause, so this is not support.
- **CPU:** est. +0.1 us ladder, +0.4 us per SVF element.
- **Test first:** render a 1 ms resonant filter envelope at 16-sample steps against steps of 1, and null them. If the
  difference is below -70 dB, drop the idea. **Opt-in:** only if it survives.

### 9. Restore the top end at 1x

- **You'd hear:** a very slightly more open saw with the filter wide open. Subtle.
- **Technique:** JASA 2012's 3-tap correction filter, whose coefficients are in the paper's Table IV. Skip it in HQ.
- **CPU:** est. +0.1-0.2 us per voice. **Risk:** also lifts aliasing a little, and adds a sample of delay.
- **Test:** saw harmonics against the ideal series; alias measure. **Opt-in:** `an_osc_air`.

### 10. Drift upgrade and top-note tracking

- **You'd hear:** slow "alive" movement in unison and thirds, and voices disagreeing slightly in the top octaves.
- **Technique:** add a slower wander (minutes) and one wander shared by the voice's oscillators; for each card, a
  small pitch-dependent flatness (the reset time adds to the period: with our 6 us at 4186 Hz that is up to 43 cents
  before any trim, my calculation; real instruments trim it out, so use far less).
- **Sources:** **I found no measurement of drift in synth oscillators.** A half-percent pitch JND (about 9 cents, UMD)
  says single notes hide small drift, but two detuned oscillators beat: 1 cent at 440 Hz is about 0.25 Hz. So beating,
  not pitch, is what you hear.
- **CPU:** est. under 0.05 us (control rate). **Risk:** low, but tuning can annoy; keep it small.
- **Test:** a logged pitch track; ear test with long held fifths. **Opt-in:** `an_drift_slow`, `an_hf_track`.

## 4. Suggested first three experiments

1. **Measuring kit (no change to the sound).** Section 5's reference model and curves, added to `test/`. Print today's
   results for both the linear and non-linear ladder, and render the listening pack. The owner tries the -25% attack
   meanwhile. *Worth doing first because everything after it can then be judged.* It may also reorder this list.
2. **Saturating SVF (item 2).** The largest gap, at a low cost. Stop at the plan's usual test gate, listen, bench.
3. **Mixer drive and feedback (item 3).** Very cheap and a big new sound. Then go on to ladder calibration (item 4),
   which depends on the kit.

Run `tools/bench.sh` on a device at the next MPC milestone: all the percentages here are PC ones, and divides, which
most of these ideas add, are relatively slower on small ARM cores (general knowledge, not measured here).

## 5. How to judge accuracy

### Measurements (add to the offline checks)

Output as a table and a plot, and printed by a `test/analog_measure` mode.

| Measure | Method | What it shows |
|---|---|---|
| Self-oscillation pitch | Full resonance, no input beyond the noise floor; cents against cutoff, 20 Hz-15 kHz | Tuning of the feedback loop |
| Self-oscillation level | Same run, level against cutoff, and purity of the sine | Whether the output holds up across the range |
| Onset of oscillation | Slowly raise resonance; note the value where it starts | Whether k is about 4 as in theory |
| Level against resonance | Saw at 110 Hz with the filter at 1 kHz; RMS and level at the fundamental, for k from 0 to 4 | Bass loss; compare with 1/(1+k) |
| Harmonic distortion against level | Sine at -40 to +12 dB at resonance 0, 50% and 90%; 2nd, 3rd and 5th harmonics | The core of "how it saturates" |
| Compression | Input against output level, near resonance | SVF and ladder feel |
| Aliasing | `alias_db` in `hq_check.c` (exists) | Cleanliness |
| Null against 4x | Same patch at 1x and 4x, level-matched, residual | How far 1x is from converged |
| Sweep smoothness | Slow resonant sweep; look for pitch-locking | The "steppy" effect Parker et al. describe |
| CPU | `offline_check analogbench`, `hqbench`; later `tools/bench.sh` | The cost of each setting |

### The reference model: a truth without owning the hardware

In a test program only, not in the plugin: solve the published circuit equations (Huovilainen eq. 10-17; Stinchcombe's
derivation) with a high-order method at a very high sample rate. Daly's thesis shows why this stays offline: the
Newton version needs 5-30 iterations per sample and was at least 100 times slower, and it was less stable.

**Honest limit:** this tells us how close we are to the *equations*, which assume perfectly matched parts, and not to
a real instrument. For a real reference, a capture of any actual analog synth, or a SPICE simulation like
Stinchcombe's, would be the next step. If the owner has or can borrow one, captures of a held saw through the filter
at several resonance values would be very useful.

### Listening tests the owner can run

The rule that matters most: **match levels to within about 0.2 dB and hide which is which.** Louder always sounds
better, and knowing which is new changes what you hear.

1. **Listening pack:** the test renders pairs of WAV files named A and B at random (a key file says which). Sounds:
   a low saw bass, a resonant filter sweep, a 4-note pad, a hard-driven lead, a pluck.
2. **Blind pick:** for each pair, pick the one you prefer and write down why. Do at least 8 per pair. 7 of 8 or more
   means you can really tell them apart.
3. **In-context test:** drop each into a real mix. Differences that only show when soloed rarely matter.
4. **Fatigue check:** listen again the next day before accepting any change.

Keep the pack and the results in the plan, so that decisions are not forgotten.

## 6. Looked at, and not recommended now

| Idea | Why not | Status of evidence |
|---|---|---|
| Newton or implicit solve of the ladder per sample | 5-30 iterations, at least 100 times slower and less stable (Daly). Mystran also advises doubling oversampling over adding correction steps. | Student thesis and practitioner advice |
| ADAA in the ladder | Real and well proven for waveshaping: Parker et al.'s rectangular kernel equals about 3x less oversampling (12x down to 4x, hard clipper at gain 10, their Table 1). But it adds a half-sample delay in the loop; their ladder test folded the nonlinearity into an integrator and iterated with Newton to -60 dB; Holters still pairs it with 2x, which itself doubles the work. Needs a log or a table per nonlinearity, and we have 5-6. | Peer-reviewed. Revisit only for a single saturator, or if HQ proves too costly on the MPC. |
| Higher-order polyBLEP | 4th-order B-spline raises the alias-free saw from 2.1 to 7.8 kHz (JASA Table VIII), but the keyboard rarely goes there; hard sync is the real issue and HQ already fixes that. | Peer-reviewed, with a model of hearing, not a listening panel. PEAQ results there were ambiguous. |
| Neural or black-box filter | Parker et al.'s method needs the circuit's internal states measured, and they say extending to larger systems and parametric control is future work. Needs training from hardware we do not have. A resonance knob is a parameter; that is what makes it hard. | Peer-reviewed, but not suited |
| Wave digital filter | Circuit-level libraries exist (chowdsp_wdf), but the paper's text was unreadable to me, so no cost figure. Costs and benefits unclear for 16+ voices on ARM. | Not assessed fully |
| Supply sag, voice crosstalk | Only a plugin announcement with no measurements (KVR) and forum reports of *faults*. Not a known part of the sound. | Marketing and lore |
| Oscillator bleed when "off" | It would force silent oscillators to run, costing real CPU, for something at the edge of hearing. | Lore |
| VCA thump | Very cheap, but I found no measurements, and clicks are easily disliked. Try it last, if at all. | Lore |
| VCA saturation | About +0.4 us per voice est., no evidence of an audible gain over item 3. | None found |
| Audible noise floor | Today -100 dB into the filters only. Nothing found saying more is wanted. Hiss can be added by the noise channel if desired. | None found |
| Soft sync | A new feature rather than accuracy. Real circuits differ a lot (forum). Add it to the roadmap if wanted. | Lore |

## 7. Open-source projects: what can be used

**Moo Won's repo is MIT (`LICENSE`), so GPL code cannot be pasted in**, nor ported line by line, without changing the
licence of the whole project. Ideas and maths are free to use: read the cited *papers*, then write our own code. This
is my reading, not legal advice. Pages marked "reviewed" were read as machine summaries of the main files: only the
methods, no code.

| Project | Licence (as stated by the project) | Use it for | Port? |
|---|---|---|---|
| Zavalishin, *The Art of VA Filter Design* rev. 2.1.2 | Free to copy the book | The main reference: ch. 4-6 | Maths: yes |
| Cytomic / Simper technical papers | Page says public domain, footer has a copyright line | SVF and Sallen-Key derivations | Likely yes; the two statements should be read together, and asking is easy |
| Mutable Instruments (eurorack, stmlib) | MIT for STM32F projects, GPL for AVR; name is a trademark | Already bundled (Plaits, `stmlib/dsp/polyblep.h`, MIT header checked) | Yes, per-file header. Stages is listed as a module; I did not check its files. |
| Airwindows | MIT | Already used in effects | Yes |
| VCV Fundamental VCF (reviewed) | GPL-3.0 | Same recipe as ours, at 2x: a useful confirmation | Learn only |
| Surge XT, `sst-filters` (reviewed) | GPL-3.0 | Huovilainen 2x and RK4 ladder variants; comments cite the same papers | Learn only |
| OB-Xf (reviewed) | GPL-3.0-or-later | Closed-form 2-pole with a mismatched diode polynomial; the idea matters for item 2 | Learn only |
| Odin 2 | GPLv3 | Diode ladder, K35 (via Surge's listing) | Learn only |
| Vital (reviewed `digital_svf`) | GPLv3, with name, preset and store restrictions | Hard-clip SVF: not an analog model | Learn only |
| Bogaudio | GPL-3.0-or-later; graphics CC BY-SA 4.0 | Its VCF says it is pure DSP theory, not an analog model | Little to learn here |
| ChowDSP `chowdsp_utils` | Per module: filters and waveshapers GPLv3, core BSD | ADAA clippers, if ever wanted | Learn only (GPL parts) |
| Stilson and Smith; Huovilainen; D'Angelo and Valimaki; Parker et al.; Holters; JASA 2012 | Papers | Methods | Write our own |

Not used: anything from commercial firmware or proprietary sources. A JP-8000 thesis came up in search; I did not open
it and it is not about this.

## 8. Sources

**Read (full text extracted):**
- Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter", DAFx 2004: <https://dafx.de/paper-archive/2004/P_061.PDF>
- Parker, Zavalishin, Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", DAFx 2016: <https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf>
- Holters, "Antiderivative Antialiasing for Stateful Systems", 2019: <https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2020/10/applsci-10-00020-v2.pdf>
- Zavalishin, *The Art of VA Filter Design* rev. 2.1.2 (mirror): <https://www.discodsp.net/VAFilterDesign_2.1.2.pdf>
- Valimaki, Pekonen, Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation", JASA 2012: <https://mac.kaist.ac.kr/pubs/ValimakiPeknenNam-jasa2012.pdf>
- Stinchcombe, "Analysis of the Moog Transistor Ladder and Derivative Filters", 2008 (not peer reviewed, very thorough): <https://www.timstinchcombe.co.uk/synth/Moog_ladder_tf.pdf>
- Stilson, Smith, "Analyzing the Moog VCF with Considerations for Digital Implementation", 1996: <https://ccrma.stanford.edu/~stilti/papers/moogvcf.pdf>
- Daly, "A comparison of virtual analogue Moog VCF models", MSc thesis, Edinburgh 2012: <https://www.acoustics.ed.ac.uk/wp-content/uploads/AMT_MSc_FinalProjects/2012__Daly__AMT_MSc_FinalProject_MoogVCF.pdf>
- Levien, "A matrix approach to the Moog ladder filter", 2013 (linear part only): <https://levien.com/ladder.pdf>
- Parker, Esqueda, Bergner, "Modelling of nonlinear state-space systems using a deep neural network", DAFx 2019: <https://dafx.de/paper-archive/2019/DAFx2019_paper_42.pdf>
- Esqueda, Bilbao, Valimaki, "Aliasing reduction in clipped signals", IEEE TSP 2016 (polyBLAMP on hard clipping; a possible alternative, not ranked): <https://www.pure.ed.ac.uk/ws/files/26997332/07499828.pdf>
- Simper, SVF note: <https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf>

**Abstract only (full paper not available to me):**
- D'Angelo, Valimaki, "An improved virtual analog model of the Moog ladder filter", ICASSP 2013: <https://manuscript.isc.ac/Inventory/49/1653463.htm>
- D'Angelo, Valimaki, "Generalized Moog Ladder Filter: Part I": <https://manuscript.isc.ac/Inventory/49/57408.htm>
- D'Angelo, Valimaki, "Generalized Moog Ladder Filter: Part II": <https://manuscript.isc.ac/Inventory/49/57658.htm>

**Forum, mailing list or tutorial (practitioner opinion; treat as lore):**
- Mystran's method, KVR: <https://www.kvraudio.com/forum/viewtopic.php?p=4926492> and <https://www.kvraudio.com/forum/viewtopic.php?p=4955112>
- "Nonlinear SVF" thread, KVR (2016, posters contradict each other): <https://www.kvraudio.com/forum/viewtopic.php?t=461311>
- Output-to-input distortion thread, Moog Forum: <https://forum.moogmusic.com/t/audio-input-distortion-trick/4021>
- Analog attack shape, Atkinson, W3C list: <https://lists.w3.org/Archives/Public/public-audio/2014JulSep/0058.html>
- RC envelope shape, McCulloch, Cycling '74: <https://cycling74.com/forums/analog-style-adsr-envelopes>
- ADAA practical notes: <https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html>
- Pitch JND, University of Maryland demo page: <https://lecdem.physics.umd.edu/h/h5/1988-h5-20.html>

**Marketing:**
- Plugin announcement claiming supply sag and slot bleed, no measurements: <https://www.kvraudio.com/forum/viewtopic.php?p=9271445>

**Project pages (licence and method):**
- <https://www.cytomic.com/technical-papers/>
- <https://github.com/surge-synthesizer/surge>, <https://github.com/surge-synthesizer/sst-filters>, <https://github.com/surge-synthesizer/sst-filters/blob/main/include/sst/filters/VintageLadders.h>
- <https://github.com/surge-synthesizer/OB-Xf>, <https://github.com/surge-synthesizer/OB-Xf/blob/main/src/engine/Filter.h>
- <https://github.com/TheWaveWarden/odin2>, <https://raw.githubusercontent.com/TheWaveWarden/odin2/master/LICENSE>
- <https://github.com/mtytel/vital>, <https://github.com/mtytel/vital/blob/main/src/synthesis/filters/digital_svf.cpp>
- <https://github.com/pichenettes/eurorack>
- <https://github.com/VCVRack/Fundamental>, <https://github.com/VCVRack/Fundamental/blob/v2/src/VCF.cpp>
- <https://github.com/bogaudio/BogaudioModules>, <https://raw.githubusercontent.com/bogaudio/BogaudioModules/master/LICENSE.md>
- <https://github.com/Chowdhury-DSP/chowdsp_utils>
- <https://github.com/airwindows/airwindows>

**Tried but could not read (not used as evidence):** Simper's ADC 2020 slides (too large for my fetcher); chowdsp_wdf paper
(arXiv 2210.12554, text unreadable); D'Angelo's Aalto dissertation (refused); Bilbao et al.'s 2017 ADAA paper (not
opened); and pages on saw-core reset times on Modwiggler and Synth-DIY (refused), so **the reset-time and reverse-charge
claims I saw in search snippets are not used here**.
