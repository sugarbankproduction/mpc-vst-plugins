/* ONE-ish master reverb (master effect 2, fed by SEND 2): one of three original designs on a mono input, stereo out.
 *   PLATE: J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters" (JAES 1997): the figure-of-eight tank,
 *          its lengths scaled from 29761 Hz to 44100 Hz and then by SIZE; DIFFUSION scales the input diffusers.
 *   ROOM:  early reflections (six taps per side off the pre-delay line) and a small late field: two input diffusers
 *          and a four-line feedback delay network (orthogonal Hadamard mixing, J.-M. Jot's design).
 *   HALL:  longer early reflections, four input diffusers and an eight-line network with four slowly modulated lines.
 * Decay is set as a time (RT60, -60 dB) and each loop's gain worked out from its length, so SIZE stretches the space and,
 * like a bigger room, the tail (the time scales with size). Damping: one-pole high cut and a low cut in every loop.
 * FREEZE: the loops at unity gain, undamped, the input shut: the tail holds. Returns the wet signal.
 *   SPACE: Airwindows Galactic (airwindows.h), stereo in: DECAY sets how little of the new sound replaces the space
 *          (full decay or FREEZE: it holds), TONE its brightness, MOD LEVEL its detune, SIZE its bigness (moving SIZE
 *          makes the crashing noises the plugin is known for). Pre-delay, damping, diffusion and early/late don't
 *          apply to it. */
#pragma once
#include "common.h"
#include "airwindows.h"

#define RV_PRE_SIZE 131072   /* 2.97 s: pre-delay up to 1.5 s plus the early taps */
#define RV(n) ((n) * (FX_SR / 29761.0f))   /* Dattorro's lengths at 29761 Hz, scaled */
#define ROOM_LINE 4096
#define HALL_LINE 16384

typedef struct {
    DL(in1, 256); DL(in2, 256); DL(in3, 1024); DL(in4, 512);
    DL(apA, 4096); DL(dA1, 16384); DL(apA2, 8192); DL(dA2, 16384);   /* left half of the tank */
    DL(apB, 4096); DL(dB1, 16384); DL(apB2, 8192); DL(dB2, 16384);   /* right half */
    float dampA, dampB, lowA, lowB, ph;
} plate_t;

typedef struct {
    DL(ap1, 512); DL(ap2, 512);
    float line[4][ROOM_LINE];
    int w;
    float damp[4], low[4];
} room_t;

typedef struct {
    DL(ap1, 512); DL(ap2, 512); DL(ap3, 512); DL(ap4, 512);
    float line[8][HALL_LINE];
    int w;
    float damp[8], low[8], ph;
} hall_t;

typedef struct {
    DL(pre, RV_PRE_SIZE);   /* mono pre-delay (and the early reflections' taps) */
    float bw;               /* input tone low-pass */
    int type;               /* the design the state below belongs to */
    union { plate_t plate; room_t room; hall_t hall; aw_galactic_t space; } u;
} rev_t;

static void rv_reset(rev_t *r, const fx_params_t *p) {
    memset(r, 0, sizeof *r);
    r->type = p->rev_type;
    if (r->type == REV_SPACE) aw_galactic_init(&r->u.space);
}

