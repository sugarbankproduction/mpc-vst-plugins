/* Transistor-ladder style filter with resonance and drive: lowpass or highpass, 24/18/12/6 dB per octave.
 * Model: the zero-delay-feedback ladder from V. Zavalishin, "The Art of VA Filter Design" (rev. 2.1.2, ch. 5,
 * openly published): four TPT one-pole lowpasses with the feedback solved inside the sample, so the cutoff stays in
 * tune and the filter stays stable right up to its top cutoff. One saturator sits where the input meets the feedback
 * (it limits self-oscillation to a steady sine and is what drive pushes into); its input is the linear solution of the
 * loop, the usual cheap way to keep a nonlinearity out of the zero-delay equation. Resonance always comes from the
 * fourth stage. Slopes and highpass are mixes of the stage outputs (same chapter): n-pole lowpass = stage n,
 * n-pole highpass = (1 - lowpass)^n expanded over the stages. A few multiplies and one division per sample, no
 * allocation: safe in the audio path. */
#pragma once
#include <math.h>

#define LADDER_K_MAX 4.4f   /* feedback at 100% resonance: just above 4, where the ladder starts to self-oscillate */
#define LADDER_FC_MAX 0.45f /* top cutoff as a share of the sample rate */

enum { LADDER_LP, LADDER_HP };
enum { LADDER_24DB, LADDER_18DB, LADDER_12DB, LADDER_6DB };   /* option order in params.json */

typedef struct {
    float s[4];                 /* the four stages' integrator states */
    float G, k, gain_in, gain_out, bass;
    float mix[5];               /* output = mix . (u, y1, y2, y3, y4) for the mode and slope */
} ladder_t;

/* tanh-like soft clip (3rd-order Pade): within 2% of tanh, exactly +-1 from |x| = 3, no library call */
static inline float ladder_sat(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static inline void ladder_reset(ladder_t *l) { l->s[0] = l->s[1] = l->s[2] = l->s[3] = 0.0f; }

/* cutoff in Hz, resonance and drive 0..1, mode LADDER_LP/HP, slope LADDER_24DB.. Call outside the per-sample loop. */
static inline void ladder_set(ladder_t *l, float hz, float reso, float drive, int mode, int slope, float sr) {
    static const float hp[4][5] = {   /* (1 - L)^n: binomial weights, alternating signs */
        {1, -4, 6, -4, 1}, {1, -3, 3, -1, 0}, {1, -2, 1, 0, 0}, {1, -1, 0, 0, 0},
    };
    float fc = hz < 1.0f ? 1.0f : hz > LADDER_FC_MAX * sr ? LADDER_FC_MAX * sr : hz;
    float g = tanf(3.14159265f * fc / sr);
    float dg = 1.0f + 15.0f * drive;   /* drive: up to +24 dB into the saturator */
    int n = slope < 0 ? 0 : slope > 3 ? 3 : slope;
    l->G = g / (1.0f + g);
    l->k = LADDER_K_MAX * reso;
    l->gain_in = 0.25f * dg;           /* at no drive a full-scale oscillator stays near the saturator's linear part */
    l->gain_out = 4.0f / sqrtf(dg);    /* undo the 0.25; give back part of drive's gain */
    /* resonance thins a lowpass's bass (passband gain 1/(1+k)): give half of it back. A highpass keeps its top. */
    l->bass = mode == LADDER_HP ? 1.0f : 1.0f + 0.5f * l->k;
    for (int i = 0; i < 5; i++) l->mix[i] = mode == LADDER_HP ? hp[n][i] : (i == 4 - n);
}

static inline float ladder_next(ladder_t *l, float x) {
    float G = l->G, H = 1.0f - G;   /* a TPT one-pole: y = G*x + (1-G)*s */
    float *s = l->s;
    float sigma = G * (G * (G * s[0] * H + s[1] * H) + s[2] * H) + s[3] * H;   /* stage 4's output from the states alone */
    float G4 = G * G * G * G;
    float in = x * l->gain_in * l->bass;   /* the caller adds a -100 dB noise floor: it starts self-oscillation and
                                               keeps the states out of the slow denormal range */
    float y[5];
    y[0] = ladder_sat(in - l->k * (G4 * in + sigma) / (1.0f + l->k * G4));
    for (int i = 0; i < 4; i++) {
        float v = (y[i] - s[i]) * G;
        y[i + 1] = v + s[i];
        s[i] = y[i + 1] + v;
    }
    const float *m = l->mix;
    return (m[0] * y[0] + m[1] * y[1] + m[2] * y[2] + m[3] * y[3] + m[4] * y[4]) * l->gain_out;
}
