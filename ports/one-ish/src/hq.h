/* (2026-10-30) HQ MODE's decimation filters: a voice that runs at 2 or 4 times the sample rate (the ANALOG page's HQ
 * MODE, render_voice) is brought back to 44.1 kHz by these half-band lowpasses, each halving the rate.
 * Design: the polyphase IIR half-band (two chains of first-order allpasses in z^-2, averaged), as in Laurent de Soras'
 * openly published HIIR notes; its coefficients come from the elliptic half-band design described there, worked out
 * offline (and checked by their frequency response):
 *   - 2x to 1x (88.2 -> 44.1 kHz): 8 coefficients, flat to 20 kHz (within 0.001 dB), at least 106 dB down from
 *     24.1 kHz (what would fold back onto 0 .. 20 kHz)
 *   - 4x to 2x (176.4 -> 88.2 kHz): 4 coefficients, flat to 40 kHz, at least 79 dB down from 60 kHz (what would fold
 *     onto 0 .. 28 kHz; the 2x stage after it takes out the rest)
 * Phase: not linear near the top of the band (a synth voice has no use for that); the delay is about 1.5 samples at
 * 44.1 kHz at low frequencies. Per output sample: one multiply and two adds per coefficient. No allocation, no
 * library call: safe in the audio path. The states start at 0 (hq_reset) and a voice resets them when it starts from
 * silence or its HQ setting changes. */
#pragma once

#define HQ_N2 8   /* the 2x stage's coefficients */
#define HQ_N4 4   /* the 4x stage's */

typedef struct {
    float x2[HQ_N2], y2[HQ_N2];   /* each 2x allpass's last input and output */
    float x4[HQ_N4], y4[HQ_N4];   /* the 4x stage's */
} hq_dec_t;

static const float HQ_C2[HQ_N2] = {0.0378221453f, 0.1409282843f, 0.2840959447f, 0.4397662051f,
                                   0.5880778848f, 0.7201863416f, 0.8367299359f, 0.9451069492f};
static const float HQ_C4[HQ_N4] = {0.0670134906f, 0.2468767582f, 0.4991291267f, 0.8095984264f};

static inline void hq_reset(hq_dec_t *d) {
    for (int i = 0; i < HQ_N2; i++) d->x2[i] = d->y2[i] = 0.0f;
    for (int i = 0; i < HQ_N4; i++) d->x4[i] = d->y4[i] = 0.0f;
}

/* one half-band stage: two samples in (a the earlier, b the later), one out. Path 0 (the even coefficients) takes the
 * later sample, path 1 the earlier one; each allpass y = c (x - y[-1]) + x[-1]. The tiny constant keeps the states
 * out of the denormal range when a voice falls silent (it is 400 dB below full scale). */
static inline float hq_stage(const float *c, float *xm, float *ym, int n, float a, float b) {
    float p0 = b + 1e-20f, p1 = a;
    for (int i = 0; i < n; i += 2) {
        float y = c[i] * (p0 - ym[i]) + xm[i];
        xm[i] = p0;
        ym[i] = y;
        p0 = y;
        y = c[i + 1] * (p1 - ym[i + 1]) + xm[i + 1];
        xm[i + 1] = p1;
        ym[i + 1] = y;
        p1 = y;
    }
    return 0.5f * (p0 + p1);
}

/* two samples at 2x the rate to one */
static inline float hq_dec2(hq_dec_t *d, const float *in) { return hq_stage(HQ_C2, d->x2, d->y2, HQ_N2, in[0], in[1]); }

/* four samples at 4x the rate to one: to 2x, then to 1x */
static inline float hq_dec4(hq_dec_t *d, const float *in) {
    float m0 = hq_stage(HQ_C4, d->x4, d->y4, HQ_N4, in[0], in[1]);
    float m1 = hq_stage(HQ_C4, d->x4, d->y4, HQ_N4, in[2], in[3]);
    return hq_stage(HQ_C2, d->x2, d->y2, HQ_N2, m0, m1);
}
