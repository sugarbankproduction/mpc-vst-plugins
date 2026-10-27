/* ONE-ish synth effect: one type at a time on the voices' sum, stereo in, wet stereo out (fx.h mixes it with the dry
 * signal). Original designs from textbook parts: modulated delay lines read with 4-point Hermite interpolation,
 * first-order all-pass phasers, a one-pole/saturator BBD and tape model, a diffusing spring-style echo tank, a sample
 * and hold bit crusher and a bank of three state-variable filters (Zavalishin's trapezoidal "TPT" form).
 * Each channel runs its own line, so a mono input (both channels equal) and a panned stereo input both work. */
#pragma once
#include "common.h"

#define SFX_SIZE 131072   /* 2.97 s per channel: delay-type times up to 2 s, plus modulation */
#define SFX_MASK (SFX_SIZE - 1)
#define SFX_MAX_STAGES 12

typedef struct {
    float l[SFX_SIZE], r[SFX_SIZE];   /* the delay lines (every delay, chorus and flanger type uses them) */
    int w;
    float ph, ph_r, vph, wph;         /* LFO phases: left, right, vibrato/flutter, tape wow */
    float fbl, fbr;                   /* the last wet samples, fed back */
    float dl, dr;                     /* the delay times in samples, gliding to the set time (no zipper) */
    float lpl, lpr;                   /* tone low-pass, per channel */
    float apl[SFX_MAX_STAGES], apr[SFX_MAX_STAGES];   /* phaser stages */
    float al, ar, dal, dar;           /* phaser all-pass coefficients and their steps (ramped between control steps) */
    float bbd[2][4];                  /* BBD filters per channel: two poles in, two poles out */
    float rnd, rnd_to;                /* BBD random wobble (smoothed random walk) */
    float hpl, hpr;                   /* tape bias high-pass */
    DL(e1l, 512); DL(e2l, 512); DL(e3l, 512); DL(etl, 2048);   /* echo diffusion, left */
    DL(e1r, 512); DL(e2r, 512); DL(e3r, 512); DL(etr, 2048);   /* and right */
    float edl, edr;                   /* echo tank damping */
    float hold_l, hold_r, acc;        /* bit crusher sample and hold */
    float rs[2][3][2];                /* resonator filter states: channel, filter, (ic1, ic2) */
    uint32_t rng;
    int cnt;                          /* samples to the next control step (phasers, resonator, BBD wobble) */
} sfx_t;

static inline void sfx_write(sfx_t *f, float xl, float xr) {
    f->l[f->w] = xl;
    f->r[f->w] = xr;
    f->w = (f->w + 1) & SFX_MASK;
}
static inline float sfx_clamp_dist(float d) { return fx_clamp(d, 2.0f, SFX_SIZE - 8.0f); }

