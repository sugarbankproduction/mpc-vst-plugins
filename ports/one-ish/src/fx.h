/* ONE-ish effects, after the voices, in the order of the Moog One's effects section (manual p96-101):
 *   SYNTH EFFECT: one effect of a chosen type (src/fx/synth_fx.h) in line, MIX between dry and wet, its own ON.
 *   MASTER EFFECTS: two slots, each running any type of the master list (common.h MFX_*: the reverbs of
 *   src/fx/reverb.h, or a synth effect type: delays, choruses, flangers, phasers, the bit crusher, the resonator) with
 *   its own controls (engine.c: slot 1's are the dly_* params, slot 2's the rev_* params, after what each held before
 *   it could take any type). BUSS MODE: DUAL (the manual's Dual Mono: SEND 1 feeds effect 1 and SEND 2 effect 2; their
 *   returns are added to the synth effect's output; EFFECT 1>2 SERIES moves effect 1's return from the output into
 *   effect 2's input) or STEREO (one effect, slot 1's, on SEND 1). Unlike the instrument's mono buss, DUAL keeps each
 *   voice's place in both effects' inputs too (the default sound is the earlier delay and reverb, unchanged).
 *   STEREO WIDTH widens or narrows the returns; master ON bypasses both. Each slot has MIX (its own dry/wet, 100%: all
 *   return) and GAIN (the return's level, 50% = unity); the phasers' TONE filters the mix, as the manual has it.
 * Sends at 0 (or master off) leave the signal exactly dry; a synth effect that is off or at mix 0 too.
 * The open-source types (Mutable Instruments Rings and Clouds, Airwindows ToTape6, Density, Console7; the Galactic
 * reverb as SPACE) run in the memory of the slot they take: no buffers of their own (third_party/THIRD_PARTY.md).
 * All buffers live inside fx_t (allocated with the instance, about 4.4 MB); fx_process() never allocates.
 * A slot's life: it starts in cleared memory; switched to another type (or off) it fades its output out over one block,
 * then its memory is cleared FX_CLEAR_CHUNK bytes a block while it is silent (no click, no long memset in one block),
 * and the new type starts there, its input fading in over the first block. A send turned to 0 lets its effect's tail ring out; the effect stops once
 * its return has been silent for 3 s. */
#pragma once
#include "fx/common.h"
#include "fx/synth_fx.h"
#include "fx/reverb.h"

#define FX_BLOCK 128
#define FX_IDLE 131072         /* samples of silent return (send at 0) before a master effect stops */
#define FX_CLEAR_CHUNK 131072  /* bytes of a stopped slot's memory cleared per block */

enum { FS_OFF, FS_RUN, FS_CLEAR };   /* a slot: clean and ready, running, being cleared */

typedef struct {
    int state;
    int type;          /* what runs (synth effect: SFX_*, master: MFX_*) */
    int switching;     /* FS_CLEAR on the way to another type (else: stopped) */
    size_t at;         /* FS_CLEAR: bytes cleared so far */
    float fade;        /* the output's fade: out as the slot switches away or stops */
    int fadein;        /* just started after a switch: its input fades in over the first block (no hard start) */
    float mix, gain;   /* last block's values, ramped from */
    float tone[2];     /* the phasers' TONE on the mix */
    int idle;          /* samples its return has been silent with its send at 0 */
} fxlife_t;

typedef struct {
    union { sfx_t sfx; rev_t rev; } u;   /* one type at a time */
    fxlife_t s;
} mslot_t;

typedef struct {
    sfx_t sfx;
    fxlife_t ss;
    mslot_t m[2];
    float send1, send2, series, width;   /* last block's values, ramped from */
} fx_t;

/* at create(): write to every page of the (calloc'd, zeroed) buffers once. The OS maps such memory lazily, so the
 * first write to each page would otherwise fault inside the audio thread when an effect is first switched on. */