/* the loop settings this block: gain per loop of `len` samples, damping coefficients (frozen: lossless) */
typedef struct { float rt, hi, lo, low_c, in; int freeze; } rv_loop_t;
static rv_loop_t rv_loop(const fx_params_t *p) {
    rv_loop_t k;
    k.freeze = p->rev_freeze;
    k.rt = fmaxf(p->rev_rt60, 0.05f);
    k.hi = k.freeze ? 1.0f : 1.0f - fx_clamp(p->rev_damp, 0.0f, 0.95f);   /* one-pole coefficient: 1 = no damping */
    k.lo = k.freeze ? 0.0f : fx_clamp(p->rev_lowdamp, 0.0f, 1.0f);         /* share of the low band taken out */
    k.low_c = fx_lp_coef(250.0f);
    k.in = k.freeze ? 0.0f : 1.0f;
    return k;
}
static inline float rv_gain(const rv_loop_t *k, float len) {
    return k->freeze ? 1.0f : fminf(powf(10.0f, -3.0f * len / (FX_SR * k->rt)), 0.999f);
}
/* one loop's damping: high cut (state *d), then low cut (state *lo) */
static inline float rv_damp(const rv_loop_t *k, float x, float *d, float *lo) {
    *d += (x - *d) * k->hi;
    *lo += (*d - *lo) * k->low_c;
    return *d - k->lo * *lo;
}

/* the pre-delay and input tone; returns this sample's input to the reverb */
static inline float rv_input(rev_t *r, float x, int pre, float tone_c) {
    DL_WRITE(r->pre, x);
    float v = DL_TAP(r->pre, pre);
    r->bw += (v - r->bw) * tone_c;
    return r->bw;
}

/* six early reflections per side off the pre-delay line, after the pre-delay; `sc` stretches them */
static inline void rv_early(rev_t *r, int pre, float sc, float *el, float *er) {
    static const int TL[6] = {313, 499, 791, 1039, 1399, 1823}, TR[6] = {367, 577, 869, 1193, 1559, 2027};
    static const float GL[6] = {0.85f, -0.7f, 0.6f, -0.5f, 0.4f, -0.32f}, GR[6] = {-0.8f, 0.72f, -0.58f, 0.52f, -0.42f, 0.3f};
    float l = 0.0f, rr = 0.0f;
    for (int k = 0; k < 6; k++) {
        l += GL[k] * DL_TAP(r->pre, pre + (int)(TL[k] * sc));
        rr += GR[k] * DL_TAP(r->pre, pre + (int)(TR[k] * sc));
    }
    *el = 0.5f * l;
    *er = 0.5f * rr;
}

static void rv_plate(rev_t *r, const fx_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                     int n, int pre, float tone_c) {
    plate_t *t = &r->u.plate;
    const rv_loop_t k = rv_loop(p);
    const float s = fx_clamp(p->rev_size, 0.5f, 2.0f), d1 = 0.75f * p->rev_diff, d2 = 0.625f * p->rev_diff;
#define SZ(x) ((int)(RV(x) * s + 0.5f))
    const int apA = SZ(672), dA1 = SZ(4453), apA2 = SZ(1800), dA2 = SZ(3720);
    const int apB = SZ(908), dB1 = SZ(4217), apB2 = SZ(2656), dB2 = SZ(3163);
    /* the decay is applied twice per half of the tank */
    const float g = rv_gain(&k, 0.5f * (float)(apA + dA1 + apA2 + dA2 + apB + dB1 + apB2 + dB2) * 0.5f);
    const float exc_amt = RV(16) * fx_clamp(p->rev_mod, 0.0f, 2.0f);
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c);
        AP(t->in1, (int)RV(142), d1, v, v);
        AP(t->in2, (int)RV(107), d1, v, v);
        AP(t->in3, (int)RV(379), d2, v, v);
        AP(t->in4, (int)RV(277), d2, v, v);
        /* the tank: each half's input is the diffused signal plus the other half's output */
        t->ph = fx_wrap(t->ph + 1.0f / FX_SR);
        float exc = exc_amt * fx_sin(t->ph);
        float a_in = v + g * DL_TAP(t->dB2, dB2 - 1) + FX_DENORM, b_in = v + g * DL_TAP(t->dA2, dA2 - 1) + FX_DENORM;
        /* modulated all-pass (decay diffusion 1, sign as in the paper's tank) */
        float da = fx_hermite(t->apA.buf, DL_MASK(t->apA), t->apA.w, (float)apA - 1.0f + exc);
        float wa = a_in - 0.7f * da; DL_WRITE(t->apA, wa); float ya = da + 0.7f * wa;
        float db = fx_hermite(t->apB.buf, DL_MASK(t->apB), t->apB.w, (float)apB - 1.0f - exc);
        float wb = b_in - 0.7f * db; DL_WRITE(t->apB, wb); float yb = db + 0.7f * wb;
        DL_WRITE(t->dA1, ya);
        DL_WRITE(t->dB1, yb);
        float fa = rv_damp(&k, DL_TAP(t->dA1, dA1 - 1), &t->dampA, &t->lowA);
        float fb = rv_damp(&k, DL_TAP(t->dB1, dB1 - 1), &t->dampB, &t->lowB);
        float ta, tb;
        AP(t->apA2, apA2, 0.5f, g * fa, ta);
        AP(t->apB2, apB2, 0.5f, g * fb, tb);
        DL_WRITE(t->dA2, ta);
        DL_WRITE(t->dB2, tb);
        /* output taps (the paper's table), scaled */
        float yl = DL_TAP(t->dB1, SZ(266)) + DL_TAP(t->dB1, SZ(2974)) - DL_TAP(t->apB2, SZ(1913)) + DL_TAP(t->dB2, SZ(1996))
                   - DL_TAP(t->dA1, SZ(1990)) - DL_TAP(t->apA2, SZ(187)) - DL_TAP(t->dA2, SZ(1066));
        float yr = DL_TAP(t->dA1, SZ(353)) + DL_TAP(t->dA1, SZ(3627)) - DL_TAP(t->apA2, SZ(1228)) + DL_TAP(t->dA2, SZ(2673))
                   - DL_TAP(t->dB1, SZ(2111)) - DL_TAP(t->apB2, SZ(335)) - DL_TAP(t->dB2, SZ(121));
        outL[i] = 0.6f * yl;
        outR[i] = 0.6f * yr;
    }