/* ---- chorus, flanger, their stereo versions, dual chorus, ensemble: modulated lines ---- */
static void sfx_mod_lines(sfx_t *f, const fx_params_t *p, const float *inL, const float *inR, float *wl, float *wr,
                          int n) {
    const int type = p->sfx_type, stereo = type == SFX_SFLANGER || type == SFX_SCHORUS;
    const int flanger = type == SFX_FLANGER || type == SFX_SFLANGER;
    const float ms = 0.001f * FX_SR;
    const float incl = p->rate_hz / FX_SR, incr = (stereo ? p->rate_r_hz : p->rate_hz) / FX_SR, vinc = p->vib_hz / FX_SR;
    const float fbl = p->fb, fbr = stereo ? p->fb_r : p->fb;
    const float tl = p->time_ms * ms, tr = (stereo ? p->time_r_ms : p->time_ms) * ms;
    const float off = 0.5f * p->spread, vib = p->vib_depth * 0.4f * ms, nz = p->noise * 0.03f;
    const float tc = fx_tone_coef(p->tone);
    for (int i = 0; i < n; i++) {
        f->dl += (tl - f->dl) * 0.002f;   /* about 10 ms to glide to a new time */
        f->dr += (tr - f->dr) * 0.002f;
        float pl = f->ph, pr = fx_wrap(f->ph_r + off);
        sfx_write(f, inL[i] + fbl * f->fbl + nz * fx_white(&f->rng) + FX_DENORM,
                  inR[i] + fbr * f->fbr + nz * fx_white(&f->rng) + FX_DENORM);
        float yl, yr;
        if (flanger) {   /* sweeps from the set time (the longest) down towards 0 */
            float ul = 0.5f + 0.5f * fx_tri(pl), ur = 0.5f + 0.5f * fx_tri(pr);
            yl = fx_hermite(f->l, SFX_MASK, f->w, sfx_clamp_dist(2.0f + (f->dl - 2.0f) * (1.0f - p->depth * ul)));
            yr = fx_hermite(f->r, SFX_MASK, f->w, sfx_clamp_dist(2.0f + (f->dr - 2.0f) * (1.0f - p->depth * ur)));
        } else if (type == SFX_CHORUS || type == SFX_SCHORUS) {   /* swings +-half the time around it */
            yl = fx_hermite(f->l, SFX_MASK, f->w, sfx_clamp_dist(f->dl * (1.0f + 0.5f * p->depth * fx_tri(pl))));
            yr = fx_hermite(f->r, SFX_MASK, f->w, sfx_clamp_dist(f->dr * (1.0f + 0.5f * p->depth * fx_tri(pr))));
        } else {
            float v = vib * fx_sin(f->vph), sw = 0.5f * p->depth;
            if (type == SFX_DCHORUS) {   /* bi-phase: the left tap and the right tap swing in opposite directions */
                yl = fx_hermite(f->l, SFX_MASK, f->w, sfx_clamp_dist(f->dl * (1.0f + sw * fx_tri(pl)) + v));
                yr = fx_hermite(f->r, SFX_MASK, f->w, sfx_clamp_dist(f->dl * (1.0f + sw * fx_tri(fx_wrap(pl + 0.5f))) + v));
            } else {   /* ensemble: three taps a third of a cycle apart on each line, spread across the sides */
                float a = fx_wrap(pl + 1.0f / 3.0f), b = fx_wrap(pl + 2.0f / 3.0f), s = p->spread;
                float d0 = sfx_clamp_dist(f->dl * (1.0f + sw * fx_sin(pl)) + v);
                float d1 = sfx_clamp_dist(f->dl * (1.0f + sw * fx_sin(a)) + v);
                float d2 = sfx_clamp_dist(f->dl * (1.0f + sw * fx_sin(b)) + v);
                float l0 = fx_hermite(f->l, SFX_MASK, f->w, d0), l1 = fx_hermite(f->l, SFX_MASK, f->w, d1);
                float l2 = fx_hermite(f->l, SFX_MASK, f->w, d2);
                float r0 = fx_hermite(f->r, SFX_MASK, f->w, d0), r1 = fx_hermite(f->r, SFX_MASK, f->w, d1);
                float r2 = fx_hermite(f->r, SFX_MASK, f->w, d2);
                yl = (l0 * (1.0f + s) + l1 + l2 * (1.0f - s)) * (1.0f / 3.0f);
                yr = (r2 * (1.0f + s) + r1 + r0 * (1.0f - s)) * (1.0f / 3.0f);
            }
        }
        f->fbl = yl; f->fbr = yr;
        f->lpl += (yl - f->lpl) * tc;
        f->lpr += (yr - f->lpr) * tc;
        wl[i] = f->lpl; wr[i] = f->lpr;
        f->ph = fx_wrap(f->ph + incl);
        f->ph_r = fx_wrap(f->ph_r + incr);
        f->vph = fx_wrap(f->vph + vinc);
    }
}

/* ---- phasers: 6 or 12 first-order all-pass stages per channel, swept up from the SWEEP point; the Time Phaser is
 * 4 stages into a delay line ---- */
static inline float sfx_ap_coef(const fx_params_t *p, float ph) {
    float hz = 40.0f * powf(100.0f, p->sweep) * exp2f(5.0f * p->depth * (0.5f + 0.5f * fx_tri(ph)));
    float t = tanf(FX_PI * fminf(hz, 16000.0f) / FX_SR);
    return (t - 1.0f) / (t + 1.0f);
}

