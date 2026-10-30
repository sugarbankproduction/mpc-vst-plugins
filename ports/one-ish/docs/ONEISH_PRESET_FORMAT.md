# Writing Moo Won presets: the `.oneish` file format

A complete guide for writing Moo Won sounds as text, by hand or with an LLM. Everything here matches what Moo Won's
loader actually accepts (checked against its source, 2026-10-31). The full list of every setting is in Appendix A, and
the long option lists (matrix sources, destinations, effect types) are in Appendix B.

Moo Won is a three-part synth: **Synth 1, Synth 2 and Synth 3**, each a complete polyphonic synth (3 oscillators,
noise, ring mod, a ladder filter and a state-variable filter pair, 3 envelopes, 4 LFOs, a 16-slot modulation matrix,
an arpeggiator, a step sequencer, generators and its own effect), sharing two master effects. One `.oneish` file holds
all three Synths: one sound.

---

## 1. The file at a glance

- Plain text, UTF-8, one `key=value` per line. File name: `<Preset Name>.oneish`.
- Lines starting with `#` and blank lines are ignored, so you can comment a preset.
- Spaces around `=` are fine. Keys are lower case, exactly as listed in Appendix A.
- Only write what differs from the defaults. Loading always starts from the defaults (section 3), then applies your
  lines. A key Moo Won doesn't know is skipped quietly, and a value that makes no sense is skipped and counted.
- Numbers outside a setting's range are clamped to the range.

```
ONE-ish patch 2
name=Glass Cathedral
type=Layer
category=Pad
mood=Dark
group=Live
notes=Synth 1 slow strings, Synth 2 bells on top. Mod wheel opens both filters.
dv=1
ctl=2
# ---- Synth 1 ----
o1_wave=Saw
flt_cut=55
amp_a=62
# ---- Synth 2 ----
s2_o1_wave=Tri
s2_flt_cut=80
```

## 2. The header and the information lines

Write these at the top, in this order:

