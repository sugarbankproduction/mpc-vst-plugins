/* State-variable filter, 12 dB per octave: lowpass, bandpass, highpass or notch from one structure, and the pair of
 * them that the synth uses as one filter (svf2_t below).
 * Model: the trapezoidal (TPT) SVF, as in V. Zavalishin, "The Art of VA Filter Design" (ch. 4) and A. Simper's openly
 * published "Linear Trap Integrated SVF" notes: stable at any cutoff and resonance, in tune up to its top cutoff.
 * Linear: resonance raises the peak at the cutoff up to +20 dB but does not self-oscillate (the ladder does that).
 * About ten multiplies per sample per element, no allocation: safe in the audio path.
 *
 * Non-linear variant (2026-11-02, svf_tick_nl, the ANALOG page's NON-LINEAR SVF): the same zero-delay structure with
 * a damping that rises with the band state's level, D = c v^2 (capped), as a soft limiter across the first integrator
 * would give. Worked out from the state alone (the last sample), so the loop stays linear within the sample and is
 * solved as above: the "cheap" method of Mystran's KVR posts, used here on the band path, on the structure of
 * Zavalishin ch. 4 and 6. A damping term moves the level, hardly the pitch, so the resonance compresses and stays in
 * tune. This is a musically saturating filter, not a model of any one circuit (nobody has published which saturator
 * matches which classic filter). One division and three multiplies more than the linear element; no tanh.
 * Self-oscillation (svf2_set_osc): the damping is allowed just below 0 at the top of the resonance, where D then
 * holds the sine's level: c v^2 averages 0.75 c A^2 over a cycle, so -k = 0.75 c A^2 gives the amplitude A. Both k and
 * c scale with the amount, so A stays about 0.7 at any amount. */
#pragma once
#include <math.h>

#define SVF_FC_MAX 0.45f       /* top cutoff as a share of the sample rate */
#define SVF_G_MAX 6.3137515f   /* tan(pi * SVF_FC_MAX): the top cutoff as the filter's g */
#define SVF_G_MIN 7.1e-5f      /* about 1 Hz at 44.1 kHz */
#define SVF_K_MIN 0.1f         /* damping at 100% resonance: Q 10, a +20 dB peak */
#define SVF_SPACE_OCT 4.0f     /* the pair's spacing at +-100%: element B sits up to 4 octaves above or below A */
#define SVF_NL_C 0.133f        /* the non-linear damping's c at 100%: self-oscillation's amplitude about 0.7 (see above) */
#define SVF_NL_DMAX 8.0f       /* its cap: enough at any level, and a state never freezes at the sample rate's top */
#define SVF_K_OSC (-0.05f)     /* the damping at 100% resonance with SVF SELF-OSC, at 100% amount: past 0 from about 97% */

enum { SVF_LP, SVF_BP, SVF_HP, SVF_NOTCH };   /* option order in params.json */
enum { SVF_SERIES, SVF_PARALLEL };            /* the pair's "svf_pair" options */

typedef struct {
    float ic1, ic2;            /* integrator states */
    float a1, a2, a3, k;
    float m0, m1, m2;          /* output = m0*x + m1*band + m2*low, for the mode */
} svf_t;

static inline void svf_reset(svf_t *f) { f->ic1 = f->ic2 = 0.0f; }

/* the cutoff in Hz as the filter's g (one tanf), held between 1 Hz and the top cutoff */
static inline float svf_g(float hz, float sr) {
    float fc = hz < 1.0f ? 1.0f : hz > SVF_FC_MAX * sr ? SVF_FC_MAX * sr : hz;
    return tanf(3.14159265f * fc / sr);
}

/* resonance 0..1 as damping k: 2 (none) .. SVF_K_MIN */
static inline float svf_damp(float reso) {
    return 2.0f - (2.0f - SVF_K_MIN) * (reso < 0.0f ? 0.0f : reso > 1.0f ? 1.0f : reso);
}