static void sfx_phaser(sfx_t *f, const fx_params_t *p, const float *inL, const float *inR, float *wl, float *wr, int n) {
    const int stages = p->sfx_type == SFX_PHASER12 ? 12 : p->sfx_type == SFX_PHASER6 ? 6 : 4;
    const int tphase = p->sfx_type == SFX_TPHASER;
    const float incl = p->rate_hz / FX_SR, off = 0.5f * p->spread, nz = p->noise * 0.03f, tc = fx_tone_coef(p->tone);
    const float dt = sfx_clamp_dist(p->dtime_ms * 0.001f * FX_SR);
    for (int i = 0; i < n; i++) {
        if (f->cnt <= 0) {   /* every 8 samples: the coefficients 8 samples on, ramped to */
            f->cnt = 8;
            float ahead = fx_wrap(f->ph + 8.0f * incl);
            f->dal = (sfx_ap_coef(p, ahead) - f->al) * 0.125f;
            f->dar = (sfx_ap_coef(p, fx_wrap(ahead + off)) - f->ar) * 0.125f;
        }
        f->cnt--;
        f->al += f->dal; f->ar += f->dar;
        float sl = inL[i] + p->fb * f->fbl + nz * fx_white(&f->rng) + FX_DENORM;
        float sr = inR[i] + p->fb * f->fbr + nz * fx_white(&f->rng) + FX_DENORM;
        for (int k = 0; k < stages; k++) {   /* y = a*x + s; s = x - a*y */
            float y = f->al * sl + f->apl[k]; f->apl[k] = sl - f->al * y; sl = y;
            y = f->ar * sr + f->apr[k]; f->apr[k] = sr - f->ar * y; sr = y;
        }
        f->fbl = sl; f->fbr = sr;
        if (tphase) {   /* into a delay line with its own feedback */
            f->dl += (dt - f->dl) * 0.0005f;
            float d = sfx_clamp_dist(f->dl - 1.0f);   /* read before this sample is written: one less */
            float el = fx_hermite(f->l, SFX_MASK, f->w, d), er = fx_hermite(f->r, SFX_MASK, f->w, d);
            sfx_write(f, sl + p->dfb * el + FX_DENORM, sr + p->dfb * er + FX_DENORM);
            sl = 0.7f * (sl + el);
            sr = 0.7f * (sr + er);
        }
        f->lpl += (sl - f->lpl) * tc;
        f->lpr += (sr - f->lpr) * tc;
        wl[i] = f->lpl; wr[i] = f->lpr;
        f->ph = fx_wrap(f->ph + incl);
    }
}

