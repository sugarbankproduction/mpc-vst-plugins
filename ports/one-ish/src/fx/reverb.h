/* ONE-ish master reverbs (a master effect slot running a reverb type): original designs on a mono input (SPACE: stereo),
 * stereo out.
 *   PLATE: J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters" (JAES 1997): the figure-of-eight tank,
 *          its lengths scaled from 29761 Hz to 44100 Hz and then by SIZE; DIFFUSION scales the input diffusers.
 *          DISTANCE adds early reflections off the pre-delay line, spread wider as it rises (0: none, as before).
 *   ROOM:  early reflections (six taps per side off the pre-delay line) and a small late field: two input diffusers
 *          and a four-line feedback delay network (orthogonal Hadamard mixing, J.-M. Jot's design). The early/late
 *          balance moves in the manual's 21 detents (all early .. E10+L10 .. all late); MOD LEVEL swings two of the
 *          lines; HIGH LEVEL / LOW LEVEL set the late sound's bands above and below HIGH FREQ.
 *   HALL:  longer early reflections, four input diffusers and an eight-line network with four slowly modulated lines;
 *          LOW DECAY / HIGH DECAY stretch or shorten the decay below 250 Hz and above HIGH FREQ (x0.1..5), and
 *          LOW / MID / HIGH LEVEL set the bands of what comes out.
 *   SHIMMER (in the spirit of the pitch-shifted reverbs, an original design): the hall's network fed by the input and
 *          by a pitch loop: the input goes round a loop of 0.1..0.4 s (by SIZE) through two pitch shifters at the A
 *          and B intervals (a two-tap delay-line shifter, sine windows), so each pass climbs again; PITCH DECAY is
 *          how much goes round (0: one shifted layer). PITCH FREEZE holds the pitch loop as it is (it stops shifting
 *          and taking input) while the reverb carries on; FREEZE holds both.
 *   BLACKHOLE (in the spirit of the huge modulated spaces, an original design): four long modulated diffusers into
 *          an eight-line network of 0.06..0.57 s lines, every line swinging (MOD DEPTH / MOD RATE); FEEDBACK sets a
 *          decay of 1..100 s; GRAVITY below 0 makes the diffusers smear more (the sound swells in), above 0 pulls the
 *          tail down (shorter, a harder onset); the output's bands split at 700 Hz by a resonant state-variable
 *          crossover (RESONANCE: a peak there), LOW / HIGH LEVEL setting each side.
 * Decay is set as a time (RT60, -60 dB) and each loop's gain worked out from its length, so SIZE stretches the space and,
 * like a bigger room, the tail (the time scales with size). Damping: one-pole high cut and a low cut in every loop.
 * FREEZE: the loops at unity gain, undamped, the input shut: the tail holds. INFINITE: the loops at unity gain with the
 * input open (a soft limiter in the loops keeps it bounded). Returns the wet signal.
 *   SPACE: Airwindows Galactic (airwindows.h), stereo in: DECAY sets how little of the new sound replaces the space
 *          (full decay, FREEZE or INFINITE: it holds), TONE its brightness, MOD LEVEL its detune, SIZE its bigness
 *          (moving SIZE makes the crashing noises the plugin is known for). Pre-delay, damping, diffusion and
 *          early/late don't apply to it. */
#pragma once
#include "common.h"
#include "airwindows.h"

#define RV_PRE_SIZE 131072   /* 2.97 s: pre-delay up to 2 s plus the early taps */
#define RV_PRE_MAX 88200     /* 2 s */
#define RV(n) ((n) * (FX_SR / 29761.0f))   /* Dattorro's lengths at 29761 Hz, scaled */
#define ROOM_LINE 4096
#define HALL_LINE 16384
#define SHIM_LOOP 32768      /* the shimmer's pitch loop, up to 0.74 s */
#define PS_SIZE 4096         /* a pitch shifter's line */
#define PS_WIN 2048.0f       /* its window: 46 ms */
#define BH_LINE 32768
#define BH_AP 8192
#define BH_XOVER_HZ 700.0f

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
    float damp[4], low[4], ph;
} room_t;