#undef SZ
}

/* orthogonal mixing: Hadamard 4 and 8, normalised (energy kept) */
static inline void rv_had4(float *x) {
    float a = x[0] + x[1], b = x[0] - x[1], c = x[2] + x[3], d = x[2] - x[3];
    x[0] = 0.5f * (a + c); x[1] = 0.5f * (b + d); x[2] = 0.5f * (a - c); x[3] = 0.5f * (b - d);
}
static inline void rv_had8(float *x) {
    rv_had4(x);
    rv_had4(x + 4);
    for (int i = 0; i < 4; i++) {
        float a = x[i], b = x[i + 4];
        x[i] = 0.70710678f * (a + b);
        x[i + 4] = 0.70710678f * (a - b);
    }
}

static void rv_room(rev_t *r, const fx_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                    int n, int pre, float tone_c, float eg, float lg) {
    static const int BASE[4] = {887, 1153, 1427, 1709};
    room_t *t = &r->u.room;
    const rv_loop_t k = rv_loop(p);
    const float s = fx_clamp(p->rev_size, 0.5f, 2.0f), dg = 0.7f * p->rev_diff;
    int len[4];
    float g[4];
    for (int j = 0; j < 4; j++) { len[j] = (int)(BASE[j] * s); g[j] = rv_gain(&k, (float)len[j]); }
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c), el, er;
        rv_early(r, pre, s, &el, &er);
        AP(t->ap1, 131, dg, v, v);
        AP(t->ap2, 199, dg, v, v);
        float o[4], x[4];
        for (int j = 0; j < 4; j++) {
            o[j] = t->line[j][(t->w - len[j]) & (ROOM_LINE - 1)];
            x[j] = g[j] * rv_damp(&k, o[j], &t->damp[j], &t->low[j]);
        }
        rv_had4(x);
        for (int j = 0; j < 4; j++) t->line[j][t->w] = x[j] + ((j & 1) ? -0.5f : 0.5f) * v + FX_DENORM;
        t->w = (t->w + 1) & (ROOM_LINE - 1);
        outL[i] = eg * el + lg * 0.5f * (o[0] - o[1] + o[2] - o[3]);
        outR[i] = eg * er + lg * 0.5f * (o[0] + o[1] - o[2] - o[3]);
    }
}