| Line | Required | What it does |
|---|---|---|
| `ONE-ish patch 2` | yes (first line) | Marks the file as a Moo Won patch, format version 2. IMPORT refuses a file without it. |
| `name=` | yes | The preset's name, as shown in the browser (the file name is what the list shows; keep them the same). |
| `author=` | no | Who made it. |
| `type=` | no | Free text; the browser's TYPE filter uses `Single`, `Layer`, `Split`, `Multi`. |
| `category=` | no | The browser's category tabs: use one of `Bass`, `Lead`, `Pad`, `Keys`, `Strings`, `Brass`, `Bells`, `Pluck`, `Organ`, `Arp`, `Sequence`, `Drum`, `FX`, `Performance`, `Generative`. |
| `mood=` | no | Free text, e.g. `Warm`, `Dark`, `Bright`, `Aggressive`, `Soft`. |
| `group=` | no | Free text, your own grouping. |
| `notes=` | no | One line. Write a line break as `\n`. Say how to play it ("Mod wheel: vibrato"). |
| `dv=1` | **strongly recommended** | "Today's defaults" (section 3). Without it, Moo Won assumes the file is from before 2026-10-30 and fills in what you left out with the OLD defaults: Synths 2 and 3 OFF, playing a copy of Synth 1's sound, and no X/Y pad, ribbon or aftertouch rows in matrix rows 13-16. |
| `ctl=2` | recommended | Each effect's CTRL 1 / CTRL 2 knobs in "Auto" use today's targets (each effect's two most useful controls). |

## 3. The defaults (what you get without writing anything)

With `dv=1`, an empty patch is:

- **All three Synths ON**, all at the same basic sound: oscillator 1 a saw at 8', the others silent (level 0), into the
  ladder filter fully open (`flt_cut=100`), a moderate amp envelope, 4 voices each, volume 70%.
- Synth 1 on MIDI channel 1, Synth 2 on 2, Synth 3 on 3, each across the whole keyboard (`key_lo=0`, `key_hi=127`).
  (Whether they all play from one keyboard depends on the MIDI setting: in Omni, the usual case, a key plays every Synth
  that is ON and whose range covers it.)
- Matrix row 1: LFO 1 -> Pitch at 4%, VIA Mod Wheel 100% (vibrato on the mod wheel). Rows 13-16, the "hands" rows:
  13 Aftertouch -> Pitch LFO1 Amt 4%, 14 Pad X -> Ladder Cutoff -90%, 15 Pad Y -> Ladder Res 60%, 16 Ribbon -> Pitch
  8.3333% (= +-2 semitones). Every other row Off.
- Master effects ON (`mfx_on=On`), but each Synth's sends at 0 (`mfx_send1=0`, `mfx_send2=0`): no echo or reverb until
  you raise a send.
- The Synth's own effect OFF (`modfx_on=Off`).

**Consequences when writing presets:**
- **Every Synth you don't want must be switched off**, e.g. `s3_syn_on=Off`. Otherwise it plays the default saw.
- **Every Synth you do use** needs its own settings (`s2_...`, `s3_...`). They don't copy Synth 1.
- To keep the hands rows, leave rows 13-16 alone. To use those rows for something else, overwrite all their fields
  (section 6).

## 4. Values

- **Numbers** are in the setting's own units, shown in Appendix A as "number min to max unit". For example `o1_pw=35`
  (35%), `o1_freq=-0.07` (semitones), `pan=-40` (%). Decimals are fine.
- **Options** are written as their label (case doesn't matter), e.g. `o1_wave=Saw`, `flt_slope=12 dB`,
  `lfo1_wave=S&H`, `mod3_src=Velocity`. Or as their number, counting from 0 in the order listed. Prefer labels: they
  survive future versions.
- **On/off switches** are options: `On` / `Off`.
- **Curved knobs ("knob position").** Times, cutoffs, rates and similar settings are stored as the knob's position, 0
  to 100, not in ms or Hz. The curves:
  - **Filter cutoffs** (`flt_cut`, `svf_cut`): 20 Hz x 1000^(knob/100). 0 = 20 Hz, 33 = 200 Hz, 50 = 632 Hz, 67 = 2 kHz,
    83 = 6.3 kHz, 100 = 20 kHz. Formula: knob = 100 x log10(Hz / 20) / 3.
  - **Envelope times** (`amp_a`, `amp_d`, `amp_r`, `fenv_*`, `menv_*`, `noise_a`, `noise_r`), and LFO delay / fades and
    glide time: ms = 10000^(knob/100). 0 = 1 ms, 25 = 10 ms, 50 = 100 ms, 75 = 1 s, 100 = 10 s. Formula: knob = 25 x
    log10(ms). (Delay, hold, LFO delay and fades, and glide: 0 means off.) With an envelope's SYNC on, the same knob picks
    a note division instead.
  - **LFO rates** (`lfo1_rate` .. `lfo4_rate`): Hz = low x 1000^(knob/100), where `low` depends on the LFO's RANGE:
    `Slow` 0.001 Hz, `Low` (the default) 0.01 Hz, `Medium` 0.1 Hz, `High` 1 Hz. In `Low`: 0 = 0.01 Hz, 33 = 0.1 Hz,
    67 = 1 Hz, 83 = 4.6 Hz, 100 = 10 Hz. With `lfoN_sync=On` the knob picks a note division of the tempo instead
    (slowest at 0, fastest at 100).
  - **Effect times and rates** (delay time, chorus rate, reverb decay and size...): 0 to 100, the effect's own curve.
    Think in proportions: 20 short, 50 medium, 80 long.
- **Text values** (only `syn_name`, `s2_syn_name`, `s3_syn_name`: each Synth's name, at most 12 characters):
  `s2_syn_name=Bells`.

## 5. The three Synths: prefixes and scope

- A **per Synth** key without a prefix is Synth 1: `flt_cut=40`. For Synth 2 add `s2_`: `s2_flt_cut=40`. For Synth 3
  add `s3_`: `s3_flt_cut=40`.
- A **shared** key has no prefix and applies to everything: the master effects (`dly_*`, `rev_*`, `mfx_*` except the
  sends), the LINK buttons (`link_*`), the master level, the generators' links and so on. Appendix A marks every key.
  Never write `s2_` / `s3_` in front of a shared key; it is skipped.
- Each Synth's sends to the master effects (`mfx_send1`, `mfx_send2`), its own effect (`modfx_*`), its key range,
  channel, level, pan, arp, sequencer and generators are per Synth.

### Building layers and splits

| Kind | How |
|---|---|
| **Layer** (Synths sound together) | All Synths ON with full ranges (the default). Give each a different role: e.g. Synth 1 the body, Synth 2 an octave up and brighter, Synth 3 a sub. Balance with `volume`, `s2_volume`, `s3_volume`, and spread with `pan` / `s2_pan`. |
| **Split** | Set ranges with MIDI note numbers (C4 = 60, C3 = 48, C2 = 36): e.g. bass `key_lo=0`, `key_hi=59`; pad `s2_key_lo=60`, `s2_key_hi=127`. Ranges may overlap for a layered middle. |
| **Single** | `s2_syn_on=Off` and `s3_syn_on=Off`. |
| **Velocity or controller layers** | Use the matrix: e.g. Synth 2's `modN_src=Velocity`, `modN_dst=Amp Level`, so it only comes in when played hard. |

## 6. The modulation matrix (16 rows per Synth)

Each Synth has 16 rows, `N` = 1 to 16. Each row has seven fields (prefix `s2_` / `s3_` for Synths 2 and 3):

| Field | Meaning | Values |
|---|---|---|
| `modN_src` | SOURCE: what moves | one of **SOURCES** (Appendix B), `Off` for an empty row |
| `modN_dst` | DESTINATION: what is moved | one of **DESTINATIONS** (Appendix B) |
| `modN_amt` | AMOUNT | -300 to 300 % |
| `modN_via` | VIA: a second source that scales the first (e.g. the mod wheel) | one of **VIA SOURCES**, `None` = always full |
| `modN_via_amt` | how much VIA has its say | -100 to 100 % (100: source x via; -100: source x (1 - via); 0: VIA ignored) |
| `modN_xf` | TRANSFORM: reshapes the signal | `None`, `Limit Low`, `Limit High`, `Squared`, `Cubed` |
| `modN_xf_amt` | the transform's level (for the limits) | 0 to 100 % |
| `modN_on` | the row's on/off | `On` (default) / `Off` |

**Always write `src`, `dst` and `amt` together**, and `via` too. Row 1's defaults are not empty (LFO 1 -> Pitch, via Mod
Wheel), so a row you reuse must override them, e.g. `mod1_via=None`.

**What 100% means** (the destination's own scale):
- Pitch destinations: +-100% = +-24 semitones (so 1 semitone = 4.1667%, an octave = 50%).
- Cutoffs: +-100% = +-5 octaves.
- Levels, wave shapes, pulse width, FM amount, resonance, drive, filter mix, spacing, LFO variation: +-100% = the whole
  range.
- Amp Level: x0 to x2.
- Envelope times and LFO rates: +-100% = the whole knob. `LFO n Rate (st)`: +-24 semitones.
- Pan: +-100% = centre to a side.
- Effect controls (Synth FX ..., FX1 ..., FX2 ...): +-100% of that control's range.
- Amounts above 100% (up to 300%) push further for strong effects.

**Sources' shapes**: envelopes, velocity, mod wheel, aftertouch, expression pedals, ribbon and pad gate run 0 to 1;
LFOs run -1 to 1 (unless that LFO's polarity is unipolar); `Random` gives a new random value per note (-1 to 1),
`Random Uni` 0 to 1; `Constant` is a steady 1 (an offset); `Keyboard` follows the key. The generator sources (Rain Drop,
Ball Hit, Euclid Gate, Turing Value...) follow the generators when they run.

Example rows:

```
# vibrato on the mod wheel, gentle
mod1_src=LFO 1
mod1_dst=Pitch
mod1_amt=2
mod1_via=Mod Wheel
mod1_via_amt=100
# velocity brightens the ladder filter
mod2_src=Velocity
mod2_dst=Ladder Cutoff
mod2_amt=30
mod2_via=None
# the mod envelope sweeps the SVF, scaled by aftertouch
mod3_src=Mod Env
mod3_dst=SVF Cutoff
mod3_amt=60
mod3_via=Aftertouch
mod3_via_amt=100
# slow LFO 2 pans Synth 2 around
s2_mod1_src=LFO 2
s2_mod1_dst=Pan
s2_mod1_amt=70
s2_mod1_via=None
```

Besides the matrix, many routings are fixed knobs: e.g. `lad_env` / `svf_env` (filter envelope amount to each
filter), `lad_kbd` (key tracking) and the filters' LFO amounts. See Appendix A: FILTER MOD, PITCH MOD.

## 7. Signal flow, in short (so the sound is not silent)

1. **Oscillators** `o1_*`, `o2_*`, `o3_*`: octave (`oN_oct`: 32' .. 2'), fine tune (`oN_freq`, semitones), beat
   (`oN_beat`, Hz), a dual wave (`oN_wave` Tri or Saw, morphing to a pulse with `oN_mix`; `oN_angle`, `oN_pw`), sync
   (`o2_sync`, `o3_sync`), FM (section FM), ring mod (section RING MOD).
2. **Mixer**: each source's level (`o1_level`, `o2_level`, `o3_level`, `noise_level`, the ring level...) and which filter
   it feeds: `oN_lad` (ladder) and `oN_svf` (state-variable). By default oscillators feed the ladder only. **A source
   with level 0, or feeding neither filter, is silent.**
3. **Filters**: the ladder (`flt_cut`, `flt_res`, `flt_drive`, `flt_mode` LP/HP, `flt_slope`) and the SVF pair
   (`svf_cut`, `svf_res`, `svf_mode`, spacing, series/parallel). `flt_mix` blends the two filters' outputs (100 = the
   ladder only, 0 = the SVF only).
4. **Amp envelope** `amp_a`, `amp_d`, `amp_s` (sustain %), `amp_r`, then `volume`, `pan`, `spread`.
5. **The Synth's effect** `modfx_on=On`, `modfx_type`, `modfx_mix`, CTRL knobs. **Master effects**: each Synth's
   `mfx_send1` (to FX 1, by default a delay) and `mfx_send2` (to FX 2, by default a reverb); the shared `dly_type`,
   `rev_type` and their settings.

## 8. Playing and performance features

- **Voices and mono**: `voices` (the Synth's polyphony; a mono choice is in its list), `uni_*` (unison count and
  detune), `glide=On` with `glide_time`, legato and the glide type.
- **Arpeggiator**: `arp_on=On`, pattern, octaves, rate (synced), gate, swing, latch (section ARP).
- **Step sequencer**: per Synth. A text preset can't write steps (they are stored in an encoded `seq=` line that only
  Moo Won writes). Pick a factory sequence with `seq_pattern` (`Bass Line`, `Chord Stabs`, `Ratchet Run`), set
  `seq_on=On`, and choose length, division and the rest in section SEQUENCER. `seq_play` (Running) starts it.
- **Generators** (section GENERATIVE and LIFE / ORBITS / PENDULUMS): Euclid, Turing, bouncing balls, particles, Life,
  orbits, pendulums, rain, Markov. Each has an `..._on` switch and settings, plus KEYS (how keys start it). **Write
  `gen_keys` explicitly**: if you leave it out, Moo Won reads it as `Held Keys`, the value from before the setting
  existed. The same goes for `gen_follow_keys` (missing = Off). `key_root` / `key_scale` set the key they play in.
- **Expression**: the matrix sources `Mod Wheel`, `Pitch Wheel`, `Aftertouch`, `Velocity`, `Ribbon`, `Pad X (CC 2)`,
  `Pad Y (CC 3)`, `Pad Gate (CC 18)`, `Exp 1 (CC 11)`, `Exp 2 (CC 4)`, `Sustain`. A good preset gives the mod wheel,
  aftertouch and the X/Y pad something musical to do, and says so in `notes=`.
- **LINKs** (shared): `link_send` (on by default), `link_cut`, `link_lfoN`, `link_arp`, `link_seq`, `link_key`,
  `link_scale` and others keep settings the same across the three Synths. `lfoN_share` makes Synths 2 and 3 play
  Synth 1's LFO N exactly, in step.
- **Analog character** (section ANALOG): `drift`, the `an_*` settings, `an_hq` (2x / 4x oversampling: cleaner but
  costs CPU) and warm-up. Moo Won's RANDOMISE never changes these.

## 9. Rules for a good, safe preset

1. Start with the header, `name=`, the tags, `dv=1`, `ctl=2`.
2. Decide the structure (Single, Layer, Split) and switch unused Synths off.
3. For every Synth used: oscillators and their levels, filter routing, filters, envelopes, volume.
4. Fill the matrix rows you want, all fields of each row. Keep rows 13-16 unless you mean to replace them.
5. Effects: each Synth's own effect and its sends; the shared master effects.
6. Keep levels sane: with three Synths layered, use volumes around 40-60% each, not 100%. Resonance above about 80%
   with high drive can get loud.
7. Write `gen_keys` and `gen_follow_keys` whenever a generator is on.
8. Don't write readouts (Appendix A marks them "read-only") or the `seq=` line.
9. Name the file `<name>.oneish`. In Moo Won: PATCHES page, IMPORT, choose the file. It's added to your current
   library. (Or copy it straight into `Documents\ONE-ish\Patches` and the browser lists it.)

## 10. A complete example (a three-Synth split and layer)

```
ONE-ish patch 2
name=Night Market
author=Example
type=Split
category=Performance
mood=Warm
notes=Left hand: round bass (below C4). Right hand: pad layered with bells.\nMod wheel: vibrato. Aftertouch: opens the pad. Pad X: bell brightness.
dv=1
ctl=2
# ---- Synth 1: bass, keys below C4 ----
syn_name=Bass
key_lo=0
key_hi=59
voices=Mono
o1_wave=Saw
o1_oct=16'
o1_level=90
o2_level=70
o2_oct=32'
o2_wave=Tri
o2_lad=On
flt_cut=38
flt_res=25
lad_env=45
fenv_a=0
fenv_d=48
fenv_s=20
amp_a=0
amp_d=60
amp_s=80
amp_r=30
volume=60
# ---- Synth 2: pad, C4 and up ----
s2_syn_name=Pad
s2_key_lo=60
s2_key_hi=127
s2_o1_wave=Saw
s2_o2_wave=Saw
s2_o2_level=80
s2_o2_freq=0.12
s2_o2_lad=On
s2_flt_cut=48
s2_amp_a=62
s2_amp_r=70
s2_volume=50
s2_mfx_send2=45
s2_mod2_src=Aftertouch
s2_mod2_dst=Ladder Cutoff
s2_mod2_amt=35
s2_mod2_via=None
# ---- Synth 3: bells over the pad ----
s3_syn_name=Bells
s3_key_lo=60
s3_key_hi=127
s3_o1_wave=Tri
s3_o1_oct=4'
s3_amp_a=0
s3_amp_d=72
s3_amp_s=0
s3_amp_r=70
s3_volume=40
s3_mfx_send1=30
s3_mfx_send2=50
s3_mod2_src=Pad X (CC 2)
s3_mod2_dst=Ladder Cutoff
s3_mod2_amt=60
s3_mod2_via=None
```

---

## Appendix A: every parameter

Generated from Moo Won's own parameter list (`ports/one-ish/params.json`, 774 keys for Synth 1 and the shared settings; 612 of them also exist for Synths 2 and 3 with the prefixes `s2_` / `s3_`). **Scope**: *per Synth* keys can be written three times (`key`, `s2_key`, `s3_key`); *shared* keys exist once and apply to all three Synths (never write `s2_` / `s3_` for them). Defaults are for a sound with `dv=1` (see section 2).

### OSC 1

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o1_oct` | Osc 1 Octave | option: `32'` / `16'` / `8'` / `4'` / `2'` | `8'` | per Synth |
| `o1_freq` | Osc 1 Frequency | number -7 to 7 st | `0` | per Synth |
| `o1_beat` | Osc 1 Beat Freq | number -7 to 7 Hz | `0` | per Synth |
| `o1_wave` | Osc 1 Wave | option: `Tri` / `Saw` | `Saw` | per Synth |
| `o1_angle` | Osc 1 Wave Angle | number 0 to 100 %, whole numbers | `0` | per Synth |
| `o1_pw` | Osc 1 Pulse Width | number 5 to 95 %, whole numbers | `50` | per Synth |
| `o1_mix` | Osc 1 Wave Mix | number 0 to 100 %, whole numbers | `0` | per Synth |

### OSC 2

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o2_oct` | Osc 2 Octave | option: `32'` / `16'` / `8'` / `4'` / `2'` | `8'` | per Synth |
| `o2_freq` | Osc 2 Frequency | number -7 to 7 st | `0` | per Synth |
| `o2_beat` | Osc 2 Beat Freq | number -7 to 7 Hz | `0` | per Synth |
| `o2_wave` | Osc 2 Wave | option: `Tri` / `Saw` | `Saw` | per Synth |
| `o2_angle` | Osc 2 Wave Angle | number 0 to 100 %, whole numbers | `0` | per Synth |
| `o2_pw` | Osc 2 Pulse Width | number 5 to 95 %, whole numbers | `50` | per Synth |
| `o2_mix` | Osc 2 Wave Mix | number 0 to 100 %, whole numbers | `0` | per Synth |
| `o2_sync` | Osc 2 Hard Sync | option: `Off` / `On` | `Off` | per Synth |

### OSC 3

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o3_oct` | Osc 3 Octave | option: `32'` / `16'` / `8'` / `4'` / `2'` | `8'` | per Synth |
| `o3_freq` | Osc 3 Frequency | number -7 to 7 st | `0` | per Synth |
| `o3_beat` | Osc 3 Beat Freq | number -7 to 7 Hz | `0` | per Synth |
| `o3_wave` | Osc 3 Wave | option: `Tri` / `Saw` | `Saw` | per Synth |
| `o3_angle` | Osc 3 Wave Angle | number 0 to 100 %, whole numbers | `0` | per Synth |
| `o3_pw` | Osc 3 Pulse Width | number 5 to 95 %, whole numbers | `50` | per Synth |
| `o3_mix` | Osc 3 Wave Mix | number 0 to 100 %, whole numbers | `0` | per Synth |
| `o3_sync` | Osc 3 Hard Sync | option: `Off` / `On` | `Off` | per Synth |

### RING MOD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `ring_src` | Ring Mod Sources | option: `1 x 2` / `2 x 3` | `1 x 2` | per Synth |

### FM

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `fm_amt` | FM Amount | number 0 to 100 %, whole numbers | `0` | per Synth |
| `fm_route` | FM Route | option: `1 > 2` / `1 > 3` / `3 > 1` | `1 > 2` | per Synth |

### MIXER

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o1_level` | Osc 1 Level | number 0 to 100 %, whole numbers | `100` | per Synth |
| `o2_level` | Osc 2 Level | number 0 to 100 %, whole numbers | `0` | per Synth |
| `o3_level` | Osc 3 Level | number 0 to 100 %, whole numbers | `0` | per Synth |
| `ring_level` | Ring Mod Level | number 0 to 100 %, whole numbers | `0` | per Synth |
| `noise_level` | Noise Level | number 0 to 100 %, whole numbers | `0` | per Synth |

### MIXER FILTERS

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o1_svf` | Osc 1 to SVF | option: `Off` / `On` | `Off` | per Synth |
| `o1_lad` | Osc 1 to Ladder | option: `Off` / `On` | `On` | per Synth |
| `o2_svf` | Osc 2 to SVF | option: `Off` / `On` | `Off` | per Synth |
| `o2_lad` | Osc 2 to Ladder | option: `Off` / `On` | `On` | per Synth |
| `o3_svf` | Osc 3 to SVF | option: `Off` / `On` | `Off` | per Synth |
| `o3_lad` | Osc 3 to Ladder | option: `Off` / `On` | `On` | per Synth |
| `noise_svf` | Noise to SVF | option: `Off` / `On` | `Off` | per Synth |
| `noise_lad` | Noise to Ladder | option: `Off` / `On` | `On` | per Synth |
| `ring_svf` | Ring Mod to SVF | option: `Off` / `On` | `Off` | per Synth |
| `ring_lad` | Ring Mod to Ladder | option: `Off` / `On` | `On` | per Synth |

### SVF

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `svf_cut` | SVF Cutoff | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `100` | per Synth |
| `svf_res` | SVF Resonance | number 0 to 100 %, whole numbers | `0` | per Synth |
| `svf_mode` | SVF Mode | option: `LP` / `BP` / `HP` / `Notch` | `LP` | per Synth |

### LADDER

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `flt_cut` | Ladder Cutoff | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `100` | per Synth |
| `flt_res` | Ladder Resonance | number 0 to 100 %, whole numbers | `0` | per Synth |
| `flt_drive` | Ladder Drive | number 0 to 100 %, whole numbers | `0` | per Synth |
| `flt_mode` | Ladder Mode | option: `LP` / `HP` | `LP` | per Synth |
| `flt_slope` | Ladder Slope | option: `24 dB` / `18 dB` / `12 dB` / `6 dB` | `24 dB` | per Synth |

### FILTERS

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `flt_route` | Filter Route | option: `Parallel` / `Series` | `Parallel` | per Synth |
| `flt_mix` | Filter Mix | number 0 to 100 %, whole numbers | `100` | per Synth |

### FILTER MOD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lad_env` | Ladder Env Amt | number -100 to 100 %, whole numbers | `0` | per Synth |
| `lad_lfo` | Ladder LFO2 Amt | number -100 to 100 %, whole numbers | `0` | per Synth |
| `lad_kbd` | Ladder Kbd Track | number -200 to 200 %, whole numbers | `0` | per Synth |
| `svf_env` | SVF Env Amt | number -100 to 100 %, whole numbers | `0` | per Synth |
| `svf_lfo` | SVF LFO2 Amt | number -100 to 100 %, whole numbers | `0` | per Synth |
| `svf_kbd` | SVF Kbd Track | number -200 to 200 %, whole numbers | `0` | per Synth |

### FILTER ENV

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `fenv_a` | Filter Env Attack | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0.0` | per Synth |
| `fenv_d` | Filter Env Decay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `65.1` | per Synth |
| `fenv_s` | Filter Env Sustain | number 0 to 100 %, whole numbers | `0` | per Synth |
| `fenv_r` | Filter Env Release | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `61.9` | per Synth |

### AMP ENV

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `amp_a` | Amp Attack | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `17.5` | per Synth |
| `amp_d` | Amp Decay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `61.9` | per Synth |
| `amp_s` | Amp Sustain | number 0 to 100 %, whole numbers | `70` | per Synth |
| `amp_r` | Amp Release | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `59.9` | per Synth |

### MOD ENV

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `menv_a` | Mod Env Attack | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0.0` | per Synth |
| `menv_d` | Mod Env Decay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `61.9` | per Synth |
| `menv_s` | Mod Env Sustain | number 0 to 100 %, whole numbers | `0` | per Synth |
| `menv_r` | Mod Env Release | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `61.9` | per Synth |

### PITCH MOD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `pitch_lfo` | Pitch LFO1 Amt | number -100 to 100 %, whole numbers | `0` | per Synth |
| `pitch_env` | Pitch Mod Env Amt | number -100 to 100 %, whole numbers | `0` | per Synth |

### LFO 1

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo1_rate` | LFO 1 Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `90.0` | per Synth |
| `lfo1_wave` | LFO 1 Wave | option: `Triangle` / `Sine` / `Square` / `Saw` / `Ramp` / `S&H` / `Noise` / `Pulse` | `Triangle` | per Synth |
| `lfo1_sync` | LFO 1 Sync | option: `Off` / `On` | `Off` | per Synth |

### LFO 2

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo2_rate` | LFO 2 Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `66.7` | per Synth |
| `lfo2_wave` | LFO 2 Wave | option: `Triangle` / `Sine` / `Square` / `Saw` / `Ramp` / `S&H` / `Noise` / `Pulse` | `Triangle` | per Synth |
| `lfo2_sync` | LFO 2 Sync | option: `Off` / `On` | `Off` | per Synth |

### PITCH BEND

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `bend_up` | Pitch Bend Up | number -24 to 24 st | `2` | per Synth |
| `bend_down` | Pitch Bend Down | number -24 to 24 st | `-2` | per Synth |

### MOD 1

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modN_src` (N = 1..16) | Mod N Source | option: one of the list **SOURCES** (43 choices) | `LFO 1` | per Synth |
| `modN_dst` (N = 1..16) | Mod N Dest | option: one of the list **DESTINATIONS** (296 choices) | `Pitch` | per Synth |
| `modN_amt` (N = 1..16) | Mod N Amount | number -300 to 300 %, whole numbers | `4` | per Synth |
| `modN_via` (N = 1..16) | Mod N Via | option: one of the list **VIA SOURCES** (43 choices) | `Mod Wheel` | per Synth |
| `modN_via_amt` (N = 1..16) | Mod N Via Amount | number -100 to 100 %, whole numbers | `100` | per Synth |
| `modN_xf` (N = 1..16) | Mod N Transform | option: `None` / `Limit Low` / `Limit High` / `Squared` / `Cubed` | `None` | per Synth |
| `modN_xf_amt` (N = 1..16) | Mod N Transform Amt | number 0 to 100 %, whole numbers | `0` | per Synth |

### VOICES

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `voices` | Voices | option: `Mono` / `2` / `3` / `4` / `5` / `6` / `7` / `8` / `9` / `10` / `11` / `12` / `13` / `14` / `15` / `16` | `4` | per Synth |

### OUT

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `volume` | Volume | number 0 to 100 %, whole numbers | `70` | per Synth |

### GLIDE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `glide` | Glide | option: `Off` / `On` | `Off` | per Synth |
| `glide_time` | Glide Time | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50.0` | per Synth |
| `glide_type` | Glide Type | option: `LCR` / `LCT` / `EXP` | `LCR` | per Synth |
| `glide_legato` | Legato Glide | option: `Off` / `On` | `Off` | per Synth |
| `glide_gated` | Gated Glide | option: `Off` / `On` | `Off` | per Synth |
| `glissando` | Glissando | option: `Off` / `On` | `Off` | per Synth |

### UNISON

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `unison` | Unison | option: `Off` / `On` | `Off` | per Synth |
| `uni_count` | Unison Count | option: `2` / `3` / `4` / `5` / `6` / `7` / `8` / `9` / `10` / `11` / `12` / `13` / `14` / `15` / `16` | `4` | per Synth |
| `uni_detune` | Unison Detune | number 0 to 100 %, whole numbers | `25` | per Synth |
| `pitch_var` | Pitch Variance | number 0 to 50 ct, whole numbers | `0` | per Synth |

### SVF PAIR

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `svf_space` | SVF Spacing | number -100 to 100 %, whole numbers | `0` | per Synth |
| `svf_pair` | SVF Series/Parallel | option: `Series` / `Parallel` | `Parallel` | per Synth |
| `flt_link` | Link Cutoff | option: `Off` / `On` | `Off` | per Synth |

### FILTER FM

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lad_fm` | Ladder FM Amt | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lad_fm_src` | Ladder FM Source | option: `Osc 1` / `Osc 2` / `Osc 3` | `Osc 1` | per Synth |
| `svf_fm` | SVF FM Amt | number 0 to 100 %, whole numbers | `0` | per Synth |
| `svf_fm_src` | SVF FM Source | option: `Osc 1` / `Osc 2` / `Osc 3` | `Osc 1` | per Synth |

### FILTER ENV MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `fenv_delay` | Filter Env Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `fenv_hold` | Filter Env Hold | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `fenv_acurve` | Filter Env Attack Curve | number -100 to 100 %, whole numbers | `0` | per Synth |
| `fenv_dcurve` | Filter Env Decay Curve | number -100 to 100 %, whole numbers | `50` | per Synth |
| `fenv_rcurve` | Filter Env Release Curve | number -100 to 100 %, whole numbers | `50` | per Synth |
| `fenv_loop` | Filter Env Loop | option: `Off` / `On` | `Off` | per Synth |
| `fenv_latch` | Filter Env Latch | option: `Off` / `On` | `Off` | per Synth |
| `fenv_sync` | Filter Env Sync | option: `Off` / `On` | `Off` | per Synth |
| `fenv_multi` | Filter Env Multi Trig | option: `Off` / `On` | `On` | per Synth |
| `fenv_reset` | Filter Env Reset | option: `Off` / `On` | `Off` | per Synth |
| `fenv_scale` | Filter Env Time Scale | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `fenv_vel` | Filter Env Vel Amount | number -100 to 100 %, whole numbers | `0` | per Synth |

### AMP ENV MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `amp_delay` | Amp Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `amp_hold` | Amp Hold | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `amp_acurve` | Amp Attack Curve | number -100 to 100 %, whole numbers | `0` | per Synth |
| `amp_dcurve` | Amp Decay Curve | number -100 to 100 %, whole numbers | `50` | per Synth |
| `amp_rcurve` | Amp Release Curve | number -100 to 100 %, whole numbers | `50` | per Synth |
| `amp_loop` | Amp Loop | option: `Off` / `On` | `Off` | per Synth |
| `amp_latch` | Amp Latch | option: `Off` / `On` | `Off` | per Synth |
| `amp_sync` | Amp Sync | option: `Off` / `On` | `Off` | per Synth |
| `amp_multi` | Amp Multi Trig | option: `Off` / `On` | `On` | per Synth |
| `amp_reset` | Amp Reset | option: `Off` / `On` | `Off` | per Synth |
| `amp_scale` | Amp Time Scale | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `amp_vel` | Amp Vel Amount | number -100 to 100 %, whole numbers | `100` | per Synth |

### MOD ENV MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `menv_delay` | Mod Env Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `menv_hold` | Mod Env Hold | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `menv_acurve` | Mod Env Attack Curve | number -100 to 100 %, whole numbers | `0` | per Synth |
| `menv_dcurve` | Mod Env Decay Curve | number -100 to 100 %, whole numbers | `50` | per Synth |
| `menv_rcurve` | Mod Env Release Curve | number -100 to 100 %, whole numbers | `50` | per Synth |
| `menv_loop` | Mod Env Loop | option: `Off` / `On` | `Off` | per Synth |
| `menv_latch` | Mod Env Latch | option: `Off` / `On` | `Off` | per Synth |
| `menv_sync` | Mod Env Sync | option: `Off` / `On` | `Off` | per Synth |
| `menv_multi` | Mod Env Multi Trig | option: `Off` / `On` | `On` | per Synth |
| `menv_reset` | Mod Env Reset | option: `Off` / `On` | `Off` | per Synth |
| `menv_scale` | Mod Env Time Scale | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `menv_vel` | Mod Env Vel Amount | number -100 to 100 %, whole numbers | `0` | per Synth |

### KEYBOARD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `hold` | Hold | option: `Off` / `On` | `Off` | per Synth |
| `vel_curve` | Velocity Curve | option: `Soft` / `Medium` / `Hard` / `Compress` / `Expand` | `Medium` | per Synth |
| `transpose` | Semitone Transpose | number -12 to 12 st, whole numbers | `0` | per Synth |
| `octave_shift` | Octave Shift | option: `-2` / `-1` / `0` / `+1` / `+2` | `0` | per Synth |
| `key_center` | Key Track Center | number 0 to 127 (knob position; the screen shows it in ms / Hz / steps) | `60` | per Synth |

### VOICES MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `note_priority` | Mono Note Priority | option: `Low` / `High` / `Last` | `Last` | per Synth |
| `steal_mode` | Voice Stealing | option: `No Stealing` / `Oldest` / `Quietest` / `Newest` | `Oldest` | per Synth |

### TUNING

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `master_tune` | Master Tune | number 400 to 480 Hz | `440` | shared |
| `fine_tune` | Fine Tune | number -100 to 100 ct, whole numbers | `0` | shared |

### OSC 1 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o1_track` | Osc 1 Pitch Tracking | number 0 to 200 %, whole numbers | `100` | per Synth |
| `o1_reset` | Osc 1 Note-On Reset | option: `Off` / `On` | `Off` | per Synth |
| `o1_bend_up_off` | Osc 1 Bend Up Offset | number -24 to 24 st, whole numbers | `0` | per Synth |
| `o1_bend_dn_off` | Osc 1 Bend Down Offset | number -24 to 24 st, whole numbers | `0` | per Synth |

### OSC 2 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o2_track` | Osc 2 Pitch Tracking | number 0 to 200 %, whole numbers | `100` | per Synth |
| `o2_reset` | Osc 2 Note-On Reset | option: `Off` / `On` | `Off` | per Synth |
| `o2_bend_up_off` | Osc 2 Bend Up Offset | number -24 to 24 st, whole numbers | `0` | per Synth |
| `o2_bend_dn_off` | Osc 2 Bend Down Offset | number -24 to 24 st, whole numbers | `0` | per Synth |
| `o2_sync_src` | Osc 2 Sync Source | option: `Osc 1` / `Osc 3` | `Osc 1` | per Synth |

### OSC 3 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `o3_track` | Osc 3 Pitch Tracking | number 0 to 200 %, whole numbers | `100` | per Synth |
| `o3_reset` | Osc 3 Note-On Reset | option: `Off` / `On` | `Off` | per Synth |
| `o3_bend_up_off` | Osc 3 Bend Up Offset | number -24 to 24 st, whole numbers | `0` | per Synth |
| `o3_bend_dn_off` | Osc 3 Bend Down Offset | number -24 to 24 st, whole numbers | `0` | per Synth |
| `o3_sync_src` | Osc 3 Sync Source | option: `Osc 1` / `Osc 2` | `Osc 1` | per Synth |

### LFO 3

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo3_rate` | LFO 3 Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `76.7` | per Synth |
| `lfo3_wave` | LFO 3 Wave | option: `Triangle` / `Sine` / `Square` / `Saw` / `Ramp` / `S&H` / `Noise` / `Pulse` | `Triangle` | per Synth |
| `lfo3_sync` | LFO 3 Sync | option: `Off` / `On` | `Off` | per Synth |

### LFO 4

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo4_rate` | LFO 4 Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `56.6` | per Synth |
| `lfo4_wave` | LFO 4 Wave | option: `Triangle` / `Sine` / `Square` / `Saw` / `Ramp` / `S&H` / `Noise` / `Pulse` | `Triangle` | per Synth |
| `lfo4_sync` | LFO 4 Sync | option: `Off` / `On` | `Off` | per Synth |

### OSC WAVE MOD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `angle_lfo3` | Angle LFO3 Amt | number 0 to 100 %, whole numbers | `0` | per Synth |
| `pw_lfo3` | PW LFO3 Amt | number 0 to 100 %, whole numbers | `0` | per Synth |

### LFO 1 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo1_range` | LFO 1 Range | option: `Slow` / `Low` / `Medium` / `High` | `Low` | per Synth |
| `lfo1_var` | LFO 1 Variation | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo1_polarity` | LFO 1 Polarity | option: `Bipolar` / `Unipolar` | `Bipolar` | per Synth |
| `lfo1_phase` | LFO 1 Start Phase | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo1_reset` | LFO 1 Note Reset | option: `Off` / `On` | `Off` | per Synth |
| `lfo1_delay` | LFO 1 Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo1_fade_in` | LFO 1 Fade In | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo1_fade_out` | LFO 1 Fade Out | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo1_repeats` | LFO 1 Repeats | option: one of the list **LIST 4 (first used by lfo1_repeats)** (33 choices) | `Inf` | per Synth |
| `lfo1_smooth` | LFO 1 Smoothing | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo1_timing` | LFO 1 Sync Timing | option: `Straight` / `Dotted` / `Triplet` | `Straight` | per Synth |

### LFO 2 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo2_range` | LFO 2 Range | option: `Slow` / `Low` / `Medium` / `High` | `Low` | per Synth |
| `lfo2_var` | LFO 2 Variation | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo2_polarity` | LFO 2 Polarity | option: `Bipolar` / `Unipolar` | `Bipolar` | per Synth |
| `lfo2_phase` | LFO 2 Start Phase | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo2_reset` | LFO 2 Note Reset | option: `Off` / `On` | `Off` | per Synth |
| `lfo2_delay` | LFO 2 Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo2_fade_in` | LFO 2 Fade In | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo2_fade_out` | LFO 2 Fade Out | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo2_repeats` | LFO 2 Repeats | option: one of the list **LIST 4 (first used by lfo1_repeats)** (33 choices) | `Inf` | per Synth |
| `lfo2_smooth` | LFO 2 Smoothing | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo2_timing` | LFO 2 Sync Timing | option: `Straight` / `Dotted` / `Triplet` | `Straight` | per Synth |

### LFO 3 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo3_range` | LFO 3 Range | option: `Slow` / `Low` / `Medium` / `High` | `Low` | per Synth |
| `lfo3_var` | LFO 3 Variation | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo3_polarity` | LFO 3 Polarity | option: `Bipolar` / `Unipolar` | `Bipolar` | per Synth |
| `lfo3_phase` | LFO 3 Start Phase | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo3_reset` | LFO 3 Note Reset | option: `Off` / `On` | `Off` | per Synth |
| `lfo3_delay` | LFO 3 Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo3_fade_in` | LFO 3 Fade In | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo3_fade_out` | LFO 3 Fade Out | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo3_repeats` | LFO 3 Repeats | option: one of the list **LIST 4 (first used by lfo1_repeats)** (33 choices) | `Inf` | per Synth |
| `lfo3_smooth` | LFO 3 Smoothing | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo3_timing` | LFO 3 Sync Timing | option: `Straight` / `Dotted` / `Triplet` | `Straight` | per Synth |

### LFO 4 MORE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `lfo4_range` | LFO 4 Range | option: `Slow` / `Low` / `Medium` / `High` | `Low` | per Synth |
| `lfo4_var` | LFO 4 Variation | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo4_polarity` | LFO 4 Polarity | option: `Bipolar` / `Unipolar` | `Bipolar` | per Synth |
| `lfo4_phase` | LFO 4 Start Phase | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo4_reset` | LFO 4 Note Reset | option: `Off` / `On` | `Off` | per Synth |
| `lfo4_delay` | LFO 4 Delay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo4_fade_in` | LFO 4 Fade In | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo4_fade_out` | LFO 4 Fade Out | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `lfo4_repeats` | LFO 4 Repeats | option: one of the list **LIST 4 (first used by lfo1_repeats)** (33 choices) | `Inf` | per Synth |
| `lfo4_smooth` | LFO 4 Smoothing | number 0 to 100 %, whole numbers | `0` | per Synth |
| `lfo4_timing` | LFO 4 Sync Timing | option: `Straight` / `Dotted` / `Triplet` | `Straight` | per Synth |

### SYNTH FX

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_on` | Synth FX On | option: `Off` / `On` | `Off` | per Synth |
| `modfx_type` | Synth FX Type | option: one of the list **LIST 5 (first used by modfx_type)** (25 choices) | `Chorus` | per Synth |
| `modfx_mix` | Synth FX Mix | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_ctrl1` | Synth FX CTRL 1 | number -100 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `modfx_ctrl2` | Synth FX CTRL 2 | number -100 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `modfx_c1_dst` | CTRL 1 Target | option: one of the list **LIST 6 (first used by modfx_c1_dst)** (49 choices) | `Auto` | per Synth |
| `modfx_c2_dst` | CTRL 2 Target | option: one of the list **LIST 6 (first used by modfx_c1_dst)** (49 choices) | `Auto` | per Synth |
| `modfx_gain` | Synth FX Gain | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `modfx_sync` | Synth FX Sync | option: `Off` / `On` | `Off` | per Synth |

### SYNTH FX MOD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_time` | Synth FX Time | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `68.4` | per Synth |
| `modfx_time_r` | Synth FX Time R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `75` | per Synth |
| `modfx_rate` | Synth FX Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `51.5` | per Synth |
| `modfx_rate_r` | Synth FX Rate R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `55` | per Synth |
| `modfx_depth` | Synth FX Depth | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_fb` | Synth FX Feedback | number -100 to 100 %, whole numbers | `0` | per Synth |
| `modfx_fb_r` | Synth FX Feedback R | number -100 to 100 %, whole numbers | `0` | per Synth |
| `modfx_spread` | Synth FX Spread | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_tone` | Synth FX Tone | number 0 to 100 %, whole numbers | `100` | per Synth |
| `modfx_noise` | Synth FX Noise | number 0 to 100 %, whole numbers | `0` | per Synth |
| `modfx_vrate` | Synth FX Vibrato Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `85` | per Synth |
| `modfx_vdepth` | Synth FX Vibrato Depth | number 0 to 100 %, whole numbers | `20` | per Synth |

### SYNTH FX DELAY

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_dtime` | Synth FX Delay Time | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `82.4` | per Synth |
| `modfx_dfb` | Synth FX Delay Feedback | number 0 to 100 %, whole numbers | `35` | per Synth |
| `modfx_sweep` | Synth FX Sweep | number 0 to 100 %, whole numbers | `35` | per Synth |
| `modfx_flutter` | Synth FX Flutter | number 0 to 100 %, whole numbers | `30` | per Synth |
| `modfx_wow` | Synth FX Wow | number 0 to 100 %, whole numbers | `30` | per Synth |
| `modfx_bias` | Synth FX Tape Bias | number 0 to 100 %, whole numbers | `30` | per Synth |
| `modfx_room` | Synth FX Echo Room | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_decay` | Synth FX Echo Decay | number 0 to 100 %, whole numbers | `50` | per Synth |

### SYNTH FX CRUSH/RESON

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_bits` | Synth FX Bit Depth | number 2 to 24 bit, whole numbers | `8` | per Synth |
| `modfx_srate` | Synth FX Sample Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `76.3` | per Synth |
| `modfx_reso` | Synth FX Resonance | number 0 to 100 %, whole numbers | `70` | per Synth |
| `modfx_ftype` | Synth FX Filter Type | option: `LP` / `BP` / `HP` | `BP` | per Synth |
| `modfx_cut1` | Synth FX Cutoff Low | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50.8` | per Synth |
| `modfx_cut2` | Synth FX Cutoff Mid | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `59.9` | per Synth |
| `modfx_cut3` | Synth FX Cutoff High | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `80.8` | per Synth |
| `modfx_lvl1` | Synth FX Level Low | number 0 to 100 %, whole numbers | `100` | per Synth |
| `modfx_lvl2` | Synth FX Level Mid | number 0 to 100 %, whole numbers | `100` | per Synth |
| `modfx_lvl3` | Synth FX Level High | number 0 to 100 %, whole numbers | `100` | per Synth |

### NOISE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `noise_color_pair` | Noise Color | option: `Red+White` / `Red+Purple` / `White+Purple` | `Red+White` | per Synth |
| `noise_color_mix` | Noise Color Mix | number 0 to 100 %, whole numbers | `100` | per Synth |
| `noise_env` | Noise Env | option: `Off` / `On` | `Off` | per Synth |
| `noise_a` | Noise Env Attack | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |
| `noise_r` | Noise Env Release | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `noise_sus` | Noise Env Sustain | option: `Off` / `On` | `On` | per Synth |

### VCA

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `pan` | Pan | number -100 to 100 %, whole numbers | `0` | per Synth |
| `spread` | Spread | number 0 to 100 %, whole numbers | `0` | per Synth |

### ANALOG

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `an_cut` | Voice Cutoff Spread | number 0 to 100 %, whole numbers | `0` | per Synth |
| `an_env` | Voice Env Time Spread | number 0 to 100 %, whole numbers | `0` | per Synth |
| `an_lvl` | Voice Level Spread | number 0 to 100 %, whole numbers | `0` | per Synth |
| `an_tune` | Voice Tuning Spread | number 0 to 100 %, whole numbers | `0` | per Synth |
| `an_res` | Voice Resonance Spread | number 0 to 100 %, whole numbers | `0` | per Synth |
| `an_ladder` | Non-Linear Ladder | number 0 to 100 %, whole numbers | `0` | per Synth |
| `drift` | Analog Drift | number 0 to 100 %, whole numbers | `0` | per Synth |
| `an_hq` | HQ Mode | option: `Off` / `2x` / `4x` | `Off` | per Synth |
| `an_warm` | Warm-Up | option: `Off` / `On` | `Off` | per Synth |
| `an_warm_amt` | Warm-Up Amount | number 0 to 100 %, whole numbers | `50` | per Synth |
| `an_warm_time` | Warm-Up Time | number 1 to 20 min, whole numbers | `5` | per Synth |

### RIBBON

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `ribbon_dst` | Ribbon Dest | option: one of the list **DESTINATIONS** (296 choices) | `Off` | per Synth |
| `ribbon_amt` | Ribbon Amount | number -100 to 100 %, whole numbers | `50` | per Synth |
| `ribbon_mode` | Ribbon Mode | option: `Spring` / `Hold` | `Spring` | per Synth |
| `ribbon_origin` | Ribbon Origin | option: `Centre` / `Left` | `Centre` | per Synth |

### ARP

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `arp_on` | Arp On | option: `Off` / `On` | `Off` | per Synth |
| `arp_pattern` | Arp Pattern | option: `Up` / `Down` / `Order` / `Random` | `Up` | per Synth |
| `arp_oct` | Arp Octaves | option: `1` / `2` / `3` / `4` | `1` | per Synth |
| `arp_dir` | Arp Oct Direction | option: `Up` / `Down` / `Up/Down` / `Down/Up` | `Up` | per Synth |
| `arp_oct_order` | Arp Octave Order | option: `Off` / `On` | `Off` | per Synth |
| `arp_pendulum` | Arp Pendulum | option: `Off` / `On` | `Off` | per Synth |
| `arp_pend_mode` | Arp Pendulum Ends | option: `Once` / `Twice` | `Once` | per Synth |
| `arp_rnd_mode` | Arp Random Mode | option: `Non-Repeating` / `Repeating` | `Non-Repeating` | per Synth |
| `arp_sync` | Arp Sync | option: `Off` / `On` | `On` | per Synth |
| `arp_rate` | Arp Free Rate | number 10 to 280 BPM, whole numbers | `120` | per Synth |
| `arp_div` | Arp Division | option: `1/1` / `1/2` / `1/4` / `1/8` / `1/16` / `1/32` / `1/64` / `1/128` | `1/16` | per Synth |
| `arp_timing` | Arp Timing | option: `Straight` / `Dotted` / `Triplet` | `Straight` | per Synth |
| `arp_gate` | Arp Gate Length | number 1 to 100 %, whole numbers | `50` | per Synth |
| `arp_swing` | Arp Swing | number 0 to 100 %, whole numbers | `50` | per Synth |
| `arp_latch` | Arp Latch | option: `Off` / `On` | `Off` | per Synth |
| `arp_reset` | Arp Note Reset | option: `Off` / `On` | `On` | per Synth |

### CHORD

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `chord_on` | Chord On | option: `Off` / `On` | `Off` | per Synth |
| `chord_shape` | Chord Shape | option: `Custom` / `Major` / `Minor` / `Sus2` / `Sus4` / `Dim` / `Aug` / `Maj7` / `Min7` / `Dom7` / `Maj9` / `Min9` / `Add9` / `Power` / `Octaves` | `Major` | per Synth |
| `chord_learn` | Chord Learn | option: `Off` / `On` | `Off` | per Synth |
| `chord_iv1` | Chord Note 2 | number -24 to 24 st, whole numbers | `0` | per Synth |
| `chord_iv2` | Chord Note 3 | number -24 to 24 st, whole numbers | `0` | per Synth |
| `chord_iv3` | Chord Note 4 | number -24 to 24 st, whole numbers | `0` | per Synth |
| `chord_iv4` | Chord Note 5 | number -24 to 24 st, whole numbers | `0` | per Synth |
| `chord_iv5` | Chord Note 6 | number -24 to 24 st, whole numbers | `0` | per Synth |

### GENERATIVE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `gen_euclid_on` | Euclid On | option: `Off` / `On` | `Off` | per Synth |
| `gen_steps` | Euclid Steps | number 1 to 32, whole numbers | `16` | per Synth |
| `gen_hits` | Euclid Hits | number 0 to 32, whole numbers | `5` | per Synth |
| `gen_rot` | Euclid Rotation | number 0 to 31, whole numbers | `0` | per Synth |
| `gen_prob` | Step Probability | number 0 to 100 %, whole numbers | `100` | per Synth |
| `gen_vel_rand` | Velocity Random | number 0 to 100 %, whole numbers | `0` | per Synth |
| `gen_tm_on` | Turing On | option: `Off` / `On` | `Off` | per Synth |
| `gen_tm_len` | Turing Length | number 2 to 16, whole numbers | `8` | per Synth |
| `gen_tm_change` | Turing Change | number 0 to 100 %, whole numbers | `25` | per Synth |
| `gen_scale` | Turing Scale | option: `Held Notes` / `Chromatic` / `Major` / `Minor` / `Dorian` / `Mixolydian` / `Pent Major` / `Pent Minor` / `Blues` | `Held Notes` | per Synth |
| `gen_range` | Turing Octaves | option: `1` / `2` / `3` / `4` | `1` | per Synth |
| `key_root` | Key | option: `C` / `C#` / `D` / `D#` / `E` / `F` / `F#` / `G` / `G#` / `A` / `A#` / `B` | `C` | per Synth |
| `key_scale` | Scale | option: `Major` / `Minor` / `Dorian` / `Phrygian` / `Lydian` / `Mixolydian` / `Locrian` / `Harmonic Minor` / `Melodic Minor` / `Pent Major` / `Pent Minor` / `Blues` / `Whole Tone` / `Chromatic` | `Minor` | per Synth |
| `phys_balls_on` | Balls On | option: `Off` / `On` | `Off` | per Synth |
| `phys_balls` | Balls | number 1 to 8, whole numbers | `3` | per Synth |
| `phys_speed` | Phys Speed | number 0 to 100 %, whole numbers | `40` | per Synth |
| `phys_gravity` | Gravity | number 0 to 100 %, whole numbers | `40` | per Synth |
| `phys_size` | Box Size | number 0 to 100 %, whole numbers | `50` | per Synth |
| `phys_bounce` | Bounce | number 50 to 100 %, whole numbers | `90` | per Synth |
| `phys_parts_on` | Particles On | option: `Off` / `On` | `Off` | per Synth |
| `phys_rate` | Particle Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `35` | per Synth |
| `phys_spread` | Particle Spread | number 0 to 100 %, whole numbers | `35` | per Synth |
| `phys_life` | Particle Life | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `phys_attract` | Attraction | number 0 to 100 %, whole numbers | `0` | per Synth |
| `phys_trigger` | Particles Play | option: `Crossing Line` / `Collision` / `Dying` | `Crossing Line` | per Synth |
| `phys_keys` | Phys Keys | option: `Free` / `Held Keys` / `Key Launch` / `Key Emit` | `Free` | per Synth |
| `phys_restart` | Key Restarts | option: `Off` / `On` | `Off` | per Synth |
| `phys_sync` | Phys Sync | option: `Off` / `1/4` / `1/8` / `1/16` / `1/32` / `1/8T` / `1/16T` | `Off` | per Synth |
| `phys_len` | Phys Note Length | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `45` | per Synth |
| `phys_octave` | Phys Low Octave | option: `C1` / `C2` / `C3` / `C4` / `C5` / `C6` | `C3` | per Synth |
| `phys_range` | Phys Octaves | option: `1` / `2` / `3` / `4` | `2` | per Synth |
| `phys_max` | Max Notes/s | number 1 to 30 /s, whole numbers | `8` | per Synth |
| `phys_dyn` | Phys Dynamics | number 0 to 100 %, whole numbers | `60` | per Synth |
| `rain_on` | Rain On | option: `Off` / `On` | `Off` | per Synth |
| `rain_rate` | Rain Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `40` | per Synth |
| `rain_sync` | Rain Sync | option: `Off` / `1/4` / `1/8` / `1/16` / `1/32` / `1/8T` / `1/16T` | `Off` | per Synth |
| `rain_spread` | Drop Size Spread | number 0 to 100 %, whole numbers | `50` | per Synth |
| `rain_speed` | Ripple Speed | number 0 to 100 %, whole numbers | `40` | per Synth |
| `rain_life` | Ripple Life | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `rain_echo` | Ripple Echo | number 0 to 100 %, whole numbers | `40` | per Synth |
| `rain_stones` | Stones | number 0 to 6, whole numbers | `2` | per Synth |
| `rain_keys` | Rain Keys | option: `Free` / `Held Keys` / `Key Launch` / `Key Emit` | `Free` | per Synth |
| `rain_len` | Rain Note Length | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `45` | per Synth |
| `rain_octave` | Rain Low Octave | option: `C1` / `C2` / `C3` / `C4` / `C5` / `C6` | `C3` | per Synth |
| `rain_range` | Rain Octaves | option: `1` / `2` / `3` / `4` | `2` | per Synth |
| `rain_max` | Rain Max Notes/s | number 1 to 30 /s, whole numbers | `6` | per Synth |
| `mkv_on` | Markov On | option: `Off` / `On` | `Off` | per Synth |
| `mkv_order` | Markov Order | option: `1` / `2` | `2` | per Synth |
| `mkv_learn` | Markov Learn | option: `Off` / `On` | `On` | per Synth |
| `mkv_forget` | Markov Forget | option: `Off` / `On` | `Off` | per Synth |
| `mkv_creat` | Creativity | number 0 to 100 %, whole numbers | `35` | per Synth |
| `mkv_rhythm` | Markov Rhythm | option: `Learned` / `Straight` / `Sparse` | `Learned` | per Synth |
| `mkv_quant` | Markov In Key | option: `Off` / `On` | `On` | per Synth |
| `mkv_rate` | Markov Rate | option: `1/4` / `1/8` / `1/16` / `1/32` / `1/8T` / `1/16T` | `1/8` | per Synth |
| `mkv_gate` | Markov Note Length | number 5 to 100 %, whole numbers | `70` | per Synth |
| `mkv_range` | Markov Octaves | option: `1` / `2` / `3` / `4` | `2` | per Synth |
| `mkv_keys` | Markov Keys | option: `Free` / `Held Keys` / `Key Launch` | `Free` | per Synth |
| `mkv_max` | Markov Max Notes/s | number 1 to 30 /s, whole numbers | `8` | per Synth |
| `gen_follow_tempo` | Gen Follow Tempo | option: `Off` / `On` | `On` | shared |
| `gen_keys` | Euclid/Turing Keys | option: `Free` / `Held Keys` / `Key Launch` | `Free` | per Synth |
| `gen_octave` | Euclid/Turing Octave | option: `C1` / `C2` / `C3` / `C4` / `C5` / `C6` | `C3` | per Synth |
| `gen_eu_notes` | Euclid Notes | option: `Root` / `Scale Up` / `Scale Up/Down` / `Scale Random` | `Root` | per Synth |
| `gen_tempo_src` | Gen Tempo | option: `Global` / `Own` | `Global` | per Synth |
| `gen_bpm` | Gen Own BPM | number 20 to 300 BPM, whole numbers | `120` | per Synth |
| `gen_follow_keys` | Gen Follow Keys | option: `Off` / `On` | `On` | per Synth |

### MACRO OSC

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `mac_model` | Macro Model | option: `VA` / `Waveshaper` / `2-Op FM` / `Formant` / `Additive` / `Wavetable` / `Chords` / `Speech` / `Swarm` / `Noise Filter` / `Particles` / `String` / `Modal` / `Bass Drum` / `Snare` / `Hi-Hat` / `VA Filter` / `Phase Dist` / `6-Op FM 1` / `6-Op FM 2` / `6-Op FM 3` / `Wave Terrain` / `String Machine` / `Chiptune` | `VA` | per Synth |
| `mac_harm` | Macro Harmonics | number 0 to 100 %, whole numbers | `50` | per Synth |
| `mac_timbre` | Macro Timbre | number 0 to 100 %, whole numbers | `50` | per Synth |
| `mac_morph` | Macro Morph | number 0 to 100 %, whole numbers | `50` | per Synth |
| `mac_oct` | Macro Octave | option: `32'` / `16'` / `8'` / `4'` / `2'` | `8'` | per Synth |
| `mac_freq` | Macro Frequency | number -7 to 7 st | `0` | per Synth |
| `mac_aux` | Macro Aux Mix | number 0 to 100 %, whole numbers | `0` | per Synth |
| `mac_level` | Macro Level | number 0 to 100 %, whole numbers | `0` | per Synth |
| `mac_svf` | Macro to SVF | option: `Off` / `On` | `Off` | per Synth |
| `mac_lad` | Macro to Ladder | option: `Off` / `On` | `On` | per Synth |

### SYNTH FX RESONATOR II

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_model` | Synth FX Resonator Model | option: `Modal` / `Sympathetic` / `String` / `FM` / `Chords` / `String + Verb` | `Modal` | per Synth |
| `modfx_pitch` | Synth FX Pitch | number -48 to 48 st | `0` | per Synth |
| `modfx_follow` | Synth FX Key Follow | option: `Off` / `On` | `On` | per Synth |
| `modfx_struct` | Synth FX Structure | number 0 to 100 %, whole numbers | `40` | per Synth |
| `modfx_pos` | Synth FX Position | number 0 to 100 %, whole numbers | `50` | per Synth |

### SYNTH FX GRANULAR

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_gmode` | Synth FX Granular Mode | option: `Granular` / `Stretch` / `Looping Delay` / `Spectral` | `Granular` | per Synth |
| `modfx_quality` | Synth FX Granular Quality | option: `16-bit Stereo` / `16-bit Mono` / `8-bit Stereo` / `8-bit Mono` | `16-bit Stereo` | per Synth |
| `modfx_size` | Synth FX Size | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_density` | Synth FX Density | number 0 to 100 %, whole numbers | `70` | per Synth |
| `modfx_texture` | Synth FX Texture | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_freeze` | Synth FX Freeze | option: `Off` / `On` | `Off` | per Synth |

### SYNTH FX DRIVE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_drive` | Synth FX Drive | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | per Synth |
| `modfx_bump` | Synth FX Head Bump | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_lowcut` | Synth FX Low Cut | number 0 to 100 %, whole numbers | `0` | per Synth |

### SYNTH FX TWO-TAP / CRUSH

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_dtime_r` | Synth FX Delay Time R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `82.4` | per Synth |
| `modfx_depth_r` | Synth FX Depth R | number 0 to 100 %, whole numbers | `50` | per Synth |
| `modfx_offset` | Synth FX Offset | number 0 to 100 %, whole numbers | `0` | per Synth |
| `modfx_fbl` | Synth FX Feedback Left | number 0 to 100 %, whole numbers | `100` | per Synth |
| `modfx_fbr` | Synth FX Feedback Right | number 0 to 100 %, whole numbers | `100` | per Synth |
| `modfx_xover` | Synth FX Crossover | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | per Synth |

### MASTER FX

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `mfx_on` | Master FX On | option: `Off` / `On` | `On` | shared |
| `mfx_buss` | Master FX Buss Mode | option: `Dual Mono` / `Stereo` | `Dual Mono` | shared |
| `mfx_send1` | Send 1 (FX1) | number 0 to 100 %, whole numbers | `0` | per Synth |
| `mfx_send2` | Send 2 (FX2) | number 0 to 100 %, whole numbers | `0` | per Synth |
| `mfx_series` | FX1 > FX2 Series | number 0 to 100 %, whole numbers | `0` | shared |
| `mfx_width` | Master FX Width | number 0 to 200 %, whole numbers | `100` | shared |

### MASTER FX 1

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `dly_type` | FX1 Type | option: one of the list **LIST 7 (first used by dly_type)** (26 choices) | `Stereo Delay` | shared |
| `dly_mix` | FX1 Mix | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_gain` | FX1 Gain | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | shared |
| `dly_sync` | FX1 Sync | option: `Off` / `On` | `Off` | shared |
| `dly_time` | FX1 Time L | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `83.7` | shared |
| `dly_time_r` | FX1 Time R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `83.7` | shared |
| `dly_fb` | FX1 Feedback | number -100 to 100 %, whole numbers | `35` | shared |
| `dly_fb_r` | FX1 Feedback R | number -100 to 100 %, whole numbers | `0` | shared |
| `dly_fbl` | FX1 Feedback Left | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_fbr` | FX1 Feedback Right | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_rate` | FX1 Rate L | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `51.5` | shared |
| `dly_rate_r` | FX1 Rate R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `55` | shared |
| `dly_depth` | FX1 Depth L | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_depth_r` | FX1 Depth R | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_tone` | FX1 Tone | number 0 to 100 %, whole numbers | `60` | shared |
| `dly_offset` | FX1 Offset | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_spread` | FX1 Spread | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_noise` | FX1 Noise | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_sweep` | FX1 Sweep | number 0 to 100 %, whole numbers | `35` | shared |
| `dly_dfb` | FX1 Delay Feedback | number 0 to 100 %, whole numbers | `35` | shared |
| `dly_vrate` | FX1 Vibrato Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `85` | shared |
| `dly_vdepth` | FX1 Vibrato Depth | number 0 to 100 %, whole numbers | `20` | shared |
| `dly_flutter` | FX1 Flutter | number 0 to 100 %, whole numbers | `30` | shared |
| `dly_wow` | FX1 Wow | number 0 to 100 %, whole numbers | `30` | shared |
| `dly_bias` | FX1 Tape Bias | number 0 to 100 %, whole numbers | `30` | shared |
| `dly_room` | FX1 Echo Room | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_decay` | FX1 Decay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `62.4` | shared |
| `dly_size` | FX1 Size | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | shared |
| `dly_pre` | FX1 Pre-delay | number 0 to 2000 ms, whole numbers | `10` | shared |
| `dly_damp` | FX1 High Damp | number 0 to 100 %, whole numbers | `40` | shared |
| `dly_lowdamp` | FX1 Low Damp | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_diff` | FX1 Diffusion | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_mod` | FX1 Mod Level | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_early` | FX1 Early/Late | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_freeze` | FX1 Hold | option: `Off` / `Freeze` / `Infinite` / `Pitch Freeze` | `Off` | shared |
| `dly_distance` | FX1 Distance | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_hifreq` | FX1 High Freq | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `68.3` | shared |
| `dly_hlevel` | FX1 High Level | number -100 to 100 %, whole numbers | `0` | shared |
| `dly_mlevel` | FX1 Mid Level | number -100 to 100 %, whole numbers | `0` | shared |
| `dly_llevel` | FX1 Low Level | number -100 to 100 %, whole numbers | `0` | shared |
| `dly_ldecay` | FX1 Low Decay | number 0.1 to 5 x | `1` | shared |
| `dly_hdecay` | FX1 High Decay | number 0.1 to 5 x | `1` | shared |
| `dly_pitch` | FX1 Pitch A | number -2400 to 2400 cents, whole numbers | `1200` | shared |
| `dly_pitch_b` | FX1 Pitch B | number -2400 to 2400 cents, whole numbers | `700` | shared |
| `dly_pdecay` | FX1 Pitch Decay | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_gravity` | FX1 Gravity | number -100 to 100 %, whole numbers | `0` | shared |
| `dly_xover` | FX1 Crossover | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | shared |
| `dly_bits` | FX1 Bit Depth | number 2 to 24 bit, whole numbers | `8` | shared |
| `dly_srate` | FX1 Sample Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `76.3` | shared |
| `dly_reso` | FX1 Resonance | number 0 to 100 %, whole numbers | `70` | shared |
| `dly_cut1` | FX1 Cutoff Low | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50.8` | shared |
| `dly_cut2` | FX1 Cutoff Mid | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `59.9` | shared |
| `dly_cut3` | FX1 Cutoff High | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `80.8` | shared |
| `dly_lvl1` | FX1 Level Low | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_lvl2` | FX1 Level Mid | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_lvl3` | FX1 Level High | number 0 to 100 %, whole numbers | `100` | shared |
| `dly_ftype` | FX1 Filter Type | option: `LP` / `BP` / `HP` | `BP` | shared |
| `dly_sibil` | FX1 Sibilance | number 0 to 100 %, whole numbers | `30` | shared |
| `dly_sens` | FX1 Sensitivity | number 0 to 100 %, whole numbers | `50` | shared |

### MASTER FX 2

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `rev_type` | FX2 Type | option: one of the list **LIST 7 (first used by dly_type)** (26 choices) | `Plate` | shared |
| `rev_mix` | FX2 Mix | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_gain` | FX2 Gain | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | shared |
| `rev_sync` | FX2 Sync | option: `Off` / `On` | `Off` | shared |
| `rev_time` | FX2 Time L | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `83.7` | shared |
| `rev_time_r` | FX2 Time R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `83.7` | shared |
| `rev_fb` | FX2 Feedback | number -100 to 100 %, whole numbers | `35` | shared |
| `rev_fb_r` | FX2 Feedback R | number -100 to 100 %, whole numbers | `0` | shared |
| `rev_fbl` | FX2 Feedback Left | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_fbr` | FX2 Feedback Right | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_rate` | FX2 Rate L | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `51.5` | shared |
| `rev_rate_r` | FX2 Rate R | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `55` | shared |
| `rev_depth` | FX2 Depth L | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_depth_r` | FX2 Depth R | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_tone` | FX2 Tone | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_offset` | FX2 Offset | number 0 to 100 %, whole numbers | `0` | shared |
| `rev_spread` | FX2 Spread | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_noise` | FX2 Noise | number 0 to 100 %, whole numbers | `0` | shared |
| `rev_sweep` | FX2 Sweep | number 0 to 100 %, whole numbers | `35` | shared |
| `rev_dfb` | FX2 Delay Feedback | number 0 to 100 %, whole numbers | `35` | shared |
| `rev_vrate` | FX2 Vibrato Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `85` | shared |
| `rev_vdepth` | FX2 Vibrato Depth | number 0 to 100 %, whole numbers | `20` | shared |
| `rev_flutter` | FX2 Flutter | number 0 to 100 %, whole numbers | `30` | shared |
| `rev_wow` | FX2 Wow | number 0 to 100 %, whole numbers | `30` | shared |
| `rev_bias` | FX2 Tape Bias | number 0 to 100 %, whole numbers | `30` | shared |
| `rev_room` | FX2 Echo Room | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_decay` | FX2 Decay | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `62.4` | shared |
| `rev_size` | FX2 Size | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50` | shared |
| `rev_pre` | FX2 Pre-delay | number 0 to 2000 ms, whole numbers | `10` | shared |
| `rev_damp` | FX2 High Damp | number 0 to 100 %, whole numbers | `40` | shared |
| `rev_lowdamp` | FX2 Low Damp | number 0 to 100 %, whole numbers | `0` | shared |
| `rev_diff` | FX2 Diffusion | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_mod` | FX2 Mod Level | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_early` | FX2 Early/Late | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_freeze` | FX2 Hold | option: `Off` / `Freeze` / `Infinite` / `Pitch Freeze` | `Off` | shared |
| `rev_distance` | FX2 Distance | number 0 to 100 %, whole numbers | `0` | shared |
| `rev_hifreq` | FX2 High Freq | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `68.3` | shared |
| `rev_hlevel` | FX2 High Level | number -100 to 100 %, whole numbers | `0` | shared |
| `rev_mlevel` | FX2 Mid Level | number -100 to 100 %, whole numbers | `0` | shared |
| `rev_llevel` | FX2 Low Level | number -100 to 100 %, whole numbers | `0` | shared |
| `rev_ldecay` | FX2 Low Decay | number 0.1 to 5 x | `1` | shared |
| `rev_hdecay` | FX2 High Decay | number 0.1 to 5 x | `1` | shared |
| `rev_pitch` | FX2 Pitch A | number -2400 to 2400 cents, whole numbers | `1200` | shared |
| `rev_pitch_b` | FX2 Pitch B | number -2400 to 2400 cents, whole numbers | `700` | shared |
| `rev_pdecay` | FX2 Pitch Decay | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_gravity` | FX2 Gravity | number -100 to 100 %, whole numbers | `0` | shared |
| `rev_xover` | FX2 Crossover | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `0` | shared |
| `rev_bits` | FX2 Bit Depth | number 2 to 24 bit, whole numbers | `8` | shared |
| `rev_srate` | FX2 Sample Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `76.3` | shared |
| `rev_reso` | FX2 Resonance | number 0 to 100 %, whole numbers | `70` | shared |
| `rev_cut1` | FX2 Cutoff Low | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `50.8` | shared |
| `rev_cut2` | FX2 Cutoff Mid | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `59.9` | shared |
| `rev_cut3` | FX2 Cutoff High | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `80.8` | shared |
| `rev_lvl1` | FX2 Level Low | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_lvl2` | FX2 Level Mid | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_lvl3` | FX2 Level High | number 0 to 100 %, whole numbers | `100` | shared |
| `rev_ftype` | FX2 Filter Type | option: `LP` / `BP` / `HP` | `BP` | shared |
| `rev_sibil` | FX2 Sibilance | number 0 to 100 %, whole numbers | `30` | shared |
| `rev_sens` | FX2 Sensitivity | number 0 to 100 %, whole numbers | `50` | shared |

### SEQUENCER

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `seq_on` | Seq On | option: `Off` / `On` | `Off` | per Synth |
| `seq_play` | Seq Play | option: `Stopped` / `Running` | `Stopped` | per Synth |
| `seq_rec` | Seq Record | option: `Off` / `On` | `Off` | per Synth |
| `seq_step_mod` | Seq Step Mod | option: `Off` / `On` | `Off` | per Synth |
| `seq_sync` | Seq Sync | option: `Off` / `On` | `On` | per Synth |
| `seq_rate` | Seq Free Rate | number 10 to 280 BPM, whole numbers | `120` | per Synth |
| `seq_div` | Seq Division | option: `1/1` / `1/2` / `1/4` / `1/8` / `1/16` / `1/32` / `1/64` / `1/128` | `1/16` | per Synth |
| `seq_timing` | Seq Timing | option: `Straight` / `Dotted` / `Triplet` | `Straight` | per Synth |
| `seq_length` | Seq Length | number 1 to 64, whole numbers | `16` | per Synth |
| `seq_follow_swing` | Seq Follow Swing | option: `Off` / `On` | `On` | per Synth |
| `clock_swing` | Master Swing | number 0 to 100 %, whole numbers | `50` | shared |
| `seq_note_trig` | Seq Note Trig | option: `Off` / `On` | `Off` | per Synth |
| `seq_note_reset` | Seq Note Reset | option: `Off` / `On` | `On` | per Synth |
| `seq_restart_hold` | Seq Restart On Hold | option: `Off` / `On` | `Off` | per Synth |
| `seq_restart_trig` | Seq Restart On Trig | option: `Off` / `On` | `On` | per Synth |
| `seq_transpose` | Seq Transpose | option: `Off` / `On` | `Off` | per Synth |
| `seq_tr_mode` | Seq Transpose Mode | option: `Start Key` / `Interval` | `Start Key` | per Synth |
| `seq_one_shot` | Seq One Shot | option: `Off` / `On` | `Off` | per Synth |
| `seq_rec_mode` | Seq Record Mode | option: `Replace` / `Overdub` / `Focused` | `Replace` | per Synth |
| `seq_pattern` | Seq Factory Sequence | option: `User` / `Bass Line` / `Chord Stabs` / `Ratchet Run` | `User` | per Synth |
| `seq_pos` | Seq Step | read-only readout: do not write |  | per Synth |

### SETTINGS

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `midi_ch` | MIDI Channel | option: `Omni` / `1` / `2` / `3` / `4` / `5` / `6` / `7` / `8` / `9` / `10` / `11` / `12` / `13` / `14` / `15` / `16` / `Per Synth` | `Omni` | shared |
| `learn_mode` | Chord Learn Mode | option: `Audible` / `Silent` | `Audible` | shared |

### SYNTH SETUP

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `syn_on` | Synth On | option: `Off` / `On` | `On` | per Synth |
| `syn_ch` | Synth MIDI Channel | option: `Omni` / `1` / `2` / `3` / `4` / `5` / `6` / `7` / `8` / `9` / `10` / `11` / `12` / `13` / `14` / `15` / `16` | `1` | per Synth |
| `key_lo` | Key Range Low | number 0 to 127 (knob position; the screen shows it in ms / Hz / steps), whole numbers | `0` | per Synth |
| `key_hi` | Key Range High | number 0 to 127 (knob position; the screen shows it in ms / Hz / steps), whole numbers | `127` | per Synth |
| `syn_solo` | Synth Solo | option: `Off` / `On` | `Off` | per Synth |
| `syn_out` | Synth Output | option: `Main` / `Own Pair` / `Main + Own` | `Main` | per Synth |
| `mout_on` | MIDI Out | option: `Off` / `On` | `Off` | per Synth |
| `mout_send` | MIDI Out Sends | option: `Keys` / `Generated` / `Both` | `Both` | per Synth |
| `mout_ch` | MIDI Out Channel | option: `1` / `2` / `3` / `4` / `5` / `6` / `7` / `8` / `9` / `10` / `11` / `12` / `13` / `14` / `15` / `16` | `1` | per Synth |
| `sus_pedal` | Sustain Pedal Holds | option: `Off` / `On` | `On` | per Synth |

### MASTER

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `master_level` | Master Level | number 0 to 100 %, whole numbers | `100` | shared |
| `seq_all` | Seq Play All | option: `Off` / `On` | `Off` | shared |

### EXT IN

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `ext_level` | Ext In Level | number 0 to 100 %, whole numbers | `0` | per Synth |
| `ext_route` | Ext In Route | option: `None` / `SVF` / `Ladder` / `Both` | `Ladder` | per Synth |

### SYNTH FX VOCODER

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modfx_sibil` | Synth FX Sibilance | number 0 to 100 %, whole numbers | `30` | per Synth |
| `modfx_sens` | Synth FX Sensitivity | number 0 to 100 %, whole numbers | `50` | per Synth |

### LIFE

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `life_on` | Life On | option: `Off` / `On` | `Off` | per Synth |
| `life_keys` | Life Keys | option: `Free` / `Held Keys` / `Key Launch` | `Free` | per Synth |
| `life_rate` | Life Rate | option: `1/4` / `1/8` / `1/16` / `1/32` / `1/8T` / `1/16T` / `Free` | `1/8` | per Synth |
| `life_speed` | Life Free Rate | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `40` | per Synth |
| `life_mode` | Life Plays | option: `Births` / `Scan Column` / `Scan Row` | `Scan Column` | per Synth |
| `life_rule` | Life Rule | option: `Life` / `HighLife` / `Seeds` | `Life` | per Synth |
| `life_density` | Life Density | number 5 to 80 %, whole numbers | `30` | per Synth |
| `life_seed` | Life Seed | number 0 to 99, whole numbers | `1` | per Synth |
| `life_reseed` | Life Reseed | option: `Off` / `On` | `Off` | per Synth |
| `life_wrap` | Life Wrap | option: `Off` / `On` | `On` | per Synth |
| `life_notes` | Life Notes/Step | number 1 to 8, whole numbers | `2` | per Synth |
| `life_len` | Life Note Length | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `45` | per Synth |
| `life_octave` | Life Low Octave | option: `C1` / `C2` / `C3` / `C4` / `C5` / `C6` | `C3` | per Synth |
| `life_range` | Life Octaves | option: `1` / `2` / `3` / `4` | `2` | per Synth |
| `life_max` | Life Max Notes/s | number 1 to 30 /s, whole numbers | `8` | per Synth |

### ORBITS

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `orb_on` | Orbits On | option: `Off` / `On` | `Off` | per Synth |
| `orb_keys` | Orbit Keys | option: `Free` / `Held Keys` / `Key Launch` | `Free` | per Synth |
| `orb_planets` | Planets | number 1 to 6, whole numbers | `3` | per Synth |
| `orb_speed` | Orbit Speed | number 0 to 100 %, whole numbers | `50` | per Synth |
| `orb_gravity` | Orbit Gravity | number 0 to 100 %, whole numbers | `0` | per Synth |
| `orb_ecc` | Eccentricity | number 0 to 80 %, whole numbers | `20` | per Synth |
| `orb_spread` | Orbit Spread | number 0 to 100 %, whole numbers | `60` | per Synth |
| `orb_spokes` | Spokes | number 1 to 8, whole numbers | `4` | per Synth |
| `orb_planet_to` | Planets As | option: `Octave` / `Velocity` | `Octave` | per Synth |
| `orb_quant` | Orbit Sync | option: `Off` / `1/4` / `1/8` / `1/16` / `1/32` / `1/8T` / `1/16T` | `1/16` | per Synth |
| `orb_len` | Orbit Note Length | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `45` | per Synth |
| `orb_octave` | Orbit Low Octave | option: `C1` / `C2` / `C3` / `C4` / `C5` / `C6` | `C3` | per Synth |
| `orb_range` | Orbit Octaves | option: `1` / `2` / `3` / `4` | `2` | per Synth |
| `orb_max` | Orbit Max Notes/s | number 1 to 30 /s, whole numbers | `8` | per Synth |

### PENDULUMS

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `pend_on` | Pendulums On | option: `Off` / `On` | `Off` | per Synth |
| `pend_keys` | Pendulum Keys | option: `Free` / `Held Keys` / `Key Launch` | `Free` | per Synth |
| `pend_count` | Pendulums | number 2 to 12, whole numbers | `5` | per Synth |
| `pend_cycle` | Pendulum Cycle | option: `1 Bar` / `2 Bars` / `4 Bars` / `8 Bars` / `16 Bars` / `32 Bars` | `8 Bars` | per Synth |
| `pend_speed` | Pendulum Swings | number 1 to 32, whole numbers | `4` | per Synth |
| `pend_spread` | Pendulum Spread | number 1 to 8, whole numbers | `1` | per Synth |
| `pend_damp` | Pendulum Damping | number 0 to 100 %, whole numbers | `0` | per Synth |
| `pend_trigger` | Pendulums Play | option: `Centre` / `Ends` / `Both` | `Centre` | per Synth |
| `pend_restart` | Pendulum Key Restart | option: `Off` / `On` | `Off` | per Synth |
| `pend_len` | Pendulum Note Length | number 0 to 100 (knob position; the screen shows it in ms / Hz / steps) | `45` | per Synth |
| `pend_octave` | Pendulum Low Octave | option: `C1` / `C2` / `C3` / `C4` / `C5` / `C6` | `C3` | per Synth |
| `pend_max` | Pendulum Max Notes/s | number 1 to 30 /s, whole numbers | `8` | per Synth |
| `pend_dyn` | Pendulum Dynamics | number 0 to 100 %, whole numbers | `60` | per Synth |

### OTHER

| Key | Name | Value | Default | Scope |
|---|---|---|---|---|
| `modN_on` (N = 1..16) | Mod N On | option: `Off` / `On` | `On` | per Synth |
| `pad_polarity` | Pad Polarity | option: `Unipolar` / `Bipolar` | `Unipolar` | per Synth |
| `pad_hold` | Pad X/Y Hold | option: `Off` / `On` | `On` | per Synth |
| `load_pos` | Load Saved Pos | option: `Off` / `Mod Wheel` / `Pad` / `Both` | `Off` | per Synth |
| `wheel_pos` | Saved Mod Wheel | number 0 to 100 %, whole numbers | `0` | per Synth |
| `pad_x_pos` | Saved Pad X | number 0 to 100 %, whole numbers | `0` | per Synth |
| `pad_y_pos` | Saved Pad Y | number 0 to 100 %, whole numbers | `0` | per Synth |
| `eg_times` | EG Times | option: `1 ms, 11 Syncs` / `MIN, 13 Syncs` | `1 ms, 11 Syncs` | per Synth |
| `arp_follow_swing` | Arp Follow Swing | option: `Off` / `On` | `Off` | per Synth |
| `seq_host_ss` | Seq Host Start/Stop | option: `Off` / `On` | `Off` | per Synth |
| `link_key` | Link Key | option: `Off` / `On` | `Off` | shared |
| `link_scale` | Link Scale | option: `Off` / `On` | `Off` | shared |
| `link_arp` | Link Arp Rate | option: `Off` / `On` | `Off` | shared |
| `link_seq` | Link Seq Rate | option: `Off` / `On` | `Off` | shared |
| `link_lfo1` | Link LFO 1 Rate | option: `Off` / `On` | `Off` | shared |
| `link_lfo2` | Link LFO 2 Rate | option: `Off` / `On` | `Off` | shared |
| `link_lfo3` | Link LFO 3 Rate | option: `Off` / `On` | `Off` | shared |
| `link_lfo4` | Link LFO 4 Rate | option: `Off` / `On` | `Off` | shared |
| `link_phys` | Link Balls/Particles | option: `Off` / `On` | `Off` | shared |
| `link_send` | Link FX Sends | option: `Off` / `On` | `On` | shared |
| `link_cut` | Link Cutoffs (3 Synths) | option: `Off` / `On` | `Off` | shared |
| `phys_floor` | Phys Floor | option: `Plays` / `Silent` / `Open` | `Plays` | per Synth |
| `phys_ceil` | Phys Ceiling | option: `Plays` / `Silent` / `Open` | `Plays` | per Synth |
| `phys_left` | Phys Left Wall | option: `Plays` / `Silent` / `Open` | `Plays` | per Synth |
| `phys_right` | Phys Right Wall | option: `Plays` / `Silent` / `Open` | `Plays` | per Synth |
| `phys_follow_oct` | Phys Follow Octave | option: `Off` / `On` | `Off` | per Synth |
| `seq_output` | Seq Output | option: `Own` / `Synth 1` / `Synth 2` / `Synth 3` / `1+2` / `1+3` / `2+3` / `1+2+3` | `Own` | per Synth |
| `dly_on` | FX1 On | option: `Off` / `On` | `On` | shared |
| `rev_on` | FX2 On | option: `Off` / `On` | `On` | shared |
| `modfx_spray` | Synth FX Spray | number 0 to 100 %, whole numbers | `15` | per Synth |
| `modfx_prand` | Synth FX Pitch Random | number 0 to 100 %, whole numbers | `10` | per Synth |
| `modfx_reverse` | Synth FX Reverse | number 0 to 100 %, whole numbers | `0` | per Synth |
| `modfx_gquant` | Synth FX Pitch Quantise | option: `Free` / `Semitones` / `Octaves` / `Oct + 5ths` | `Free` | per Synth |
| `dly_gpos` | FX1 Grain Position | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_gdens` | FX1 Grain Density | number 0 to 100 %, whole numbers | `70` | shared |
| `dly_genv` | FX1 Grain Envelope | number 0 to 100 %, whole numbers | `50` | shared |
| `dly_gspray` | FX1 Spray | number 0 to 100 %, whole numbers | `15` | shared |
| `dly_gprand` | FX1 Pitch Random | number 0 to 100 %, whole numbers | `10` | shared |
| `dly_greverse` | FX1 Reverse | number 0 to 100 %, whole numbers | `0` | shared |
| `dly_gquant` | FX1 Pitch Quantise | option: `Free` / `Semitones` / `Octaves` / `Oct + 5ths` | `Free` | shared |
| `rev_gpos` | FX2 Grain Position | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_gdens` | FX2 Grain Density | number 0 to 100 %, whole numbers | `70` | shared |
| `rev_genv` | FX2 Grain Envelope | number 0 to 100 %, whole numbers | `50` | shared |
| `rev_gspray` | FX2 Spray | number 0 to 100 %, whole numbers | `15` | shared |
| `rev_gprand` | FX2 Pitch Random | number 0 to 100 %, whole numbers | `10` | shared |
| `rev_greverse` | FX2 Reverse | number 0 to 100 %, whole numbers | `0` | shared |
| `rev_gquant` | FX2 Pitch Quantise | option: `Free` / `Semitones` / `Octaves` / `Oct + 5ths` | `Free` | shared |
| `link_pad` | Link X/Y Pad (3 Synths) | option: `Off` / `On` | `Off` | shared |
| `lfo1_share` | Share LFO 1 (3 Synths) | option: `Off` / `On` | `Off` | shared |
| `lfo2_share` | Share LFO 2 (3 Synths) | option: `Off` / `On` | `Off` | shared |
| `lfo3_share` | Share LFO 3 (3 Synths) | option: `Off` / `On` | `Off` | shared |
| `lfo4_share` | Share LFO 4 (3 Synths) | option: `Off` / `On` | `Off` | shared |

## Appendix B: the long option lists

Write the label exactly as shown (case doesn't matter), or its number: the first entry is 0.

### SOURCES

0 `Off`, 1 `Filter Env`, 2 `Amp Env`, 3 `Mod Env`, 4 `LFO 1`, 5 `LFO 2`, 6 `Velocity`, 7 `Mod Wheel`, 8 `Aftertouch`, 9 `Keyboard`, 10 `Random`, 11 `LFO 3`, 12 `LFO 4`, 13 `Pitch Wheel`, 14 `KB Gate`, 15 `Sustain`, 16 `Exp 1 (CC 11)`, 17 `Exp 2 (CC 4)`, 18 `Random Uni`, 19 `Constant`, 20 `Noise EG`, 21 `Sostenuto`, 22 `Ribbon`, 23 `Pad X (CC 2)`, 24 `Pad Y (CC 3)`, 25 `Pad Gate (CC 18)`, 26 `Rain Drop`, 27 `Ripple Energy`, 28 `Markov Step`, 29 `Markov Height`, 30 `Ball 1 Height`, 31 `Ball 1 X`, 32 `Balls Speed`, 33 `Ball Hit`, 34 `Particle Count`, 35 `Particle Hit`, 36 `Turing Value`, 37 `Euclid Gate`, 38 `Life Population`, 39 `Orbit 1 X`, 40 `Orbit 1 Y`, 41 `Pendulum 1`, 42 `Pendulum Sync`

### DESTINATIONS

0 `Off`, 1 `Pitch`, 2 `Osc 1 Pitch`, 3 `Osc 2 Pitch`, 4 `Osc 3 Pitch`, 5 `Osc 1 Level`, 6 `Osc 2 Level`, 7 `Osc 3 Level`, 8 `Noise Level`, 9 `Ring Level`, 10 `Osc 1 Angle`, 11 `Osc 2 Angle`, 12 `Osc 3 Angle`, 13 `Osc 1 PW`, 14 `Osc 2 PW`, 15 `Osc 3 PW`, 16 `Osc 1 Wave Mix`, 17 `Osc 2 Wave Mix`, 18 `Osc 3 Wave Mix`, 19 `FM Amount`, 20 `Ladder Cutoff`, 21 `Ladder Res`, 22 `Ladder Drive`, 23 `SVF Cutoff`, 24 `SVF Res`, 25 `Filter Mix`, 26 `Amp Level`, 27 `Pitch LFO1 Amt`, 28 `Pitch EG Amt`, 29 `Angle LFO3 Amt`, 30 `PW LFO3 Amt`, 31 `Osc 1 Beat`, 32 `Osc 2 Beat`, 33 `Osc 3 Beat`, 34 `SVF Spacing`, 35 `SVF EG Amt`, 36 `SVF LFO2 Amt`, 37 `SVF FM Amt`, 38 `Ladder EG Amt`, 39 `Ladder LFO2 Amt`, 40 `Ladder FM Amt`, 41 `Filter Env Delay`, 42 `Filter Env Attack`, 43 `Filter Env Hold`, 44 `Filter Env Decay`, 45 `Filter Env Sustain`, 46 `Filter Env Release`, 47 `Filter Env Time Scale`, 48 `Amp Env Delay`, 49 `Amp Env Attack`, 50 `Amp Env Hold`, 51 `Amp Env Decay`, 52 `Amp Env Sustain`, 53 `Amp Env Release`, 54 `Amp Env Time Scale`, 55 `Mod Env Delay`, 56 `Mod Env Attack`, 57 `Mod Env Hold`, 58 `Mod Env Decay`, 59 `Mod Env Sustain`, 60 `Mod Env Release`, 61 `Mod Env Time Scale`, 62 `LFO 1 Rate`, 63 `LFO 1 Rate (st)`, 64 `LFO 1 Variation`, 65 `LFO 1 Delay`, 66 `LFO 1 Fade In`, 67 `LFO 1 Fade Out`, 68 `LFO 2 Rate`, 69 `LFO 2 Rate (st)`, 70 `LFO 2 Variation`, 71 `LFO 2 Delay`, 72 `LFO 2 Fade In`, 73 `LFO 2 Fade Out`, 74 `LFO 3 Rate`, 75 `LFO 3 Rate (st)`, 76 `LFO 3 Variation`, 77 `LFO 3 Delay`, 78 `LFO 3 Fade In`, 79 `LFO 3 Fade Out`, 80 `LFO 4 Rate`, 81 `LFO 4 Rate (st)`, 82 `LFO 4 Variation`, 83 `LFO 4 Delay`, 84 `LFO 4 Fade In`, 85 `LFO 4 Fade Out`, 86 `Pan`, 87 `Noise Color Mix`, 88 `Analog Drift`, 89 `Spread`, 90 `Synth FX Mix`, 91 `Synth FX Gain`, 92 `Synth FX CTRL 1`, 93 `Synth FX CTRL 2`, 94 `Synth FX Rate`, 95 `Synth FX Rate R`, 96 `Synth FX Depth`, 97 `Synth FX Feedback`, 98 `Synth FX Feedback R`, 99 `Synth FX Time`, 100 `Synth FX Time R`, 101 `Synth FX Delay Time`, 102 `Synth FX Delay Feedback`, 103 `Synth FX Tone`, 104 `Synth FX Spread`, 105 `Synth FX Sweep`, 106 `Synth FX Noise`, 107 `Synth FX Vibrato Rate`, 108 `Synth FX Vibrato Depth`, 109 `Synth FX Flutter`, 110 `Synth FX Wow`, 111 `Synth FX Tape Bias`, 112 `Synth FX Echo Room`, 113 `Synth FX Echo Decay`, 114 `Synth FX Bit Depth`, 115 `Synth FX Sample Rate`, 116 `Synth FX Resonance`, 117 `Synth FX Cutoff Low`, 118 `Synth FX Cutoff Mid`, 119 `Synth FX Cutoff High`, 120 `Synth FX Level Low`, 121 `Synth FX Level Mid`, 122 `Synth FX Level High`, 123 `Send 1 (FX1)`, 124 `Send 2 (FX2)`, 125 `FX1 > FX2 Series`, 126 `Master FX Width`, 127 `FX1 Time L`, 128 `FX1 Time R`, 129 `FX1 Feedback`, 130 `FX1 Tone`, 131 `FX1 Offset`, 132 `FX2 Decay`, 133 `FX2 Size`, 134 `FX2 Pre-delay`, 135 `FX2 High Damp`, 136 `FX2 Low Damp`, 137 `FX2 Diffusion`, 138 `FX2 Tone`, 139 `FX2 Mod Level`, 140 `FX2 Early/Late`, 141 `Synth FX Pitch`, 142 `Synth FX Structure`, 143 `Synth FX Position`, 144 `Synth FX Size`, 145 `Synth FX Density`, 146 `Synth FX Texture`, 147 `Synth FX Drive`, 148 `Synth FX Head Bump`, 149 `Synth FX Low Cut`, 150 `Synth FX Delay Time R`, 151 `Synth FX Depth R`, 152 `Synth FX Offset`, 153 `Synth FX Feedback Left`, 154 `Synth FX Feedback Right`, 155 `Synth FX Crossover`, 156 `FX1 Mix`, 157 `FX1 Gain`, 158 `FX1 Feedback R`, 159 `FX1 Feedback Left`, 160 `FX1 Feedback Right`, 161 `FX1 Rate L`, 162 `FX1 Rate R`, 163 `FX1 Depth L`, 164 `FX1 Depth R`, 165 `FX1 Spread`, 166 `FX1 Noise`, 167 `FX1 Sweep`, 168 `FX1 Delay Feedback`, 169 `FX1 Vibrato Rate`, 170 `FX1 Vibrato Depth`, 171 `FX1 Flutter`, 172 `FX1 Wow`, 173 `FX1 Tape Bias`, 174 `FX1 Echo Room`, 175 `FX1 Decay`, 176 `FX1 Size`, 177 `FX1 Pre-delay`, 178 `FX1 High Damp`, 179 `FX1 Low Damp`, 180 `FX1 Diffusion`, 181 `FX1 Mod Level`, 182 `FX1 Early/Late`, 183 `FX1 Distance`, 184 `FX1 High Freq`, 185 `FX1 High Level`, 186 `FX1 Mid Level`, 187 `FX1 Low Level`, 188 `FX1 Low Decay`, 189 `FX1 High Decay`, 190 `FX1 Pitch A`, 191 `FX1 Pitch B`, 192 `FX1 Pitch Decay`, 193 `FX1 Gravity`, 194 `FX1 Crossover`, 195 `FX1 Bit Depth`, 196 `FX1 Sample Rate`, 197 `FX1 Resonance`, 198 `FX1 Cutoff Low`, 199 `FX1 Cutoff Mid`, 200 `FX1 Cutoff High`, 201 `FX1 Level Low`, 202 `FX1 Level Mid`, 203 `FX1 Level High`, 204 `FX2 Mix`, 205 `FX2 Gain`, 206 `FX2 Time L`, 207 `FX2 Time R`, 208 `FX2 Feedback`, 209 `FX2 Feedback R`, 210 `FX2 Feedback Left`, 211 `FX2 Feedback Right`, 212 `FX2 Rate L`, 213 `FX2 Rate R`, 214 `FX2 Depth L`, 215 `FX2 Depth R`, 216 `FX2 Offset`, 217 `FX2 Spread`, 218 `FX2 Noise`, 219 `FX2 Sweep`, 220 `FX2 Delay Feedback`, 221 `FX2 Vibrato Rate`, 222 `FX2 Vibrato Depth`, 223 `FX2 Flutter`, 224 `FX2 Wow`, 225 `FX2 Tape Bias`, 226 `FX2 Echo Room`, 227 `FX2 Distance`, 228 `FX2 High Freq`, 229 `FX2 High Level`, 230 `FX2 Mid Level`, 231 `FX2 Low Level`, 232 `FX2 Low Decay`, 233 `FX2 High Decay`, 234 `FX2 Pitch A`, 235 `FX2 Pitch B`, 236 `FX2 Pitch Decay`, 237 `FX2 Gravity`, 238 `FX2 Crossover`, 239 `FX2 Bit Depth`, 240 `FX2 Sample Rate`, 241 `FX2 Resonance`, 242 `FX2 Cutoff Low`, 243 `FX2 Cutoff Mid`, 244 `FX2 Cutoff High`, 245 `FX2 Level Low`, 246 `FX2 Level Mid`, 247 `FX2 Level High`, 248 `Macro Harmonics`, 249 `Macro Timbre`, 250 `Macro Morph`, 251 `Macro Level`, 252 `Ext Level`, 253 `Osc 1 Octave`, 254 `Osc 2 Octave`, 255 `Osc 3 Octave`, 256 `Noise EG Attack`, 257 `Noise EG Sustain`, 258 `Noise EG Release`, 259 `Filter Env Multi Trig`, 260 `Filter Env Sync`, 261 `Filter Env Loop`, 262 `Filter Env Latch`, 263 `Amp Env Multi Trig`, 264 `Amp Env Sync`, 265 `Amp Env Loop`, 266 `Amp Env Latch`, 267 `Mod Env Multi Trig`, 268 `Mod Env Sync`, 269 `Mod Env Loop`, 270 `Mod Env Latch`, 271 `LFO 1 Wave`, 272 `LFO 2 Wave`, 273 `LFO 3 Wave`, 274 `LFO 4 Wave`, 275 `Synth FX Spray`, 276 `Synth FX Pitch Random`, 277 `Synth FX Reverse`, 278 `FX1 Grain Position`, 279 `FX1 Grain Density`, 280 `FX1 Grain Envelope`, 281 `FX1 Spray`, 282 `FX1 Pitch Random`, 283 `FX1 Reverse`, 284 `FX2 Grain Position`, 285 `FX2 Grain Density`, 286 `FX2 Grain Envelope`, 287 `FX2 Spray`, 288 `FX2 Pitch Random`, 289 `FX2 Reverse`, 290 `Synth FX Sibilance`, 291 `Synth FX Sensitivity`, 292 `FX1 Sibilance`, 293 `FX1 Sensitivity`, 294 `FX2 Sibilance`, 295 `FX2 Sensitivity`

### VIA SOURCES

0 `None`, 1 `Filter Env`, 2 `Amp Env`, 3 `Mod Env`, 4 `LFO 1`, 5 `LFO 2`, 6 `Velocity`, 7 `Mod Wheel`, 8 `Aftertouch`, 9 `Keyboard`, 10 `Random`, 11 `LFO 3`, 12 `LFO 4`, 13 `Pitch Wheel`, 14 `KB Gate`, 15 `Sustain`, 16 `Exp 1 (CC 11)`, 17 `Exp 2 (CC 4)`, 18 `Random Uni`, 19 `Constant`, 20 `Noise EG`, 21 `Sostenuto`, 22 `Ribbon`, 23 `Pad X (CC 2)`, 24 `Pad Y (CC 3)`, 25 `Pad Gate (CC 18)`, 26 `Rain Drop`, 27 `Ripple Energy`, 28 `Markov Step`, 29 `Markov Height`, 30 `Ball 1 Height`, 31 `Ball 1 X`, 32 `Balls Speed`, 33 `Ball Hit`, 34 `Particle Count`, 35 `Particle Hit`, 36 `Turing Value`, 37 `Euclid Gate`, 38 `Life Population`, 39 `Orbit 1 X`, 40 `Orbit 1 Y`, 41 `Pendulum 1`, 42 `Pendulum Sync`

### LIST 4 (first used by lfo1_repeats)

0 `Inf`, 1 `1`, 2 `2`, 3 `3`, 4 `4`, 5 `5`, 6 `6`, 7 `7`, 8 `8`, 9 `9`, 10 `10`, 11 `11`, 12 `12`, 13 `13`, 14 `14`, 15 `15`, 16 `16`, 17 `17`, 18 `18`, 19 `19`, 20 `20`, 21 `21`, 22 `22`, 23 `23`, 24 `24`, 25 `25`, 26 `26`, 27 `27`, 28 `28`, 29 `29`, 30 `30`, 31 `31`, 32 `32`

### LIST 5 (first used by modfx_type)

0 `Delay`, 1 `BBD Delay`, 2 `Tape Delay`, 3 `Echo`, 4 `Flanger`, 5 `Stereo Flanger`, 6 `Chorus`, 7 `Dual Chorus`, 8 `Ensemble`, 9 `Stereo Chorus`, 10 `6-Stage Phaser`, 11 `12-Stage Phaser`, 12 `Time Phaser`, 13 `Bit Crusher`, 14 `Resonator`, 15 `Modal Resonator`, 16 `Granular`, 17 `Tape Color`, 18 `Density`, 19 `Console`, 20 `Stereo Delay`, 21 `Ping Pong`, 22 `10 Band Vocoder`, 23 `16 Band Vocoder`, 24 `Grains`

### LIST 6 (first used by modfx_c1_dst)

0 `Auto`, 1 `Mix`, 2 `Feedback`, 3 `Feedback R`, 4 `Time`, 5 `Time R`, 6 `Delay Time`, 7 `Delay FB`, 8 `Rate`, 9 `Rate R`, 10 `Depth`, 11 `Tone`, 12 `Spread`, 13 `Gain`, 14 `Sweep`, 15 `Noise`, 16 `Vib Rate`, 17 `Vib Depth`, 18 `Flutter`, 19 `Wow`, 20 `Bias`, 21 `Room`, 22 `Decay`, 23 `Bits`, 24 `Sample Rate`, 25 `Resonance`, 26 `Cutoff Low`, 27 `Cutoff Mid`, 28 `Cutoff High`, 29 `Pitch`, 30 `Structure`, 31 `Position`, 32 `Size`, 33 `Density`, 34 `Texture`, 35 `Drive`, 36 `Head Bump`, 37 `Low Cut`, 38 `Delay Time R`, 39 `Depth R`, 40 `Offset`, 41 `Feedback Left`, 42 `Feedback Right`, 43 `Crossover`, 44 `Sibilance`, 45 `Sensitivity`, 46 `Spray`, 47 `Pitch Random`, 48 `Reverse`

### LIST 7 (first used by dly_type)

0 `Plate`, 1 `Room`, 2 `Hall`, 3 `Space`, 4 `Shimmer`, 5 `Blackhole`, 6 `Stereo Delay`, 7 `Ping Pong`, 8 `Delay`, 9 `BBD Delay`, 10 `Tape Delay`, 11 `Echo`, 12 `Flanger`, 13 `Stereo Flanger`, 14 `Chorus`, 15 `Dual Chorus`, 16 `Ensemble`, 17 `Stereo Chorus`, 18 `6-Stage Phaser`, 19 `12-Stage Phaser`, 20 `Time Phaser`, 21 `Bit Crusher`, 22 `Resonator`, 23 `10 Band Vocoder`, 24 `16 Band Vocoder`, 25 `Grains`

