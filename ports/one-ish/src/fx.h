/* ONE-ish effects, after the voices, stereo out: a modulation effect (chorus, flanger or phaser), a delay (free or
 * tempo-synced, feedback tone, ping-pong) and a plate reverb. All buffers live inside fx_t (allocated with the
 * instance); fx_process() never allocates. Designs are openly published ones:
 *   chorus / flanger: modulated delay lines, read with 4-point Hermite interpolation
 *   phaser: six first-order all-pass stages per channel swept by an LFO, with feedback
 *   reverb: J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters" (JAES 1997), the figure-of-eight
 *           plate tank, its lengths scaled from 29761 Hz to 44100 Hz */
#pragma once
#include <math.h>
#include <string.h>

enum { MODFX_OFF, MODFX_CHORUS, MODFX_FLANGER, MODFX_PHASER };   /* option order in params.json */

#define FX_SR 44100.0f
#define DLY_SIZE 131072      /* 2.97 s per channel: delay times up to 2 s */
#define MOD_SIZE 4096        /* 93 ms: chorus and flanger */
#define PHASER_STAGES 6

typedef struct {   /* what the effects read; worked out by the engine from the knobs */
    int mod_type;
    float mod_rate_hz, mod_depth, mod_fb, mod_mix;   /* depth, mix 0..1; feedback -0.95..0.95 */
    float dly_ms, dly_fb, dly_tone, dly_mix;         /* tone 0..1 (dark..bright) */
    int dly_pingpong;
    float rev_decay, rev_damp, rev_predelay_ms, rev_mix;
} fx_params_t;

/* fixed-length delay line over a power-of-two buffer */
#define DL(name, size) struct { float buf[size]; int w; } name
#define DL_MASK(d) ((int)(sizeof (d).buf / sizeof (d).buf[0]) - 1)
#define DL_WRITE(d, x) do { (d).buf[(d).w] = (x); (d).w = ((d).w + 1) & DL_MASK(d); } while (0)
#define DL_TAP(d, n) ((d).buf[((d).w - 1 - (n)) & DL_MASK(d)])   /* the sample written n writes ago */

typedef struct {
    /* modulation effects */
    float mod_l[MOD_SIZE], mod_r[MOD_SIZE];
    int mod_w;
    float mod_phase, mod_fb_l, mod_fb_r;
    float ph_l[PHASER_STAGES], ph_r[PHASER_STAGES];
    /* delay */
    float dly_l[DLY_SIZE], dly_r[DLY_SIZE];
    int dly_w;
    float dly_pos;            /* the read distance in samples, gliding to the set time (no zipper on a time change) */
    float dly_lp_l, dly_lp_r;
    /* plate reverb */
    DL(pre, 16384);
    float bw;                  /* input bandwidth filter state */
    DL(in1, 256); DL(in2, 256); DL(in3, 1024); DL(in4, 512);
    DL(apA, 2048); DL(dA1, 8192); DL(apA2, 4096); DL(dA2, 8192);   /* left half of the tank */
    DL(apB, 2048); DL(dB1, 8192); DL(apB2, 4096); DL(dB2, 8192);   /* right half */
    float dampA, dampB, rev_phase;
    int mod_on, dly_on, rev_on;   /* last block's state: an effect turned back on starts from silence */
} fx_t;

static inline float fx_hermite(const float *buf, int mask, int w, float dist) {   /* read `dist` samples back */
    float pos = (float)w - 1.0f - dist;
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    float xm1 = buf[(i - 1) & mask], x0 = buf[i & mask], x1 = buf[(i + 1) & mask], x2 = buf[(i + 2) & mask];
    float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

/* Schroeder all-pass on a fixed delay of n samples: w = x + g*d, y = d - g*w */
#define AP(d, n, g, x, y) do { float d_ = DL_TAP(d, (n) - 1); float w_ = (x) + (g) * d_; DL_WRITE(d, w_); \
                               (y) = d_ - (g) * w_; } while (0)

/* Dattorro's lengths at 29761 Hz, scaled */
#define RV(n) ((int)((n) * (FX_SR / 29761.0f) + 0.5f))

static inline void fx_clear_mod(fx_t *f) {
    memset(f->mod_l, 0, sizeof f->mod_l); memset(f->mod_r, 0, sizeof f->mod_r);
    memset(f->ph_l, 0, sizeof f->ph_l); memset(f->ph_r, 0, sizeof f->ph_r);
    f->mod_fb_l = f->mod_fb_r = 0.0f;
}

static inline void fx_clear_delay(fx_t *f) {
    memset(f->dly_l, 0, sizeof f->dly_l); memset(f->dly_r, 0, sizeof f->dly_r);
    f->dly_lp_l = f->dly_lp_r = 0.0f;
}