static inline void fx_init(fx_t *f) {
    volatile char *b = (volatile char *)f;
    for (size_t i = 0; i < sizeof *f; i += 4096) b[i] = 0;
}

static inline void fx_copy(float *dst, const float *src, int n) { if (dst != src) memmove(dst, src, sizeof(float) * n); }

/* the next part of a stopped slot's memory cleared (all: the rest at once, it is wanted now); clean: FS_OFF */
static void fx_clear_step(fxlife_t *s, void *mem, size_t size, int all) {
    size_t left = size - s->at, n = all || left < FX_CLEAR_CHUNK ? left : FX_CLEAR_CHUNK;
    memset((char *)mem + s->at, 0, n);
    s->at += n;
    if (s->at >= size) { s->state = FS_OFF; s->at = 0; }
}

/* the phasers' TONE: a low-pass on the mixed signal (manual p107): all of it from mix 50% up, fading out below so that
 * mix 0 stays exactly dry */
static void fx_mix_tone(fxlife_t *s, const fxs_params_t *q, int type, float m, float dm, float *l, float *r, int n) {
    if (!SFX_IS_PHASER(type) || q->tone >= 0.999f) return;
    const float c = fx_tone_coef(q->tone);
    for (int i = 0; i < n; i++, m += dm) {
        float a = fminf(1.0f, 2.0f * m);
        s->tone[0] += (l[i] - s->tone[0]) * c;
        s->tone[1] += (r[i] - s->tone[1]) * c;
        l[i] += a * (s->tone[0] - l[i]);
        r[i] += a * (s->tone[1] - r[i]);
    }
}

/* the synth effect, as on the instrument: its input is the voices' mono sum (mono, or NULL: inL/inR as they are), its
 * wet signal sits at the synth pan, and the dry signal keeps each voice's place (inL, inR) */
static void fx_synth(fx_t *f, const fx_params_t *p, const float *mono, const float *inL, const float *inR, float *outL,
                     float *outR, int n) {
    const fxs_params_t *q = &p->sfx;
    fxlife_t *s = &f->ss;
    int want = p->sfx_on && (q->mix > 0.0f || (s->state == FS_RUN && s->mix > 0.0f));
    if (s->state == FS_CLEAR) fx_clear_step(s, &f->sfx, sizeof f->sfx, want && !s->switching);
    int fading = s->state == FS_RUN && (!want || s->type != q->type);
    if (s->state == FS_OFF && want) {   /* from off (dry, as mix 0): the wet fades in, no step (a modulated mix) */
        sfx_init(&f->sfx, q);
        s->fadein = s->switching;
        s->state = FS_RUN; s->type = q->type; s->switching = 0;
        s->mix = 0.0f; s->gain = q->gain; s->tone[0] = s->tone[1] = 0.0f;
    }
    if (s->state != FS_RUN) { fx_copy(outL, inL, n); fx_copy(outR, inR, n); return; }
    fxs_params_t run;
    if (q->type != s->type) { run = *q; run.type = s->type; q = &run; }   /* the old type fades out */
    float wl[FX_BLOCK], wr[FX_BLOCK], dm, dg;
    const float *xl = mono ? mono : inL, *xr = mono ? mono : inR;
    float fl[FX_BLOCK], fr[FX_BLOCK];
    if (s->fadein) {   /* after a switch: the new type's input fades in */
        for (int i = 0; i < n; i++) { float a = (float)(i + 1) / (float)n; fl[i] = xl[i] * a; fr[i] = xr[i] * a; }
        xl = fl; xr = fr;
        s->fadein = 0;
    }
    sfx_process(&f->sfx, q, xl, xr, wl, wr, n);
    float m = fx_ramp(&s->mix, fading ? 0.0f : q->mix, n, fading ? n : p->ramp, &dm);
    float g = fx_ramp(&s->gain, q->gain, n, p->ramp, &dg);
    float pl = sqrtf(1.0f - p->wet_pan), pr = sqrtf(1.0f + p->wet_pan);   /* equal power, 1 and 1 at the centre */
    float m0 = m;
    for (int i = 0; i < n; i++) {
        float dry = 1.0f - m, wet = m * g;
        outL[i] = inL[i] * dry + wl[i] * wet * pl;
        outR[i] = inR[i] * dry + wr[i] * wet * pr;
        m += dm; g += dg;
    }
    fx_mix_tone(s, q, s->type, m0, dm, outL, outR, n);
    if (fading) { s->state = FS_CLEAR; s->at = 0; s->switching = want; s->mix = 0.0f; }
}