/* a new g with the same damping and mode: one division (the per-sample path of filter FM) */
static inline void svf_set_g(svf_t *f, float g) {
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

static inline void svf_coefs(svf_t *f, float g, float k, int mode) {
    f->k = k;
    svf_set_g(f, g);
    switch (mode) {
    case SVF_BP:    f->m0 = 0; f->m1 = 1.0f + 0.5f * k; f->m2 = 0; break;   /* peak (1 + k/2)/k: 0 dB at no resonance,
                                                                          +20 dB at full, like LP and HP */
    case SVF_HP:    f->m0 = 1; f->m1 = -k;     f->m2 = -1.0f; break;
    case SVF_NOTCH: f->m0 = 1; f->m1 = -k;     f->m2 = 0;     break;
    default:        f->m0 = 0; f->m1 = 0;      f->m2 = 1.0f;  break;
    }
}

/* cutoff in Hz, resonance 0..1, mode SVF_LP.. Call outside the per-sample loop (one tanf). */
static inline void svf_set(svf_t *f, float hz, float reso, int mode, float sr) {
    svf_coefs(f, svf_g(hz, sr), svf_damp(reso), mode);
}

static inline float svf_next(svf_t *f, float x) {
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;   /* band */
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;   /* low */
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return f->m0 * x + f->m1 * v1 + f->m2 * v2;
}

/* Two elements acting as one filter, both in the same mode and resonance. Element A sits at the cutoff; SPACING puts
 * element B up to SVF_SPACE_OCT octaves above or below it.
 *   Series:   A feeds B. LP and HP become 24 dB per octave, BP narrows, the notch deepens; with spacing, BP becomes a
 *             band between the two cutoffs and the notch two notches. Each element gets the square root of the
 *             damping, so the pair's peak at the cutoff matches one element's: -6 dB at no resonance (as the 12 dB
 *             filter) and +20 dB at full, instead of +40.
 *   Parallel: A and B side by side, averaged. At spacing 0 both are the same filter, so the pair is exactly the single
 *             12 dB element (the default: the sound from before the pair existed), worked out once. With spacing,
 *             resonance gives two peaks (LP, BP, HP) and the notch two shallow dips.
 * The Moog One's pair offers 12 and 24 dB slopes; here 12 dB is Parallel at spacing 0 and 24 dB is Series. */
typedef struct {
    svf_t a, b;
    float ga, gb;              /* each element's g at the last control step (filter FM scales them per sample) */
    int series;
    int single;                /* parallel at spacing 0: element A alone is the pair */
    float nl;                  /* the non-linear variant's c (read by svf2_next_nl only) */
} svf2_t;

static inline void svf2_reset(svf2_t *p) { svf_reset(&p->a); svf_reset(&p->b); }

/* cutoff in Hz, spacing -1..1, resonance 0..1, mode SVF_LP.., pair SVF_SERIES/SVF_PARALLEL. Outside the sample loop. */
static inline void svf2_set(svf2_t *p, float hz, float space, float reso, int mode, int pair, float sr) {
    int was_single = p->single;
    float k = svf_damp(reso);
    p->series = pair == SVF_SERIES;
    p->single = !p->series && space == 0.0f;
    if (p->series) k = sqrtf(k);
    p->ga = svf_g(hz, sr);
    p->gb = p->single ? p->ga : svf_g(hz * exp2f(space * SVF_SPACE_OCT), sr);
    svf_coefs(&p->a, p->ga, k, mode);
    svf_coefs(&p->b, p->gb, k, mode);
    if (was_single && !p->single && !p->series) {   /* spacing just left 0: B carries on from A, no step */
        p->b.ic1 = p->a.ic1;
        p->b.ic2 = p->a.ic2;
    }
}

static inline float svf2_next(svf2_t *p, float x) {
    if (p->series) return svf_next(&p->b, svf_next(&p->a, x));
    if (p->single) return svf_next(&p->a, x);
    return 0.5f * (svf_next(&p->a, x) + svf_next(&p->b, x));
}

/* one sample of an element at g with the non-linear damping (see the top of the file); c 0: svf_next to rounding */
static inline float svf_tick_nl(svf_t *f, float x, float g, float c) {
    float s1 = f->ic1, s2 = f->ic2;
    float d = c * s1 * s1;
    if (d > SVF_NL_DMAX) d = SVF_NL_DMAX;
    float v1 = (s1 + g * (x - s2)) / (1.0f + g * (g + f->k + d));   /* band */
    float v2 = s2 + g * v1;   /* low */
    f->ic1 = 2.0f * v1 - s1;
    f->ic2 = 2.0f * v2 - s2;
    return f->m0 * x + f->m1 * v1 + f->m2 * v2;
}

/* SVF SELF-OSC, after svf2_set: resonance 0..1 reaches SVF_K_OSC x the amount (0..1) instead of SVF_K_MIN. In series
 * the square root as svf2_set takes it, while there is one; below 0 k itself. Outside the sample loop. */
static inline void svf2_set_osc(svf2_t *p, float reso, float amount, int mode) {
    float k = 2.0f - (2.0f - SVF_K_OSC * amount) * (reso < 0.0f ? 0.0f : reso > 1.0f ? 1.0f : reso);
    if (p->series && k > 0.0f) k = sqrtf(k);
    svf_coefs(&p->a, p->ga, k, mode);
    svf_coefs(&p->b, p->gb, k, mode);
}

static inline float svf_clamp_g(float g) { return g < SVF_G_MIN ? SVF_G_MIN : g > SVF_G_MAX ? SVF_G_MAX : g; }

/* filter FM: both cutoffs times e (2 to the power of this sample's modulation in octaves). Scaling g stands in for
 * tan(pi fc / sr) of the scaled cutoff: exact octaves where tan is near linear (most of the range), a little wide near
 * the top cutoff, where g is clamped. One division per element per sample, no tanf. */
static inline float svf2_next_fm(svf2_t *p, float x, float e) {
    svf_set_g(&p->a, svf_clamp_g(p->ga * e));
    if (!p->single) svf_set_g(&p->b, svf_clamp_g(p->gb * e));
    return svf2_next(p, x);
}

/* the pair with the non-linear damping, both cutoffs times e (1: no filter FM) */
static inline float svf2_next_nl(svf2_t *p, float x, float e) {
    float ga = svf_clamp_g(p->ga * e), c = p->nl;
    if (p->single) return svf_tick_nl(&p->a, x, ga, c);
    float gb = svf_clamp_g(p->gb * e);
    if (p->series) return svf_tick_nl(&p->b, svf_tick_nl(&p->a, x, ga, c), gb, c);
    return 0.5f * (svf_tick_nl(&p->a, x, ga, c) + svf_tick_nl(&p->b, x, gb, c));
}