typedef struct {
    DL(ap1, 512); DL(ap2, 512); DL(ap3, 512); DL(ap4, 512);
    float line[8][HALL_LINE];
    int w;
    float damp[8], low[8], ph;
    float bl[8], bh[8];      /* LOW / HIGH DECAY's band splits, per line */
} hall_t;

typedef struct {
    hall_t tank;
    float loop[SHIM_LOOP];   /* the pitch loop */
    int lw;
    float ps[PS_SIZE];       /* the shifters' line */
    int pw;
    float pph[2];            /* the A and B shifters' window phases */
    float lp, hp;            /* the pitch loop's tone */
} shim_t;

typedef struct {
    DL(d1, BH_AP); DL(d2, BH_AP); DL(d3, BH_AP); DL(d4, BH_AP);   /* the modulated diffusers */
    float line[8][BH_LINE];
    int w;
    float damp[8], ph;
    float sv[2][2];          /* the output crossover (a state-variable filter per channel) */
} bhole_t;

typedef struct {
    DL(pre, RV_PRE_SIZE);    /* mono pre-delay (and the early reflections' taps) */
    float bw;                /* input tone low-pass */
    int type;                /* the design the state below belongs to */
    float eq[2][2];          /* the output band levels' splits, per channel */
    union { plate_t plate; room_t room; hall_t hall; aw_galactic_t space; shim_t shim; bhole_t bh; } u;
} rev_t;

/* a fresh start in zeroed memory (fx.h clears it) */
static void rv_init(rev_t *r, const fxs_params_t *p) {
    r->type = p->type;
    if (r->type == REV_SPACE) aw_galactic_init(&r->u.space);
}

/* the loop settings this block: gain per loop of `len` samples, damping coefficients (frozen: lossless) */
typedef struct { float rt, hi, lo, low_c, in; int freeze, inf; } rv_loop_t;
static rv_loop_t rv_loop(const fxs_params_t *p) {
    rv_loop_t k;
    k.freeze = p->hold == HOLD_FREEZE;
    k.inf = p->hold == HOLD_INFINITE;
    k.rt = fmaxf(p->rev_rt60, 0.05f);
    k.hi = k.freeze ? 1.0f : 1.0f - fx_clamp(p->rev_damp, 0.0f, 0.95f);   /* one-pole coefficient: 1 = no damping */
    k.lo = k.freeze ? 0.0f : fx_clamp(p->rev_lowdamp, 0.0f, 1.0f);         /* share of the low band taken out */
    k.low_c = fx_lp_coef(250.0f);
    k.in = k.freeze ? 0.0f : 1.0f;
    return k;
}
static inline float rv_gain_rt(const rv_loop_t *k, float len, float rt) {
    return k->freeze || k->inf ? 1.0f : fminf(powf(10.0f, -3.0f * len / (FX_SR * rt)), 0.999f);
}
static inline float rv_gain(const rv_loop_t *k, float len) { return rv_gain_rt(k, len, k->rt); }
/* INFINITE: a lossless loop that still takes input, kept bounded by a soft limiter on what it writes */
static inline float rv_lim(const rv_loop_t *k, float x) { return k->inf ? 2.0f * fx_sat(0.5f * x) : x; }
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