static void rv_hall(rev_t *r, const fx_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                    int n, int pre, float tone_c, float eg, float lg) {
    static const int BASE[8] = {1931, 2251, 2647, 3001, 3413, 3797, 4153, 4597};
    hall_t *t = &r->u.hall;
    const rv_loop_t k = rv_loop(p);
    const float s = fx_clamp(p->rev_size, 0.5f, 2.0f), dg = 0.7f * p->rev_diff;
    const float depth = 8.0f * fx_clamp(p->rev_mod, 0.0f, 2.0f);
    int len[8];
    float g[8];
    for (int j = 0; j < 8; j++) { len[j] = (int)(BASE[j] * s); g[j] = rv_gain(&k, (float)len[j]); }
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c), el, er;
        rv_early(r, pre, 1.8f * s, &el, &er);
        AP(t->ap1, 113, dg, v, v);
        AP(t->ap2, 167, dg, v, v);
        AP(t->ap3, 251, dg, v, v);
        AP(t->ap4, 347, dg, v, v);
        t->ph = fx_wrap(t->ph + 0.37f / FX_SR);
        float o[8], x[8];
        for (int j = 0; j < 8; j++) {
            if (j & 1) o[j] = t->line[j][(t->w - len[j]) & (HALL_LINE - 1)];
            else {   /* four lines swing slowly, a quarter cycle apart: a smoother, less metallic tail */
                float ex = depth * (1.0f + fx_sin(fx_wrap(t->ph + 0.125f * (float)j)));
                o[j] = fx_hermite(t->line[j], HALL_LINE - 1, t->w, (float)len[j] - 1.0f + ex);
            }
            x[j] = g[j] * rv_damp(&k, o[j], &t->damp[j], &t->low[j]);
        }
        rv_had8(x);
        for (int j = 0; j < 8; j++) t->line[j][t->w] = x[j] + ((j & 1) ? -0.35f : 0.35f) * v + FX_DENORM;
        t->w = (t->w + 1) & (HALL_LINE - 1);
        outL[i] = eg * el + lg * 0.55f * (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]);
        outR[i] = eg * er + lg * 0.55f * (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7]);
    }
}

/* in: what SEND 2 (and the delay, in series) feeds it; out: the reverb */
static void rv_process(rev_t *r, const fx_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                       int n) {
    if (p->rev_type == REV_SPACE) {
        float replace = p->rev_freeze ? 0.0f : 1.0f - fx_clamp(p->rev_decay_k, 0.0f, 1.0f);
        aw_galactic(&r->u.space, replace, fx_clamp(p->rev_tone, 0.0f, 1.0f), fx_clamp(0.5f * p->rev_mod, 0.0f, 1.0f),
                    fx_clamp(p->rev_size_k, 0.0f, 1.0f), 1.0f, inL, inR, outL, outR, n);
        return;
    }
    int pre = (int)fx_clamp(p->rev_pre_ms * 0.001f * FX_SR, 0.0f, 66200.0f);
    float tone_c = fx_tone_coef(p->rev_tone);
    float b = fx_clamp(p->rev_early, 0.0f, 1.0f), eg = fminf(1.0f, 2.0f * (1.0f - b)), lg = fminf(1.0f, 2.0f * b);
    if (p->rev_type == REV_ROOM) rv_room(r, p, inL, inR, outL, outR, n, pre, tone_c, eg, lg);
    else if (p->rev_type == REV_HALL) rv_hall(r, p, inL, inR, outL, outR, n, pre, tone_c, eg, lg);
    else rv_plate(r, p, inL, inR, outL, outR, n, pre, tone_c);
}
