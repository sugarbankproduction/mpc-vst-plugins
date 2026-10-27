# Third-party code in ONE-ish

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