static void rv_plate(rev_t *r, const fxs_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
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
    /* DISTANCE: early reflections (none at 0; they fade in over the first tenth), spread up to 3x the room's */
    const float dist = fx_clamp(p->distance, 0.0f, 1.0f), er_g = fminf(1.0f, 10.0f * dist) * 0.6f;
    const float er_sc = 0.25f + 2.75f * dist;
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
        if (k.inf) { a_in = rv_lim(&k, a_in); b_in = rv_lim(&k, b_in); }
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
        if (er_g > 0.0f) {
            float el, er;
            rv_early(r, pre, er_sc, &el, &er);
            outL[i] += er_g * el;
            outR[i] += er_g * er;
        }
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

static void rv_room(rev_t *r, const fxs_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                    int n, int pre, float tone_c, float eg, float lg) {
    static const int BASE[4] = {887, 1153, 1427, 1709};
    room_t *t = &r->u.room;
    const rv_loop_t k = rv_loop(p);
    const float s = fx_clamp(p->rev_size, 0.5f, 2.0f), dg = 0.7f * p->rev_diff;
    const float depth = 4.0f * fx_clamp(p->rev_mod, 0.0f, 2.0f);   /* MOD LEVEL: lines 0 and 2 swing (0: none) */
    int len[4];
    float g[4];
    for (int j = 0; j < 4; j++) { len[j] = (int)(BASE[j] * s); g[j] = rv_gain(&k, (float)len[j]); }
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c), el, er;
        rv_early(r, pre, s, &el, &er);
        AP(t->ap1, 131, dg, v, v);
        AP(t->ap2, 199, dg, v, v);
        float o[4], x[4];
        if (depth > 0.0f) t->ph = fx_wrap(t->ph + 0.53f / FX_SR);
        for (int j = 0; j < 4; j++) {
            if (depth > 0.0f && !(j & 1))
                o[j] = fx_hermite(t->line[j], ROOM_LINE - 1, t->w,
                                  (float)len[j] - 1.0f + depth * (1.0f + fx_sin(fx_wrap(t->ph + 0.25f * (float)j))));
            else
                o[j] = t->line[j][(t->w - len[j]) & (ROOM_LINE - 1)];
            x[j] = g[j] * rv_damp(&k, o[j], &t->damp[j], &t->low[j]);
        }
        rv_had4(x);
        for (int j = 0; j < 4; j++) t->line[j][t->w] = rv_lim(&k, x[j] + ((j & 1) ? -0.5f : 0.5f) * v + FX_DENORM);
        t->w = (t->w + 1) & (ROOM_LINE - 1);
        outL[i] = eg * el + lg * 0.5f * (o[0] - o[1] + o[2] - o[3]);
        outR[i] = eg * er + lg * 0.5f * (o[0] + o[1] - o[2] - o[3]);
    }
}

/* the eight-line network of the hall (and the shimmer): its loop gains for this block */
static const int HALL_BASE[8] = {1931, 2251, 2647, 3001, 3413, 3797, 4153, 4597};
typedef struct { int len[8]; float g[8], glo[8], ghi[8], depth, cl, ch; int bands; } fdn8_t;
static void fdn8_setup(fdn8_t *c, const rv_loop_t *k, const fxs_params_t *p, float s) {
    c->depth = 8.0f * fx_clamp(p->rev_mod, 0.0f, 2.0f);
    c->bands = !k->freeze && !k->inf && (p->dec_lo != 1.0f || p->dec_hi != 1.0f);
    c->cl = fx_lp_coef(250.0f);
    c->ch = fx_lp_coef(fx_clamp(p->hifreq_hz, 500.0f, 16000.0f));
    for (int j = 0; j < 8; j++) {
        c->len[j] = (int)(HALL_BASE[j] * s);
        c->g[j] = rv_gain(k, (float)c->len[j]);
        if (c->bands) {
            c->glo[j] = rv_gain_rt(k, (float)c->len[j], k->rt * fx_clamp(p->dec_lo, 0.1f, 5.0f));
            c->ghi[j] = rv_gain_rt(k, (float)c->len[j], k->rt * fx_clamp(p->dec_hi, 0.1f, 5.0f));
        }
    }
}
/* one sample of the network: v in; the left and right output sums out (before their 0.55 scale) */
static inline void fdn8_tick(hall_t *t, const rv_loop_t *k, const fdn8_t *c, float v, float *sl, float *sr) {
    t->ph = fx_wrap(t->ph + 0.37f / FX_SR);
    float o[8], x[8];
    for (int j = 0; j < 8; j++) {
        if (j & 1) o[j] = t->line[j][(t->w - c->len[j]) & (HALL_LINE - 1)];
        else {   /* four lines swing slowly, a quarter cycle apart: a smoother, less metallic tail */
            float ex = c->depth * (1.0f + fx_sin(fx_wrap(t->ph + 0.125f * (float)j)));
            o[j] = fx_hermite(t->line[j], HALL_LINE - 1, t->w, (float)c->len[j] - 1.0f + ex);
        }
        if (c->bands) {   /* LOW / HIGH DECAY: the bands below 250 Hz and above HIGH FREQ at decays of their own */
            float y = rv_damp(k, o[j], &t->damp[j], &t->low[j]);
            t->bl[j] += (y - t->bl[j]) * c->cl;
            t->bh[j] += (y - t->bh[j]) * c->ch;
            float lo = t->bl[j], hi = y - t->bh[j];
            x[j] = c->glo[j] * lo + c->g[j] * (y - lo - hi) + c->ghi[j] * hi;
        } else {
            x[j] = c->g[j] * rv_damp(k, o[j], &t->damp[j], &t->low[j]);
        }
    }
    rv_had8(x);
    for (int j = 0; j < 8; j++) t->line[j][t->w] = rv_lim(k, x[j] + ((j & 1) ? -0.35f : 0.35f) * v + FX_DENORM);
    t->w = (t->w + 1) & (HALL_LINE - 1);
    *sl = o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7];
    *sr = o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7];
}