/* a master effect's return was silent this block with its send at 0: count towards stopping it */
static int fx_idle(int *idle, const float *l, const float *r, int n, int send_off) {
    float peak = 0.0f;
    for (int i = 0; i < n; i++) peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
    *idle = send_off && peak < 1e-6f ? *idle + n : 0;
    return *idle < FX_IDLE;
}

/* one master slot for this block: in -> its return (out); returns 1 when it ran (else out is silence) */
static int mslot_run(mslot_t *ms, const fxs_params_t *q, int want, const float *inL, const float *inR, float *outL,
                     float *outR, int n, int ramp) {
    fxlife_t *s = &ms->s;
    if (s->state == FS_CLEAR) fx_clear_step(s, &ms->u, sizeof ms->u, want && !s->switching);
    int fading = s->state == FS_RUN && (!want || s->type != q->mtype);
    if (s->state == FS_OFF && want) {
        if (MFX_SFX[q->mtype] < 0) rv_init(&ms->u.rev, q);
        else sfx_init(&ms->u.sfx, q);
        s->fade = 1.0f;
        s->fadein = s->switching;   /* after a switch the new type's input fades in; from off its send ramps up */
        s->state = FS_RUN; s->type = q->mtype; s->switching = 0; s->idle = 0;
        s->mix = q->mix; s->gain = q->gain; s->tone[0] = s->tone[1] = 0.0f;
    }
    if (s->state != FS_RUN) {
        memset(outL, 0, sizeof(float) * n);
        memset(outR, 0, sizeof(float) * n);
        return 0;
    }
    fxs_params_t run;
    if (q->mtype != s->type) {   /* the old type fades out */
        run = *q; run.mtype = s->type; run.type = MFX_SFX[s->type] < 0 ? s->type : MFX_SFX[s->type]; q = &run;
    }
    float wl[FX_BLOCK], wr[FX_BLOCK], dm, dg, df, fl[FX_BLOCK], fr[FX_BLOCK];
    const float *xl = inL, *xr = inR;
    if (s->fadein) {
        for (int i = 0; i < n; i++) { float a = (float)(i + 1) / (float)n; fl[i] = inL[i] * a; fr[i] = inR[i] * a; }
        xl = fl; xr = fr;
        s->fadein = 0;
    }
    if (MFX_SFX[s->type] < 0) rv_process(&ms->u.rev, q, xl, xr, wl, wr, n);
    else sfx_process(&ms->u.sfx, q, xl, xr, wl, wr, n);
    float m = fx_ramp(&s->mix, q->mix, n, ramp, &dm), g = fx_ramp(&s->gain, q->gain, n, ramp, &dg);
    float fd = fx_ramp(&s->fade, fading ? 0.0f : 1.0f, n, n, &df);
    if (m == 1.0f && dm == 0.0f && g == 1.0f && dg == 0.0f && fd == 1.0f && df == 0.0f) {   /* all return, as it is */
        fx_copy(outL, wl, n);
        fx_copy(outR, wr, n);
    } else {
        float m0 = m;
        for (int i = 0; i < n; i++) {
            float wet = m * g * fd, dry = (1.0f - m) * fd;
            outL[i] = xl[i] * dry + wl[i] * wet;
            outR[i] = xr[i] * dry + wr[i] * wet;
            m += dm; g += dg; fd += df;
        }
        if (MFX_SFX[s->type] >= 0) fx_mix_tone(s, q, MFX_SFX[s->type], m0, dm, outL, outR, n);
    }
    if (fading) { s->state = FS_CLEAR; s->at = 0; s->switching = want; }
    return 1;
}

