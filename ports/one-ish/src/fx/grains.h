/* ONE-ish GRAINS (2026-10-30): an original granular effect (textbook granular synthesis, no third-party code), a synth
 * effect type and a master slot type. It differs from GRANULAR (Mutable Instruments Clouds, mi_effects.h) by design:
 * a long recording (5.9 s), FREEZE that holds all of it, reverse grains, pitch quantised to semitones, octaves or
 * octaves and fifths, random pitch, and its own position spray.
 *   The slot's input (mono: the two sides' average) is written into a circular buffer of GR_SIZE samples (5.94 s at
 * 44.1 kHz). Grains (a fixed pool of GR_VOICES) start DENSITY times a second; each reads GR_SIZE's audio from POSITION
 * back (plus up to SPRAY at random), for SIZE, at PITCH (plus up to PITCH RANDOM at random; then quantised), backwards
 * with the REVERSE probability, under an ENVELOPE (raised-cosine edges: a near-square Tukey at 0, Hann at 50%, a
 * percussive fast-attack long-decay shape at 100%; never less than 1.5 ms per edge, so no clicks), placed at random up
 * to STEREO either side. Reads are 4-point Hermite interpolated.
 *   FREEZE stops the writing: the grains play what the buffer holds for as long as it is on. FEEDBACK adds the grains
 * (one block later, soft-limited to +-1) back into what is recorded, at most 90%.
 *   Every grain's reads stay inside the recorded audio whatever FREEZE does while it plays (its start distance is
 * clamped for the worst case of both), so a grain never crosses the write point.
 *   Output: the grains' sum times 1 / sqrt(1 + overlap / 2) (the expected overlap: density x size), ramped per block.
 *   Lives in the slot's union (synth_fx.h, over the delay lines' memory: no memory of its own), cleared by fx.h when
 * the slot changes type. Nothing here allocates; no locks, no files. */
#pragma once
#include "common.h"

#define GR_SIZE 262144                 /* samples (power of two): 5.94 s at 44.1 kHz, the delay lines' memory */
#define GR_MASK (GR_SIZE - 1)
#define GR_VOICES 64
#define GR_BLOCK 128                   /* samples per call at most (fx.h FX_BLOCK) */
#define GR_EDGE 64                     /* the shortest envelope edge, samples (1.5 ms) */
#define GR_FB_MAX 0.9f

enum { GQ_FREE, GQ_SEMI, GQ_OCT, GQ_OCT5 };   /* "modfx_gquant" / "dly_gquant" / "rev_gquant" options */

typedef struct {
    int len;            /* 0: free */
    int age;            /* samples played */
    int base;           /* buffer index it starts at */
    float rate;         /* read speed, negative: reverse */
    float ia, ir;       /* 1 / attack and release lengths in samples */
    float gl, gr;       /* its place in the stereo field */
} grain_t;

typedef struct {
    float buf[GR_SIZE];
    grain_t g[GR_VOICES];
    float fbk[GR_BLOCK];  /* the last block's output (mono), fed back */
    float gain;           /* the output gain, ramped */
    float acc;            /* the grain clock: a new grain at 1 */
    int w;                /* write position */
    uint32_t rng;
} gr_t;

/* the knobs (0..1) in their units */
static inline float gr_size_ms(float k) { return 10.0f + 490.0f * powf(fx_clamp(k, 0, 1), 1.5f); }   /* 10..500 ms */
static inline float gr_rate_hz(float k) { k = fx_clamp(k, 0, 1); return 1.0f + 149.0f * k * k; }   /* 1..150 a second */
static inline float gr_pos_s(float k) { k = fx_clamp(k, 0, 1); return 5.0f * k * k * k; }   /* 0..5 s back (50%: 0.6 s) */
static inline float gr_spray_s(float k) { k = fx_clamp(k, 0, 1); return 2.0f * k * k; }   /* up to 2 s more */
static inline float gr_prand_st(float k) { k = fx_clamp(k, 0, 1); return 12.0f * k * k; }   /* +-0..12 semitones */

static void gr_init(gr_t *g) {
    g->rng = 0x9E3779B9u;
    g->gain = -1.0f;   /* (set at the first block) */
}

static inline float gr_u(gr_t *g) { return 0.5f + 0.5f * fx_white(&g->rng); }   /* 0..1 */

/* semitones quantised as the option says */
static inline float gr_quant(float st, int q) {
    switch (q) {
    case GQ_SEMI: return roundf(st);
    case GQ_OCT: return 12.0f * roundf(st / 12.0f);
    case GQ_OCT5: {   /* the nearest octave or octave + fifth */
        float o = 12.0f * floorf(st / 12.0f), r = st - o;
        return o + (r < 3.5f ? 0.0f : r < 9.5f ? 7.0f : 12.0f);
    }
    default: return st;
    }
}

/* sin^2(pi/2 u), u 0..1: a raised-cosine edge */
static inline float gr_edge(float u) { float s = fx_sin(0.25f * u); return s * s; }