static void rv_hall(rev_t *r, const fxs_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                    int n, int pre, float tone_c, float eg, float lg) {
    hall_t *t = &r->u.hall;
    const rv_loop_t k = rv_loop(p);
    const float s = fx_clamp(p->rev_size, 0.5f, 2.0f), dg = 0.7f * p->rev_diff;
    fdn8_t c;
    fdn8_setup(&c, &k, p, s);
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c), el, er, sl, sr;
        rv_early(r, pre, 1.8f * s, &el, &er);
        AP(t->ap1, 113, dg, v, v);
        AP(t->ap2, 167, dg, v, v);
        AP(t->ap3, 251, dg, v, v);
        AP(t->ap4, 347, dg, v, v);
        fdn8_tick(t, &k, &c, v, &sl, &sr);
        outL[i] = eg * el + lg * 0.55f * sl;
        outR[i] = eg * er + lg * 0.55f * sr;
    }
}

/* a pitch shifter voice: two taps half a window apart on line `buf`, each swept across the window at the ratio's rate
 * and faded by a sine window (their powers add to one); `ph` its phase 0..1 */
static inline float ps_read(const float *buf, int w, float dist) {
    float pos = (float)w - 1.0f - dist;
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    return buf[i & (PS_SIZE - 1)] * (1.0f - t) + buf[(i + 1) & (PS_SIZE - 1)] * t;
}
static inline float ps_voice(const float *buf, int w, float *ph, float ratio) {
    float p0 = *ph, p1 = fx_wrap(p0 + 0.5f);
    float y = ps_read(buf, w, 1.0f + p0 * PS_WIN) * fx_sin(0.5f * p0) + ps_read(buf, w, 1.0f + p1 * PS_WIN) * fx_sin(0.5f * p1);
    float q = p0 - (ratio - 1.0f) / PS_WIN;   /* higher: the read gains on the write */
    *ph = q < 0.0f ? q + 1.0f : q >= 1.0f ? q - 1.0f : q;
    return y;
}

static void rv_shimmer(rev_t *r, const fxs_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                       int n, int pre, float tone_c) {
    shim_t *t = &r->u.shim;
    hall_t *h = &t->tank;
    const rv_loop_t k = rv_loop(p);
    const float s = fx_clamp(p->rev_size, 0.5f, 2.0f), dg = 0.7f * p->rev_diff;
    fdn8_t c;
    fdn8_setup(&c, &k, p, s);
    const int frozen = p->hold == HOLD_PITCH || p->hold == HOLD_FREEZE, len = (int)(0.2f * FX_SR * s);
    const float ra = exp2f(fx_clamp(p->pitch_a, -2400.0f, 2400.0f) / 1200.0f);
    const float rb = exp2f(fx_clamp(p->pitch_b, -2400.0f, 2400.0f) / 1200.0f);
    const float fp = 0.95f * fx_clamp(p->pdecay, 0.0f, 1.0f), lpc = fx_lp_coef(7000.0f), hpc = fx_lp_coef(90.0f);
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c), sl, sr;
        AP(h->ap1, 113, dg, v, v);
        AP(h->ap2, 167, dg, v, v);
        AP(h->ap3, 251, dg, v, v);
        AP(h->ap4, 347, dg, v, v);
        float y = t->loop[(t->lw - len) & (SHIM_LOOP - 1)];
        if (frozen) {   /* the pitch loop goes round as it is: it holds */
            t->loop[t->lw] = y;
        } else {   /* the input and what goes round, shifted again */
            t->ps[t->pw] = v + fp * y;
            t->pw = (t->pw + 1) & (PS_SIZE - 1);
            float z = 0.70710678f * (ps_voice(t->ps, t->pw, &t->pph[0], ra) + ps_voice(t->ps, t->pw, &t->pph[1], rb));
            t->lp += (z - t->lp) * lpc;
            t->hp += (t->lp - t->hp) * hpc;
            t->loop[t->lw] = fx_sat(t->lp - t->hp) + FX_DENORM;
        }
        t->lw = (t->lw + 1) & (SHIM_LOOP - 1);
        fdn8_tick(h, &k, &c, k.freeze ? 0.0f : v + y, &sl, &sr);
        outL[i] = 0.55f * sl;
        outR[i] = 0.55f * sr;
    }
}

