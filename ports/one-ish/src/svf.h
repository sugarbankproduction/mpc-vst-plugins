/* State-variable filter, 12 dB per octave: lowpass, bandpass, highpass or notch from one structure.
 * Model: the trapezoidal (TPT) SVF, as in V. Zavalishin, "The Art of VA Filter Design" (ch. 4) and A. Simper's openly
 * published "Linear Trap Integrated SVF" notes: stable at any cutoff and resonance, in tune up to its top cutoff.
 * Linear: resonance raises the peak at the cutoff up to +20 dB but does not self-oscillate (the ladder does that).
 * About ten multiplies per sample, no allocation: safe in the audio path. */
#pragma once
#include <math.h>

#define SVF_FC_MAX 0.45f   /* top cutoff as a share of the sample rate */
#define SVF_K_MIN 0.1f     /* damping at 100% resonance: Q 10, a +20 dB peak */

enum { SVF_LP, SVF_BP, SVF_HP, SVF_NOTCH };   /* option order in params.json */

typedef struct {
    float ic1, ic2;            /* integrator states */
    float a1, a2, a3, k;
    float m0, m1, m2;          /* output = m0*x + m1*band + m2*low, for the mode */
} svf_t;

static inline void svf_reset(svf_t *f) { f->ic1 = f->ic2 = 0.0f; }

/* cutoff in Hz, resonance 0..1, mode SVF_LP.. Call outside the per-sample loop (one tanf). */
static inline void svf_set(svf_t *f, float hz, float reso, int mode, float sr) {
    float fc = hz < 1.0f ? 1.0f : hz > SVF_FC_MAX * sr ? SVF_FC_MAX * sr : hz;
    float g = tanf(3.14159265f * fc / sr);
    float k = 2.0f - (2.0f - SVF_K_MIN) * (reso < 0.0f ? 0.0f : reso > 1.0f ? 1.0f : reso);
    f->k = k;
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
    switch (mode) {
    case SVF_BP:    f->m0 = 0; f->m1 = 1.0f + 0.5f * k; f->m2 = 0; break;   /* peak (1 + k/2)/k: 0 dB at no resonance,
                                                                          +20 dB at full, like LP and HP */
    case SVF_HP:    f->m0 = 1; f->m1 = -k;     f->m2 = -1.0f; break;
    case SVF_NOTCH: f->m0 = 1; f->m1 = -k;     f->m2 = 0;     break;
    default:        f->m0 = 0; f->m1 = 0;      f->m2 = 1.0f;  break;
    }
}

static inline float svf_next(svf_t *f, float x) {
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;   /* band */
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;   /* low */
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return f->m0 * x + f->m1 * v1 + f->m2 * v2;
}
