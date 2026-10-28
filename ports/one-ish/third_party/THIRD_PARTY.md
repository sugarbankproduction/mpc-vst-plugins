# Third-party code in ONE-ish

ONE-ish's own code is under the repository's licence. The code in this folder comes from other projects, under the
licences below. Each copied file keeps its own copyright header.

## Mutable Instruments Plaits and stmlib (MIT)

The MACRO channel (`src/macro.h`, `src/macro.cc`) runs the synthesis models of Plaits, the macro-oscillator module by
Mutable Instruments, with the parts of stmlib (its support library) that they use.

- Plaits: <https://github.com/pichenettes/eurorack> (`plaits/`), commit `f4cfa7697095a28d292bff758beb51deb49b5a86`
- stmlib: <https://github.com/pichenettes/stmlib>, commit `d18def816c51d1da0c108236928b2bbd25c17481`
- Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com)

What was copied: from Plaits, `plaits/dsp/` (the engines of both banks, oscillators, drums, physical models, speech,
6-op FM, noise, the effects some engines use) and `plaits/resources.cc` / `resources.h` (lookup tables, wavetables,
the three 6-op FM factory banks); from stmlib, `stmlib.h`, the `dsp/` and `utils/` headers they include, `dsp/units.cc`
and `utils/random.cc`. Only files the MACRO channel compiles or includes; not copied: Plaits' `voice.cc` / `voice.h`
(src/macro.cc does that job), its low-pass gate and decay envelope, its hardware drivers, UI, settings, user-data and
bootloader code, and its tests. Every copied file carries the MIT notice below; the files under `plaits/` and
`stmlib/` are unmodified.

### Local changes

Three files are changed. The changed copies live under `patched/`, which is first on the include path
(`-Iports/one-ish/third_party/patched` before `-Iports/one-ish/third_party` in `vst.json`), so every
`#include "plaits/..."` picks them up; the originals stay unmodified beside them.

1. `patched/plaits/dsp/dsp.h`: `kSampleRate` and `kCorrectedSampleRate` are 44100 (were 48000 and 47872.34, the
   module's I2S frame rate). Plaits runs at ONE-ish's 44.1 kHz: its tuning (`a0`, `NoteToFrequency`) and every time
   constant written in terms of these follow (drum pulse and decay times, the hi-hat's filters, the string's damping,
   the speech frame rate, the 6-op FM envelopes and LFO). Measured offline: VA, wavetable and additive within 0.1 cent
   of the note. A few constants written as plain numbers per sample (some smoothing and envelope coefficients, the
   diffuser's 0.3 Hz LFO in `fx/diffuser.h`, which assumes 48 kHz) now run 8% slower than on the module; inaudible in
   practice.
2. `patched/plaits/dsp/fm/dx_units.h`: in `Pow2Fast`, `r.w += x_integral << 23;` is worked out in unsigned arithmetic
   (the same bits). The original shifts a negative number left, undefined in C++11, which the offline checks'
   UndefinedBehaviorSanitizer reports.
3. `patched/plaits/dsp/speech/lpc_speech_synth.h`: `PlayFrame(frames, frame, interpolate)` with `interpolate`
   false passes the same frame twice instead of reading the next one at blend 0 (unused). The original read one frame
   past `LPCSpeechSynthController::phonemes_` when a note-on picked the last consonant (Speech with harmonics about
   17-33%), which AddressSanitizer reports as a global-buffer-overflow (2026-10-27, offline). The sound is the same.

The build also defines `TEST` (`vst.json` cflags), which stmlib uses to pick its portable C code instead of ARM
Cortex-M4 assembly and RAM-section attributes; nothing in ONE-ish's own code reads it.

### Licence

```
Copyright 2012-2016 Emilie Gillet.
Everything here is under the MIT licence. Each vendored file keeps its own copyright header; the notices below cover
the copies, the modified copies under `patched/` and the ports. ONE-ish's own code is under the repository's licence.

## Mutable Instruments Rings (the "Modal Resonator" synth effect)

- Source: <https://github.com/pichenettes/eurorack>, commit `f4cfa7697095a28d292bff758beb51deb49b5a86`, folder `rings/`.
- Copied unmodified (line endings as in the repository, LF): `rings/dsp/` (part, string, resonator, FM voice, their
  headers, the limiter, note filter, plucker, follower, `fx/fx_engine.h`, `fx/reverb.h`) and `rings/resources.{h,cc}`
  (its lookup tables). The string synth "easter egg" and the hardware files are not copied.
- Used through `src/fx/mi_effects.{h,cc}`: polyphony 1, the voices as its external exciter input, run at 44.1 kHz
  with its tonic raised 1.468 semitones (it is written for 48 kHz).

## Mutable Instruments Clouds (the "Granular" synth effect)

- Source: the same repository and commit, folder `clouds/`.
- Copied unmodified: `clouds/dsp/` (granular processor, the four players, phase vocoder, `fx/`), `clouds/resources.{h,cc}`
  and `clouds/drivers/debug_pin.h` (built with `-DTEST`, which makes it empty).
- Modified copies, built or found in place of the originals (`patched/` comes first on the include path; every
  change is marked "ONE-ish patch" in the file):
  - `patched/clouds/dsp/correlator.cc`: a shift by 32 bits (undefined in C++) spelled out as the module's processor
    does it (0).
  - `patched/clouds/dsp/looping_sample_player.h`: the read position's fractional part taken with an unsigned shift
    (the original's signed shift overflows, undefined in C++; same value).
- Used through `src/fx/mi_effects.{h,cc}`: run at 44.1 kHz (written for 32 kHz) with larger recording buffers.

Copyright notice of Rings and Clouds (as in each file's header):

```
Copyright 2014-2015 Emilie Gillet.

Author: Emilie Gillet (emilie.o.gillet@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## stmlib (Mutable Instruments' support library, used by Rings and Clouds)

- Source: <https://github.com/pichenettes/stmlib>, commit `d18def816c51d1da0c108236928b2bbd25c17481`.
- Copied unmodified: `stmlib.h`, `LICENSE`, `dsp/` (`dsp.h`, `filter.h`, `delay_line.h`, `cosine_oscillator.h`,
  `units.{h,cc}`, `parameter_interpolator.h`, `atan.{h,cc}`, `rsqrt.h`), `utils/` (`random.{h,cc}`,
  `buffer_allocator.h`, `dsp.h`), `fft/shy_fft.h`. Built with `-DTEST` (its portable code, not the Cortex-M assembly).
- Licence: MIT, Copyright 2012-2015 Emilie Gillet; see `stmlib/LICENSE` and each file's header (the same notice as
  above).

## Airwindows (the "Tape Color", "Density" and "Console" synth effects, the "Space" reverb)

- Source: <https://github.com/airwindows/airwindows>, commit `dfa54018e788fd67b8e57ae6c8012a05f1f086a6`,
  `plugins/LinuxVST/src/<Name>/<Name>Proc.cpp` (`processReplacing`) of ToTape6, Density, Console7Channel,
  Console7Buss and Galactic.
- Not copied: ported to plain C in `src/fx/airwindows.h` (the algorithms as written, in double precision; the
  changes, none audible, are listed at the top of that file). Offline tests compare the ports with the original
  plugins' output (`test/offline_check.c`, `oss_effects`).
- Licence (the repository's `LICENSE`, repeated in `src/fx/airwindows.h`):

```
MIT License

Copyright (c) 2018 Chris Johnson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