static void rv_blackhole(rev_t *r, const fxs_params_t *p, const float *inL, const float *inR, float *outL,
                         float *outR, int n, int pre, float tone_c) {
    static const int BASE[8] = {4001, 4813, 5683, 6397, 7211, 8009, 8807, 9601}, AP_BASE[4] = {997, 1409, 1999, 2711};
    bhole_t *t = &r->u.bh;
    rv_loop_t k = rv_loop(p);
    const float grav = fx_clamp(p->gravity, -1.0f, 1.0f), sc = 1.3f * fx_clamp(p->rev_size, 0.5f, 2.0f);
    if (grav > 0.0f) k.rt *= 1.0f - 0.5f * grav;   /* pulled down: shorter */
    const float coef = 0.6f - 0.25f * grav;        /* the diffusers' smear: 0.35 (sharp) .. 0.85 (swells in) */
    const float mod = fx_clamp(p->rev_mod, 0.0f, 2.0f), dap = 12.0f * mod, dln = 40.0f * mod;
    const float inc = fx_clamp(p->rate_hz, 0.01f, 20.0f) / FX_SR, hic = k.freeze ? 1.0f : fx_lp_coef(9000.0f);
    int len[8], alen[4];
    float g[8];
    for (int j = 0; j < 8; j++) { len[j] = (int)(BASE[j] * sc); g[j] = rv_gain(&k, (float)len[j]); }
    for (int j = 0; j < 4; j++) alen[j] = (int)(AP_BASE[j] * sc);
    /* the crossover: TPT state-variable filter, its Q from RESONANCE */
    const float rr = fx_clamp(p->reso, 0.0f, 1.0f) * fx_clamp(p->reso, 0.0f, 1.0f);   /* gentle low on the knob */
    const float q = 0.707f + 6.0f * rr, kk = 1.0f / q, sg = tanf(FX_PI * BH_XOVER_HZ / FX_SR);
    const float a1 = 1.0f / (1.0f + sg * (sg + kk)), a2 = sg * a1, a3 = sg * a2;
    const float gl = p->lvl_lo, gh = p->lvl_hi, gb = 0.5f * (gl + gh) * kk + 3.0f * rr * kk;
    for (int i = 0; i < n; i++) {
        float v = rv_input(r, 0.5f * (inL[i] + inR[i]) * k.in, pre, tone_c);
        t->ph = fx_wrap(t->ph + inc);
#define BH_AP_STEP(d, j) do { float d_ = fx_hermite((d).buf, BH_AP - 1, (d).w, (float)alen[j] - 1.0f + \
                              dap * (1.0f + fx_sin(fx_wrap(t->ph + 0.25f * (j))))); \
                              float w_ = v + coef * d_; DL_WRITE(d, w_); v = d_ - coef * w_; } while (0)
        BH_AP_STEP(t->d1, 0); BH_AP_STEP(t->d2, 1); BH_AP_STEP(t->d3, 2); BH_AP_STEP(t->d4, 3);