/* a running slot whose return has been silent long enough with its send at 0 stops (its memory is then cleared) */
static void mslot_idle(mslot_t *ms, const float *l, const float *r, int n, int send_off) {
    if (ms->s.state == FS_RUN && !fx_idle(&ms->s.idle, l, r, n, send_off)) {
        ms->s.state = FS_CLEAR; ms->s.at = 0; ms->s.switching = 0;
    }
}

static void fx_master(fx_t *f, const fx_params_t *p, float *l, float *r, int n) {
    mslot_t *m1 = &f->m[0], *m2 = &f->m[1];
    const int on = p->mfx_on, stereo = p->buss == MB_STEREO;
    const float t1 = on ? p->send1 : 0.0f, t2 = on && !stereo ? p->send2 : 0.0f, tse = stereo ? 0.0f : p->series;
    int run1 = m1->s.state == FS_RUN, run2 = m2->s.state == FS_RUN;
    int want1 = on && (t1 > 0.0f || f->send1 > 0.0f || run1);
    int want2 = on && !stereo && (t2 > 0.0f || f->send2 > 0.0f || run2 || (want1 && tse > 0.0f));
    if (!want1 && !want2 && !run1 && !run2) {   /* nothing plays: the next part of a stopped slot's memory cleared */
        if (m1->s.state == FS_CLEAR) fx_clear_step(&m1->s, &m1->u, sizeof m1->u, 0);
        if (m2->s.state == FS_CLEAR) fx_clear_step(&m2->s, &m2->u, sizeof m2->u, 0);
        f->send1 = f->send2 = 0.0f;
        return;
    }
    if (want1 && m1->s.state != FS_RUN) f->send1 = 0.0f;   /* starting: its send fades in */
    if (!run1 && !run2) { f->series = tse; f->width = p->width; }
    float dl[FX_BLOCK], dr[FX_BLOCK], rl[FX_BLOCK], rr[FX_BLOCK], d1, d2, ds, dw;
    float s1 = fx_ramp(&f->send1, t1, n, p->ramp, &d1), s2 = fx_ramp(&f->send2, t2, n, p->ramp, &d2);
    float se = fx_ramp(&f->series, tse, n, p->ramp, &ds), wd = fx_ramp(&f->width, p->width, n, p->ramp, &dw);
    int any = 0;
    if (want1 || run1 || m1->s.state == FS_CLEAR) {
        float a = s1;
        for (int i = 0; i < n; i++, a += d1) { dl[i] = l[i] * a; dr[i] = r[i] * a; }
        if (mslot_run(m1, &p->mfx[0], want1, dl, dr, dl, dr, n, p->ramp)) {
            any = 1;
            mslot_idle(m1, dl, dr, n, t1 <= 0.0f && f->send1 <= 0.0f);
        }
    } else {
        memset(dl, 0, sizeof(float) * n); memset(dr, 0, sizeof(float) * n);
    }
    if (want2 || run2 || m2->s.state == FS_CLEAR) {
        float a = s2, b = se;
        for (int i = 0; i < n; i++, a += d2, b += ds) { rl[i] = l[i] * a + dl[i] * b; rr[i] = r[i] * a + dr[i] * b; }
        if (mslot_run(m2, &p->mfx[1], want2, rl, rr, rl, rr, n, p->ramp)) {
            any = 1;
            mslot_idle(m2, rl, rr, n, t2 <= 0.0f && f->send2 <= 0.0f && m1->s.state != FS_RUN);
        }
    } else {
        memset(rl, 0, sizeof(float) * n); memset(rr, 0, sizeof(float) * n);
    }
    if (!any) return;
    for (int i = 0; i < n; i++) {   /* the returns, effect 1's share taken by the series amount, then width (mid/side) */
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
