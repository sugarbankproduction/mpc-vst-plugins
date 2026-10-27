/* ONE-ish effects, after the voices, in the order of the Moog One's effects section (manual p96-101):
 *   SYNTH EFFECT: one effect of a chosen type (src/fx/synth_fx.h) in line, MIX between dry and wet, its own ON.
 *   MASTER EFFECTS, as sends: SEND 1 feeds the master delay (src/fx/master_delay.h), SEND 2 the master reverb
 *   (src/fx/reverb.h); their returns are added to the synth effect's output. EFFECT 1>2 SERIES moves the delay's return
 *   from the output into the reverb's input; STEREO WIDTH widens or narrows the returns; master ON bypasses both.
 * Sends at 0 (or master off) leave the signal exactly dry; a synth effect that is off or at mix 0 too.
 * All buffers live inside fx_t (allocated with the instance, about 3.1 MB); fx_process() never allocates. An effect
 * switched on (or the synth effect or reverb switched to another type) starts from silence. A send turned to 0 lets
 * its effect's tail ring out; the effect stops once its return has been silent for 3 s. */
#pragma once
#include "fx/common.h"
#include "fx/synth_fx.h"
#include "fx/master_delay.h"
#include "fx/reverb.h"

#define FX_BLOCK 128
#define FX_IDLE MD_SIZE   /* samples of silent return (send at 0) before a master effect stops */

typedef struct {
    sfx_t sfx;
    mdly_t dly;
    rev_t rev;
    int sfx_run, sfx_type, dly_run, rev_run;   /* last block's state */
    int dly_idle, rev_idle;                    /* samples its return has been silent with its send at 0 */
    float mix, gain, send1, send2, series, width;   /* last block's values, ramped from */
} fx_t;

/* at create(): write to every page of the (calloc'd, zeroed) buffers once. The OS maps such memory lazily, so the
 * first write to each page would otherwise fault inside the audio thread when an effect is first switched on. */
static inline void fx_init(fx_t *f) {
    volatile char *b = (volatile char *)f;
    for (size_t i = 0; i < sizeof *f; i += 4096) b[i] = 0;
}

static inline void fx_copy(float *dst, const float *src, int n) { if (dst != src) memmove(dst, src, sizeof(float) * n); }

/* the synth effect, as on the instrument: its input is the voices' mono sum (mono, or NULL: inL/inR as they are), its
 * wet signal sits at the synth pan, and the dry signal keeps each voice's place (inL, inR) */
static void fx_synth(fx_t *f, const fx_params_t *p, const float *mono, const float *inL, const float *inR, float *outL,
                     float *outR, int n) {
    int run = p->sfx_on && (p->mix > 0.0f || (f->sfx_run && f->mix > 0.0f));
    if (run && (!f->sfx_run || f->sfx_type != p->sfx_type)) {
        sfx_reset(&f->sfx, p);
        f->mix = p->mix;
        f->gain = p->gain;
    }
    f->sfx_run = run;
    f->sfx_type = p->sfx_type;
    if (!run) { fx_copy(outL, inL, n); fx_copy(outR, inR, n); return; }
    float wl[FX_BLOCK], wr[FX_BLOCK], dm, dg;
    sfx_process(&f->sfx, p, mono ? mono : inL, mono ? mono : inR, wl, wr, n);
    float m = fx_ramp(&f->mix, p->mix, n, &dm), g = fx_ramp(&f->gain, p->gain, n, &dg);
    float pl = sqrtf(1.0f - p->wet_pan), pr = sqrtf(1.0f + p->wet_pan);   /* equal power, 1 and 1 at the centre */
    for (int i = 0; i < n; i++) {
        float dry = 1.0f - m, wet = m * g;
        outL[i] = inL[i] * dry + wl[i] * wet * pl;
        outR[i] = inR[i] * dry + wr[i] * wet * pr;
        m += dm; g += dg;
    }
}