static inline void fx_clear_reverb(fx_t *f) {
    memset(&f->pre, 0, (char *)&f->dampA - (char *)&f->pre);   /* every reverb line, pre-delay to dB2 */
    f->dampA = f->dampB = f->bw = 0.0f;
}

static inline float fx_tri(float ph) { return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; }

static void fx_mod(fx_t *f, const fx_params_t *p, const float *in, float *l, float *r, int n) {
    float inc = p->mod_rate_hz / FX_SR, wet = p->mod_mix, dry = 1.0f - 0.5f * p->mod_mix;
    for (int i = 0; i < n; i++) {
        float ph = f->mod_phase, ph2 = ph + 0.25f;   /* the right channel a quarter cycle on: stereo width */
        if (ph2 >= 1.0f) ph2 -= 1.0f;
        float x = in[i], yl, yr;
        if (p->mod_type == MODFX_PHASER) {
            float lfo_l = 0.5f + 0.5f * fx_tri(ph), lfo_r = 0.5f + 0.5f * fx_tri(ph2);
            /* sweep 200 Hz .. 200 Hz * 2^(5 * depth) on a log scale; first-order all-pass coefficient from tan */
            float hz_l = 200.0f * exp2f(5.0f * p->mod_depth * lfo_l), hz_r = 200.0f * exp2f(5.0f * p->mod_depth * lfo_r);
            float tl = tanf(3.14159265f * hz_l / FX_SR), tr = tanf(3.14159265f * hz_r / FX_SR);
            float al = (tl - 1.0f) / (tl + 1.0f), ar = (tr - 1.0f) / (tr + 1.0f);
            float sl = x + p->mod_fb * f->mod_fb_l, sr = x + p->mod_fb * f->mod_fb_r;
            for (int k = 0; k < PHASER_STAGES; k++) {   /* y = a*x + s; s = x - a*y */
                float y = al * sl + f->ph_l[k]; f->ph_l[k] = sl - al * y; sl = y;
                y = ar * sr + f->ph_r[k]; f->ph_r[k] = sr - ar * y; sr = y;
            }
            f->mod_fb_l = sl; f->mod_fb_r = sr;
            yl = sl; yr = sr;
        } else {
            float base = p->mod_type == MODFX_FLANGER ? 1.0f : 7.0f;      /* ms */
            float swing = p->mod_type == MODFX_FLANGER ? 4.0f : 6.0f;
            float fb = p->mod_type == MODFX_FLANGER ? p->mod_fb : 0.0f;
            float dl = (base + swing * p->mod_depth * (0.5f + 0.5f * fx_tri(ph))) * 0.001f * FX_SR;
            float dr = (base + swing * p->mod_depth * (0.5f + 0.5f * fx_tri(ph2))) * 0.001f * FX_SR;
            f->mod_l[f->mod_w] = x + fb * f->mod_fb_l;
            f->mod_r[f->mod_w] = x + fb * f->mod_fb_r;
            f->mod_w = (f->mod_w + 1) & (MOD_SIZE - 1);
            yl = fx_hermite(f->mod_l, MOD_SIZE - 1, f->mod_w, dl);
            yr = fx_hermite(f->mod_r, MOD_SIZE - 1, f->mod_w, dr);
            f->mod_fb_l = yl; f->mod_fb_r = yr;
        }
        l[i] = x * dry + yl * wet;
        r[i] = x * dry + yr * wet;
        f->mod_phase = ph + inc >= 1.0f ? ph + inc - 1.0f : ph + inc;
    }
}

static void fx_delay(fx_t *f, const fx_params_t *p, float *l, float *r, int n) {
    float target = p->dly_ms * 0.001f * FX_SR;
    if (target > DLY_SIZE - 4) target = DLY_SIZE - 4;
    if (target < 1.0f) target = 1.0f;
    float tone = 0.05f + 0.95f * p->dly_tone * p->dly_tone;   /* one-pole low-pass in the feedback path */
    for (int i = 0; i < n; i++) {
        f->dly_pos += (target - f->dly_pos) * 0.0005f;          /* about 45 ms to glide to a new time */
        float el = fx_hermite(f->dly_l, DLY_SIZE - 1, f->dly_w, f->dly_pos);
        float er = fx_hermite(f->dly_r, DLY_SIZE - 1, f->dly_w, f->dly_pos);
        f->dly_lp_l += (el - f->dly_lp_l) * tone;
        f->dly_lp_r += (er - f->dly_lp_r) * tone;
        float in_l = l[i], in_r = r[i];
        if (p->dly_pingpong) {   /* the input enters on the left; each repeat crosses to the other side */
            f->dly_l[f->dly_w] = 0.5f * (in_l + in_r) + p->dly_fb * f->dly_lp_r;
            f->dly_r[f->dly_w] = p->dly_fb * f->dly_lp_l;
        } else {
            f->dly_l[f->dly_w] = in_l + p->dly_fb * f->dly_lp_l;
            f->dly_r[f->dly_w] = in_r + p->dly_fb * f->dly_lp_r;
        }
        f->dly_w = (f->dly_w + 1) & (DLY_SIZE - 1);
        l[i] = in_l + el * p->dly_mix;
        r[i] = in_r + er * p->dly_mix;
    }
}

