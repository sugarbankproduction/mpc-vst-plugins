/* ONE-ish master delay (master effect 1, fed by SEND 1): a stereo delay with its own left and right times, or a
 * ping-pong delay whose repeats cross sides, the right side's repeats pushed later by OFFSET (up to half the left
 * time). Cubic (Hermite) reads, times glide when changed, a tone low-pass in the feedback. Returns the wet signal. */
#pragma once
#include "common.h"

#define MD_SIZE 131072   /* 2.97 s per channel */

typedef struct {
    float l[MD_SIZE], r[MD_SIZE];
    int w;
    float pl, pr;        /* read distances in samples, gliding to the set times */
    float lpl, lpr;      /* feedback tone */
} mdly_t;

static inline float md_target_l(const fx_params_t *p) { return fx_clamp(p->dly_l_ms * 0.001f * FX_SR, 3.0f, MD_SIZE - 8.0f); }
static inline float md_target_r(const fx_params_t *p) {
    float ms = p->dly_r_ms + (p->dly_pp ? p->dly_offset * 0.5f * p->dly_l_ms : 0.0f);
    return fx_clamp(ms * 0.001f * FX_SR, 3.0f, MD_SIZE - 8.0f);
}

static void md_reset(mdly_t *d, const fx_params_t *p) {
    memset(d, 0, sizeof *d);
    d->pl = md_target_l(p);
    d->pr = md_target_r(p);
}

/* in: what SEND 1 feeds it; out: the echoes */
static void md_process(mdly_t *d, const fx_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                       int n) {
    float tl = md_target_l(p), tr = md_target_r(p);
    float tone = 0.05f + 0.95f * p->dly_tone * p->dly_tone, fb = p->dly_fb;
    for (int i = 0; i < n; i++) {
        d->pl += (tl - d->pl) * 0.0005f;   /* about 45 ms to glide to a new time */
        d->pr += (tr - d->pr) * 0.0005f;
        /* read before this sample is written: one less */
        float el = fx_hermite(d->l, MD_SIZE - 1, d->w, d->pl - 1.0f), er = fx_hermite(d->r, MD_SIZE - 1, d->w, d->pr - 1.0f);
        d->lpl += (el - d->lpl) * tone;
        d->lpr += (er - d->lpr) * tone;
        if (p->dly_pp) {   /* the input enters on the left; each repeat crosses to the other side */
            d->l[d->w] = 0.5f * (inL[i] + inR[i]) + fb * d->lpr + FX_DENORM;
            d->r[d->w] = fb * d->lpl + FX_DENORM;
        } else {
            d->l[d->w] = inL[i] + fb * d->lpl + FX_DENORM;
            d->r[d->w] = inR[i] + fb * d->lpr + FX_DENORM;
        }
        d->w = (d->w + 1) & (MD_SIZE - 1);
        outL[i] = el;
        outR[i] = er;
    }
}