/* ---- delays: clean, BBD, tape and echo; one tap per channel at the delay time ---- */
static void sfx_delays(sfx_t *f, const fx_params_t *p, const float *inL, const float *inR, float *wl, float *wr, int n) {
    const int type = p->sfx_type;
    const float ms = 0.001f * FX_SR, target = sfx_clamp_dist(p->dtime_ms * ms), tc = fx_tone_coef(p->tone);
    /* BBD: the bandwidth falls as the delay grows (a 4096-stage line clocked slower), about 680 Hz/s of delay */
    float bc = fx_lp_coef(fx_clamp(682.7f * FX_SR / fmaxf(f->dl, 1.0f), 400.0f, 15000.0f));
    float drive = 1.0f + 2.0f * p->bias, hpc = fx_lp_coef(20.0f + 280.0f * p->bias);   /* tape */
    float wow_inc = (0.3f + 1.2f * p->wow) / FX_SR, flut_inc = (4.0f + 10.0f * p->flutter) / FX_SR;
    float room = 0.3f + 0.7f * p->room, efb = 0.8f * p->decay;   /* echo tank */
    int a1 = (int)(141 * room), a2 = (int)(263 * room), a3 = (int)(397 * room), at = (int)(1499 * room);
    int b1 = (int)(157 * room), b2 = (int)(293 * room), b3 = (int)(443 * room), bt = (int)(1693 * room);
    for (int i = 0; i < n; i++) {
        f->dl += (target - f->dl) * 0.0005f;   /* about 45 ms to glide to a new time */
        float d = f->dl;
        if (type == SFX_BBD) {   /* a slow random wander of the clock */
            if (--f->cnt <= 0) { f->cnt = 8192; f->rnd_to = fx_white(&f->rng); }
            f->rnd += (f->rnd_to - f->rnd) * 0.0001f;   /* a few cents of drift at full MOD */
            d += p->depth * (0.002f * d + 8.0f) * f->rnd;
        } else if (type == SFX_TAPE) {
            d += p->wow * (0.003f * d + 10.0f) * fx_sin(f->wph) + p->flutter * (0.0004f * d + 1.5f) * fx_sin(f->vph);
            f->wph = fx_wrap(f->wph + wow_inc);
            f->vph = fx_wrap(f->vph + flut_inc);
        } else if (type == SFX_ECHO) {
            d += p->depth * ms * fx_sin(f->ph);
            f->ph = fx_wrap(f->ph + p->rate_hz / FX_SR);
        }
        d = sfx_clamp_dist(d - 1.0f);   /* read before this sample is written: one less */
        float el = fx_hermite(f->l, SFX_MASK, f->w, d), er = fx_hermite(f->r, SFX_MASK, f->w, d);
        float xl = inL[i], xr = inR[i];
        if (type == SFX_BBD) {   /* filtered in and out, soft-clipped like a companded line, clock hiss */
            float *a = f->bbd[0], *b = f->bbd[1];
            a[2] += (el - a[2]) * bc; a[3] += (a[2] - a[3]) * bc;
            b[2] += (er - b[2]) * bc; b[3] += (b[2] - b[3]) * bc;
            f->lpl += (a[3] - f->lpl) * tc; f->lpr += (b[3] - f->lpr) * tc;
            float hiss = p->noise * 0.004f;
            el = f->lpl + hiss * fx_white(&f->rng); er = f->lpr + hiss * fx_white(&f->rng);
            float vl = fx_sat(xl + p->fb * el), vr = fx_sat(xr + p->fb * er);
            a[0] += (vl - a[0]) * bc; a[1] += (a[0] - a[1]) * bc;
            b[0] += (vr - b[0]) * bc; b[1] += (b[0] - b[1]) * bc;
            sfx_write(f, a[1] + FX_DENORM, b[1] + FX_DENORM);
        } else if (type == SFX_TAPE) {   /* head loss (tone), bias: a low cut in the loop and record saturation */
            f->lpl += (el - f->lpl) * tc; f->lpr += (er - f->lpr) * tc;
            el = f->lpl; er = f->lpr;
            f->hpl += (el - f->hpl) * hpc; f->hpr += (er - f->hpr) * hpc;
            sfx_write(f, fx_sat((xl + p->fb * (el - f->hpl)) * drive) / drive + FX_DENORM,
                      fx_sat((xr + p->fb * (er - f->hpr)) * drive) / drive + FX_DENORM);
        } else {   /* clean delay and echo: the tone low-pass inside the feedback loop */
            f->lpl += (el - f->lpl) * tc; f->lpr += (er - f->lpr) * tc;
            sfx_write(f, xl + p->fb * f->lpl + FX_DENORM, xr + p->fb * f->lpr + FX_DENORM);
            if (type == SFX_ECHO) {   /* the echoes through a small spring-like diffusing tank */
                float yl, yr, tl = DL_TAP(f->etl, at - 1), tr = DL_TAP(f->etr, bt - 1);
                f->edl += (tl - f->edl) * 0.5f; f->edr += (tr - f->edr) * 0.5f;
                AP(f->e1l, a1, 0.6f, f->lpl, yl); AP(f->e2l, a2, 0.6f, yl, yl); AP(f->e3l, a3, 0.6f, yl, yl);
                AP(f->e1r, b1, 0.6f, f->lpr, yr); AP(f->e2r, b2, 0.6f, yr, yr); AP(f->e3r, b3, 0.6f, yr, yr);
                yl += efb * f->edr + FX_DENORM;   /* the two sides cross-feed: a wider tail */
                yr += efb * f->edl + FX_DENORM;
                DL_WRITE(f->etl, yl); DL_WRITE(f->etr, yr);
                el = 0.4f * f->lpl + 0.8f * yl;
                er = 0.4f * f->lpr + 0.8f * yr;
            } else if (type == SFX_DELAY) {
                el = f->lpl; er = f->lpr;
            }
        }
        wl[i] = el; wr[i] = er;
    }
}