/* a grain starting `at` samples into this block; head: the write point at that sample */
static void gr_spawn(gr_t *g, const fxs_params_t *p, int head, int at) {
    grain_t *v = NULL;
    for (int k = 0; k < GR_VOICES; k++)
        if (!g->g[k].len) { v = &g->g[k]; break; }
    if (!v) return;   /* the pool is full: this one is skipped */
    int len = (int)(gr_size_ms(p->size) * 0.001f * FX_SR);
    float st = fx_clamp(p->pitch_st, -48.0f, 48.0f) + gr_prand_st(p->prand) * fx_white(&g->rng);
    float r = fx_clamp(exp2f(gr_quant(st, p->gquant) / 12.0f), 0.125f, 4.0f);
    if (gr_u(g) < p->reverse) r = -r;
    /* the start distance behind the write point, clamped so every read stays in recorded audio, frozen or not */
    float lo = 4.0f + fmaxf(0.0f, len * r), hi = GR_SIZE - 8.0f - GR_BLOCK - fmaxf(0.0f, len * (1.0f - r));
    float d = (gr_pos_s(p->position) + gr_spray_s(p->spray) * gr_u(g)) * FX_SR;
    d = fx_clamp(d, lo, hi);
    v->base = (head - (int)d) & GR_MASK;
    v->rate = r;
    v->len = len;
    v->age = -at;   /* starts `at` samples into this block */
    float k = fx_clamp(p->texture, 0, 1), a = k <= 0.5f ? 0.02f + 0.96f * k : 0.5f - 0.96f * (k - 0.5f);
    float rl = k <= 0.5f ? a : 1.0f - a;
    v->ia = 1.0f / fmaxf(a * len, GR_EDGE);
    v->ir = 1.0f / fmaxf(rl * len, GR_EDGE);
    float pan = fx_clamp(p->spread, 0, 1) * fx_white(&g->rng);   /* -1..1, equal power */
    v->gl = sqrtf(0.5f * (1.0f - pan));
    v->gr = sqrtf(0.5f * (1.0f + pan));
}

/* inL, inR: the slot's input; wl, wr: the grains (n <= GR_BLOCK) */
static void gr_process(gr_t *g, const fxs_params_t *p, const float *inL, const float *inR, float *wl, float *wr, int n) {
    if (n > GR_BLOCK) n = GR_BLOCK;
    const float fb = fx_clamp(p->fb, -GR_FB_MAX, GR_FB_MAX);
    /* the block's input into the buffer; frozen: nothing written and the write point stays (writing goes on from
     * there when FREEZE is let go, so the recording joins up) */
    const int w0 = g->w;
    if (!p->freeze)
        for (int i = 0; i < n; i++) {
            g->buf[g->w] = 0.5f * (inL[i] + inR[i]) + fb * fx_sat(g->fbk[i]) + FX_DENORM;
            g->w = (g->w + 1) & GR_MASK;
        }
    /* new grains, on their sample */
    const float inc = gr_rate_hz(p->density) / FX_SR;
    for (int i = 0; i < n; i++) {
        g->acc += inc;
        if (g->acc >= 1.0f) { g->acc -= 1.0f; gr_spawn(g, p, w0 + (p->freeze ? 0 : i), i); }
    }
    memset(wl, 0, sizeof(float) * n);
    memset(wr, 0, sizeof(float) * n);
    for (int k = 0; k < GR_VOICES; k++) {
        grain_t *v = &g->g[k];
        if (!v->len) continue;
        int i = v->age < 0 ? -v->age : 0, a = v->age < 0 ? 0 : v->age;
        for (; i < n && a < v->len; i++, a++) {
            float pos = v->rate * (float)a, fl = floorf(pos), t = pos - fl;
            int j = v->base + (int)fl;
            float xm1 = g->buf[(j - 1) & GR_MASK], x0 = g->buf[j & GR_MASK], x1 = g->buf[(j + 1) & GR_MASK],
                  x2 = g->buf[(j + 2) & GR_MASK];
            float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2,
                  c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            float y = ((c3 * t + c2) * t + c1) * t + x0;
            float ea = (float)a * v->ia, er = (float)(v->len - a) * v->ir;
            float e = ea < 1.0f ? gr_edge(ea) : er < 1.0f ? gr_edge(er) : 1.0f;
            y *= e;
            wl[i] += y * v->gl;
            wr[i] += y * v->gr;
        }
        if (a >= v->len) v->len = 0;
        else v->age = a;
    }
    /* level: by the expected overlap, ramped across the block */
    float over = gr_rate_hz(p->density) * gr_size_ms(p->size) * 0.001f;
    float to = 1.0f / sqrtf(1.0f + 0.5f * over), from = g->gain < 0.0f ? to : g->gain, dg = (to - from) / (float)n;
    for (int i = 0; i < n; i++, from += dg) {
        wl[i] *= from;
        wr[i] *= from;
        g->fbk[i] = 0.5f * (wl[i] + wr[i]);
    }
    for (int i = n; i < GR_BLOCK; i++) g->fbk[i] = 0.0f;
    g->gain = to;
}