/* a master effect's return was silent this block with its send at 0: count towards stopping it */
static int fx_idle(int *idle, const float *l, const float *r, int n, int send_off) {
    float peak = 0.0f;
    for (int i = 0; i < n; i++) peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
    *idle = send_off && peak < 1e-6f ? *idle + n : 0;
    return *idle < FX_IDLE;
}

static void fx_master(fx_t *f, const fx_params_t *p, float *l, float *r, int n) {
    if (!p->mfx_on) { f->dly_run = f->rev_run = 0; f->send1 = f->send2 = 0.0f; return; }
    int dly = p->send1 > 0.0f || f->send1 > 0.0f || f->dly_run;
    int rev = p->send2 > 0.0f || f->send2 > 0.0f || f->rev_run || (dly && p->series > 0.0f);
    if (!dly && !rev) return;
    if (dly && !f->dly_run) { md_reset(&f->dly, p); f->send1 = 0.0f; f->dly_idle = 0; }
    if (rev && (!f->rev_run || f->rev.type != p->rev_type)) { rv_reset(&f->rev, p); f->rev_idle = 0; }
    if (!f->dly_run && !f->rev_run) { f->series = p->series; f->width = p->width; }
    float dl[FX_BLOCK], dr[FX_BLOCK], rl[FX_BLOCK], rr[FX_BLOCK], d1, d2, ds, dw;
    float s1 = fx_ramp(&f->send1, p->send1, n, &d1), s2 = fx_ramp(&f->send2, p->send2, n, &d2);
    float se = fx_ramp(&f->series, p->series, n, &ds), wd = fx_ramp(&f->width, p->width, n, &dw);
    if (dly) {
        float a = s1;
        for (int i = 0; i < n; i++, a += d1) { dl[i] = l[i] * a; dr[i] = r[i] * a; }
        md_process(&f->dly, p, dl, dr, dl, dr, n);
        f->dly_run = fx_idle(&f->dly_idle, dl, dr, n, p->send1 <= 0.0f && f->send1 <= 0.0f);
    } else {
        memset(dl, 0, sizeof(float) * n); memset(dr, 0, sizeof(float) * n);
    }
    if (rev) {
        float a = s2, b = se;
        for (int i = 0; i < n; i++, a += d2, b += ds) { rl[i] = l[i] * a + dl[i] * b; rr[i] = r[i] * a + dr[i] * b; }
        rv_process(&f->rev, p, rl, rr, rl, rr, n);
        f->rev_run = fx_idle(&f->rev_idle, rl, rr, n, p->send2 <= 0.0f && f->send2 <= 0.0f && !f->dly_run);
    } else {
        memset(rl, 0, sizeof(float) * n); memset(rr, 0, sizeof(float) * n);
    }
    for (int i = 0; i < n; i++) {   /* the returns, the delay's share taken by the series amount, then width (mid/side) */
        float yl = dl[i] * (1.0f - se) + rl[i], yr = dr[i] * (1.0f - se) + rr[i];
        float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * wd;
        l[i] += mid + side;
        r[i] += mid - side;
        se += ds; wd += dw;
    }
}

/* in: the voices (stereo; pass the same buffer twice for a mono sum); out: stereo. In and out may be the same buffers.
 * Any n: worked through in blocks of up to 128. */
/* mono: the voices' mono sum for the synth effect's input (NULL: inL/inR); inL, inR: the voices in stereo */
static void fx_process(fx_t *f, const fx_params_t *p, const float *mono, const float *inL, const float *inR, float *outL,
                       float *outR, int n) {
    for (int at = 0; at < n; at += FX_BLOCK) {
        int m = n - at < FX_BLOCK ? n - at : FX_BLOCK;
        fx_synth(f, p, mono ? mono + at : NULL, inL + at, inR + at, outL + at, outR + at, m);
        fx_master(f, p, outL + at, outR + at, m);
    }
}