/* ---- bit crusher: sample and hold at the set rate, then quantised to the bit depth ---- */
static void sfx_crush(sfx_t *f, const fx_params_t *p, const float *inL, const float *inR, float *wl, float *wr, int n) {
    const float inc = p->srate_hz / FX_SR, steps = exp2f(fx_clamp(p->bits, 2.0f, 24.0f) - 1.0f), inv = 1.0f / steps;
    for (int i = 0; i < n; i++) {
        f->acc += inc;
        if (f->acc >= 1.0f) {
            f->acc -= floorf(f->acc);
            f->hold_l = floorf(fx_clamp(inL[i], -1.0f, 1.0f) * steps + 0.5f) * inv;
            f->hold_r = floorf(fx_clamp(inR[i], -1.0f, 1.0f) * steps + 0.5f) * inv;
        }
        wl[i] = f->hold_l; wr[i] = f->hold_r;
    }
}

/* ---- resonator: three state-variable filters (low, mid, high) in parallel, cutoffs swept together by an LFO ---- */
static void sfx_reson(sfx_t *f, const fx_params_t *p, const float *inL, const float *inR, float *wl, float *wr, int n) {
    const float k = 2.0f - 1.98f * fx_clamp(p->reso, 0.0f, 1.0f);   /* damping: Q 0.5 .. 50 */
    const float norm = p->ftype == RES_BP ? k : sqrtf(0.5f * k);    /* a resonant peak about as loud as the input */
    float a1[3], a2[3], a3[3];
    int at = 0;
    while (at < n) {
        int m = n - at < 16 ? n - at : 16;   /* coefficients every 16 samples */
        float sweep = exp2f(2.0f * p->depth * fx_tri(f->ph));
        for (int j = 0; j < 3; j++) {
            float g = tanf(FX_PI * fminf(p->cut_hz[j] * sweep, 0.45f * FX_SR) / FX_SR);
            a1[j] = 1.0f / (1.0f + g * (g + k)); a2[j] = g * a1[j]; a3[j] = g * a2[j];
        }
        for (int i = at; i < at + m; i++) {
            float x[2] = {inL[i], inR[i]}, y[2] = {0.0f, 0.0f};
            for (int c = 0; c < 2; c++)
                for (int j = 0; j < 3; j++) {
                    float *s = f->rs[c][j];
                    float v3 = x[c] - s[1], v1 = a1[j] * s[0] + a2[j] * v3, v2 = s[1] + a2[j] * s[0] + a3[j] * v3;
                    s[0] = 2.0f * v1 - s[0] + FX_DENORM;
                    s[1] = 2.0f * v2 - s[1] + FX_DENORM;
                    float o = p->ftype == RES_LP ? v2 : p->ftype == RES_BP ? v1 : x[c] - k * v1 - v2;
                    y[c] += p->lvl[j] * o;
                }
            wl[i] = 0.6f * norm * y[0];
            wr[i] = 0.6f * norm * y[1];
        }
        f->ph = fx_wrap(f->ph + p->rate_hz * (float)m / FX_SR);
        at += m;
    }
}

/* the synth effect's wet signal for n samples */
static void sfx_process(sfx_t *f, const fx_params_t *p, const float *inL, const float *inR, float *wl, float *wr, int n) {
    switch (p->sfx_type) {
    case SFX_DELAY: case SFX_BBD: case SFX_TAPE: case SFX_ECHO: sfx_delays(f, p, inL, inR, wl, wr, n); break;
    case SFX_PHASER6: case SFX_PHASER12: case SFX_TPHASER: sfx_phaser(f, p, inL, inR, wl, wr, n); break;
    case SFX_CRUSH: sfx_crush(f, p, inL, inR, wl, wr, n); break;
    case SFX_RESON: sfx_reson(f, p, inL, inR, wl, wr, n); break;
    default: sfx_mod_lines(f, p, inL, inR, wl, wr, n); break;
    }
}

/* a fresh start (switched on, or another type): silence, and the times start where they are set */
static void sfx_reset(sfx_t *f, const fx_params_t *p) {
    memset(f, 0, sizeof *f);
    f->rng = 0x1234567u;
    f->dl = p->sfx_type <= SFX_ECHO || p->sfx_type == SFX_TPHASER ? sfx_clamp_dist(p->dtime_ms * 0.001f * FX_SR)
                                                                   : p->time_ms * 0.001f * FX_SR;
    f->dr = (p->sfx_type == SFX_SFLANGER || p->sfx_type == SFX_SCHORUS ? p->time_r_ms : p->time_ms) * 0.001f * FX_SR;
    f->al = f->ar = sfx_ap_coef(p, 0.0f);
}