#undef BH_AP_STEP
        float o[8], x[8];
        for (int j = 0; j < 8; j++) {
            float ex = dln * (1.0f + fx_sin(fx_wrap(0.7f * t->ph + 0.125f * (float)j)));
            o[j] = fx_hermite(t->line[j], BH_LINE - 1, t->w, (float)len[j] - 1.0f + ex);
            t->damp[j] += (o[j] - t->damp[j]) * hic;
            x[j] = g[j] * t->damp[j];
        }
        rv_had8(x);
        for (int j = 0; j < 8; j++) t->line[j][t->w] = rv_lim(&k, x[j] + ((j & 1) ? -0.3f : 0.3f) * v + FX_DENORM);
        t->w = (t->w + 1) & (BH_LINE - 1);
        float y[2] = {0.5f * (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]),
                      0.5f * (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7])};
        if (grav > 0.0f) { y[0] += 0.5f * grav * v; y[1] -= 0.5f * grav * v; }   /* a denser, harder onset */
        for (int ch = 0; ch < 2; ch++) {   /* low + band + high = the input: the levels and the resonance shape it */
            float *s = t->sv[ch];
            float v3 = y[ch] - s[1], v1 = a1 * s[0] + a2 * v3, v2 = s[1] + a2 * s[0] + a3 * v3;
            s[0] = 2.0f * v1 - s[0] + FX_DENORM;
            s[1] = 2.0f * v2 - s[1] + FX_DENORM;
            y[ch] = gl * v2 + gh * (y[ch] - kk * v1 - v2) + gb * v1;
        }
        outL[i] = y[0];
        outR[i] = y[1];
    }
}

/* the band levels of what comes out (room: LOW / HIGH; hall: LOW / MID / HIGH): split at 250 Hz and HIGH FREQ; all at
 * 1 (0 on the knobs) leaves it as it is */
static void rv_bands(rev_t *r, const fxs_params_t *p, float *l, float *rr, int n, int mid) {
    const float gl = p->lvl_lo, gh = p->lvl_hi, gm = mid ? p->lvl_mid : 1.0f;
    if (gl == 1.0f && gh == 1.0f && gm == 1.0f) return;
    const float cl = fx_lp_coef(250.0f), chc = fx_lp_coef(fx_clamp(p->hifreq_hz, 500.0f, 16000.0f));
    float *io[2] = {l, rr};
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < n; i++) {
            float x = io[c][i];
            r->eq[c][0] += (x - r->eq[c][0]) * cl;
            r->eq[c][1] += (x - r->eq[c][1]) * chc;
            float lo = r->eq[c][0], hi = x - r->eq[c][1];
            io[c][i] = gl * lo + gm * (x - lo - hi) + gh * hi;
        }
}

/* in: what the slot's send (and effect 1, in series) feeds it; out: the reverb */
static void rv_process(rev_t *r, const fxs_params_t *p, const float *inL, const float *inR, float *outL, float *outR,
                       int n) {
    if (p->type == REV_SPACE) {
        float replace = p->hold == HOLD_FREEZE || p->hold == HOLD_INFINITE ? 0.0f : 1.0f - fx_clamp(p->rev_decay_k, 0.0f, 1.0f);
        aw_galactic(&r->u.space, replace, fx_clamp(p->rev_tone, 0.0f, 1.0f), fx_clamp(0.5f * p->rev_mod, 0.0f, 1.0f),
                    fx_clamp(p->rev_size_k, 0.0f, 1.0f), 1.0f, inL, inR, outL, outR, n);
        return;
    }
    int pre = (int)fx_clamp(p->rev_pre_ms * 0.001f * FX_SR, 0.0f, (float)RV_PRE_MAX);
    float tone_c = fx_tone_coef(p->rev_tone);
    float b = fx_clamp(p->rev_early, 0.0f, 1.0f);
    if (p->type == REV_ROOM) b = roundf(b * 20.0f) * 0.05f;   /* the manual's 21 detents: E10+L0 .. E10+L10 .. E0+L10 */
    float eg = fminf(1.0f, 2.0f * (1.0f - b)), lg = fminf(1.0f, 2.0f * b);
    switch (p->type) {
    case REV_ROOM: rv_room(r, p, inL, inR, outL, outR, n, pre, tone_c, eg, lg); rv_bands(r, p, outL, outR, n, 0); break;
    case REV_HALL: rv_hall(r, p, inL, inR, outL, outR, n, pre, tone_c, eg, lg); rv_bands(r, p, outL, outR, n, 1); break;
    case REV_SHIMMER: rv_shimmer(r, p, inL, inR, outL, outR, n, pre, tone_c); break;
    case REV_BLACKHOLE: rv_blackhole(r, p, inL, inR, outL, outR, n, pre, tone_c); break;
    default: rv_plate(r, p, inL, inR, outL, outR, n, pre, tone_c); break;
    }
}
