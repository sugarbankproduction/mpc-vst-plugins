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

Two files are changed. The changed copies live under `patched/`, which is first on the include path
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

The build also defines `TEST` (`vst.json` cflags), which stmlib uses to pick its portable C code instead of ARM
Cortex-M4 assembly and RAM-section attributes; nothing in ONE-ish's own code reads it.

### Licence

```
Copyright 2012-2016 Emilie Gillet.

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