static void fx_reverb(fx_t *f, const fx_params_t *p, float *l, float *r, int n) {
    float decay = p->rev_decay, damp = p->rev_damp;
    int pre = (int)(p->rev_predelay_ms * 0.001f * FX_SR);
    if (pre > 16380) pre = 16380;
    for (int i = 0; i < n; i++) {
        float x = 0.5f * (l[i] + r[i]);
        DL_WRITE(f->pre, x);
        float v = DL_TAP(f->pre, pre);
        f->bw += (v - f->bw) * 0.9995f;
        AP(f->in1, RV(142), 0.75f, f->bw, v);
        AP(f->in2, RV(107), 0.75f, v, v);
        AP(f->in3, RV(379), 0.625f, v, v);
        AP(f->in4, RV(277), 0.625f, v, v);
        /* the tank: each half's input is the diffused signal plus the other half's output */
        f->rev_phase += 1.0f / FX_SR;
        if (f->rev_phase >= 1.0f) f->rev_phase -= 1.0f;
        float exc = RV(16) * sinf(6.2831853f * f->rev_phase);
        float a_in = v + decay * DL_TAP(f->dB2, RV(3163) - 1), b_in = v + decay * DL_TAP(f->dA2, RV(3720) - 1);
        /* modulated all-pass (decay diffusion 1, sign as in the paper's tank) */
        float da = fx_hermite(f->apA.buf, DL_MASK(f->apA), f->apA.w, RV(672) - 1 + exc);
        float wa = a_in - 0.7f * da; DL_WRITE(f->apA, wa); float ya = da + 0.7f * wa;
        float db = fx_hermite(f->apB.buf, DL_MASK(f->apB), f->apB.w, RV(908) - 1 - exc);
        float wb = b_in - 0.7f * db; DL_WRITE(f->apB, wb); float yb = db + 0.7f * wb;
        DL_WRITE(f->dA1, ya);
        DL_WRITE(f->dB1, yb);
        f->dampA += ((DL_TAP(f->dA1, RV(4453) - 1)) - f->dampA) * (1.0f - damp);
        f->dampB += ((DL_TAP(f->dB1, RV(4217) - 1)) - f->dampB) * (1.0f - damp);
        float ta, tb;
        AP(f->apA2, RV(1800), 0.5f, decay * f->dampA, ta);
        AP(f->apB2, RV(2656), 0.5f, decay * f->dampB, tb);
        DL_WRITE(f->dA2, ta);
        DL_WRITE(f->dB2, tb);
        /* output taps (the paper's table), scaled */
        float yl = DL_TAP(f->dB1, RV(266)) + DL_TAP(f->dB1, RV(2974)) - DL_TAP(f->apB2, RV(1913)) + DL_TAP(f->dB2, RV(1996))
                   - DL_TAP(f->dA1, RV(1990)) - DL_TAP(f->apA2, RV(187)) - DL_TAP(f->dA2, RV(1066));
        float yr = DL_TAP(f->dA1, RV(353)) + DL_TAP(f->dA1, RV(3627)) - DL_TAP(f->apA2, RV(1228)) + DL_TAP(f->dA2, RV(2673))
                   - DL_TAP(f->dB1, RV(2111)) - DL_TAP(f->apB2, RV(335)) - DL_TAP(f->dB2, RV(121));
        l[i] += 0.6f * yl * p->rev_mix;
        r[i] += 0.6f * yr * p->rev_mix;
    }
}

/* in: the voices' mono sum; out: l, r. Effects with their mix at 0 are skipped; one switched back on starts clean. */
static void fx_process(fx_t *f, const fx_params_t *p, const float *in, float *l, float *r, int n) {
    int mod_on = p->mod_type != MODFX_OFF && p->mod_mix > 0.0f;
    int dly_on = p->dly_mix > 0.0f, rev_on = p->rev_mix > 0.0f;
    if (mod_on && !f->mod_on) fx_clear_mod(f);
    if (dly_on && !f->dly_on) { fx_clear_delay(f); f->dly_pos = p->dly_ms * 0.001f * FX_SR; }
    if (rev_on && !f->rev_on) fx_clear_reverb(f);
    f->mod_on = mod_on; f->dly_on = dly_on; f->rev_on = rev_on;
    if (mod_on) fx_mod(f, p, in, l, r, n);
    else for (int i = 0; i < n; i++) l[i] = r[i] = in[i];
    if (dly_on) fx_delay(f, p, l, r, n);
    if (rev_on) fx_reverb(f, p, l, r, n);
}
