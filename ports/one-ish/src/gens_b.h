/* ONE-ish's RAIN AND RIPPLES and MARKOV MELODY note generators (2026-10-30), in the GENERATIVE section beside the
 * bouncing balls and particles (src/physgen.h). This file is the pure part of each: no engine, no params.
 *
 * RAIN AND RIPPLES: drops fall onto a pond (seen from above, 0..1 each way) at a RATE; a drop lands RN_FALL seconds
 * after it appears, and its landing is a hit (where, how big) and starts a ripple ring there. A ring grows at the
 * ripple SPEED and fades over its LIFE; where two rings first touch, or a ring first reaches one of the STONES, is a
 * softer hit (an echo). Pools of 16 drops and 32 rings (a new ring over a full pool takes the oldest one's place).
 *
 * MARKOV MELODY: a table of what comes next, learned from notes. A note is a scale degree (D: its place in the scale,
 * counting up from the root of the lowest octave; in a 7-note scale D 7 is the root an octave up). What is learned:
 * after a degree of the scale (0 .. size-1, the octave left out) the step to the next note (-7 .. +7 degrees), and
 * (second order) the same after the last two degrees; and a small table of rhythm: after a gap of so many steps, the
 * next gap (1, 2, 3, 4, 6 or 8 steps). Counts in fixed arrays (uint16, halved when one gets full). It starts from a
 * gentle built-in table (mostly steps of one or two degrees, leaning home to the root; gaps of one or two steps), so it
 * plays something before it has heard anything. Choosing: CREATIVITY 0 takes the most likely step every time; above
 * 0 it draws at random, flatter the higher (a temperature), and over 50 it also wanders off the table a little (more
 * towards 100).
 *
 * Fixed-size arrays, a seeded random sequence of their own (seq.h's xorshift), no allocation, no locks, no files: safe
 * in the audio thread, and the same seed always gives the same notes. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "seq.h"

/* ---- RAIN AND RIPPLES ---- */
#define RN_DROPS 16        /* drops falling at most */
#define RN_RIPS 32         /* rings at most */
#define RN_STONES 6        /* stones at most */
#define RN_HITS 48         /* the hits one step reports at most (more are dropped) */
#define RN_FALL 0.3f       /* seconds from a drop appearing to its landing (the window shows it falling) */
#define RN_QUIET 0.06f     /* an echo softer than this (of a full drop's strength) plays nothing */

enum { RN_DROP, RN_CROSS, RN_STONE };   /* what a hit was */

typedef struct { float x, y, size, t; uint8_t live; } rn_drop_t;   /* t: seconds still to fall */
typedef struct { float x, y, r, e0, age, life; uint32_t met; uint8_t stones, live, lit; } rn_rip_t;
typedef struct { uint8_t kind, who; float x, y, amp, frac; } rn_hit_t;   /* amp 0..1; frac: when in the step */

typedef struct {
    float rate;      /* drops a second (0: none) */
    float spread;    /* 0 .. 1: drop sizes from 1 down to 1 - spread */
    float speed;     /* how fast a ring grows, pond widths a second */
    float life;      /* how long a full drop's ring lasts, seconds */
    float echo;      /* 0 .. 1: the chance a crossing or a stone plays, and how loud (0: no echoes) */
    int stones;      /* the stones in the pond, 0 .. RN_STONES */
} rn_cfg_t;

typedef struct {
    rn_drop_t d[RN_DROPS];
    rn_rip_t r[RN_RIPS];
    float sx[RN_STONES], sy[RN_STONES];   /* where the stones are (fixed by the seed) */
    float next;      /* seconds to the next drop */
    float energy;    /* the rings' strength together, 0 .. 1 (a modulation source) */
    uint32_t rng;    /* never 0 (xorshift) */
} rn_world_t;

static inline float rn_rand(rn_world_t *w) { return seq_rand01(&w->rng); }

/* everything from the start: the same seed, the same drops and stones (seed 0 is made 1) */
static inline void rn_restart(rn_world_t *w, uint32_t seed) {
    memset(w, 0, sizeof *w);
    w->rng = seed ? seed : 1u;
    for (int k = 0; k < RN_STONES; k++) {   /* spread over the pond, away from its edges */
        w->sx[k] = 0.15f + 0.7f * rn_rand(w);
        w->sy[k] = 0.15f + 0.7f * rn_rand(w);
    }
    w->next = 0.1f;   /* the first drop soon */
}

static inline int rn_add(rn_hit_t *h, int n, int kind, int who, float x, float y, float amp, float frac) {
    if (n >= RN_HITS) return n;
    h[n] = (rn_hit_t){(uint8_t)kind, (uint8_t)who, x, y, amp, frac < 0.0f ? 0.0f : frac > 0.999f ? 0.999f : frac};
    return n + 1;
}

static void rn_spawn(rn_world_t *w, const rn_cfg_t *c) {
    for (int i = 0; i < RN_DROPS; i++) {
        rn_drop_t *d = &w->d[i];
        if (d->live) continue;
        float x = 0.06f + 0.88f * rn_rand(w), y = 0.06f + 0.88f * rn_rand(w);
        *d = (rn_drop_t){x, y, 1.0f - c->spread * rn_rand(w), RN_FALL, 1};
        return;
    }   /* (all falling: none this time) */
}

static void rn_ring(rn_world_t *w, const rn_cfg_t *c, float x, float y, float size) {
    int j = 0;
    for (int i = 0; i < RN_RIPS; i++) {   /* a free place, else the oldest ring's */
        if (!w->r[i].live) { j = i; break; }
        if (w->r[i].age > w->r[j].age) j = i;
    }
    for (int i = 0; i < RN_RIPS; i++) w->r[i].met &= ~(1u << j);   /* the others have not met this one yet */
    w->r[j] = (rn_rip_t){x, y, 0.0f, size, 0.0f, c->life * (0.6f + 0.4f * size), 0, 0, 1, 0};
}

static inline float rn_left(const rn_rip_t *r) { return r->e0 * (1.0f - r->age / r->life); }   /* its strength now */

/* the pond moves on by dt seconds; returns the hits (into h, RN_HITS at most): landings first, then echoes */
static int rn_step(rn_world_t *w, const rn_cfg_t *c, float dt, rn_hit_t *h) {
    int n = 0;
    if (c->rate > 0.0f) {
        w->next -= dt;
        for (int guard = 0; w->next <= 0.0f && guard < 4; guard++) {
            rn_spawn(w, c);
            w->next += (0.25f + 1.5f * rn_rand(w)) / c->rate;   /* about RATE a second, not evenly */
        }
        if (w->next <= 0.0f) w->next = 0.0f;
    }
    for (int i = 0; i < RN_DROPS; i++) {
        rn_drop_t *d = &w->d[i];
        if (!d->live) continue;
        float t0 = d->t;
        d->t -= dt;
        if (d->t > 0.0f) continue;
        d->live = 0;
        n = rn_add(h, n, RN_DROP, i, d->x, d->y, d->size, t0 / dt);
        rn_ring(w, c, d->x, d->y, d->size);
    }
    float energy = 0.0f;
    for (int i = 0; i < RN_RIPS; i++) {
        rn_rip_t *r = &w->r[i];
        if (!r->live) continue;
        r->age += dt;
        if (r->age >= r->life) { r->live = 0; continue; }
        r->r += c->speed * dt;
        energy += rn_left(r);
        for (int k = 0; k < c->stones && k < RN_STONES; k++) {   /* a ring reaching a stone */
            if (r->stones >> k & 1) continue;
            float dx = w->sx[k] - r->x, dy = w->sy[k] - r->y;
            if (r->r * r->r < dx * dx + dy * dy) continue;
            r->stones |= (uint8_t)(1u << k);
            float amp = rn_left(r) * c->echo;
            if (c->echo > 0.0f && amp >= RN_QUIET && rn_rand(w) < c->echo)
                n = rn_add(h, n, RN_STONE, k, w->sx[k], w->sy[k], amp, 0.5f);
        }
    }
    w->energy = energy / 3.0f > 1.0f ? 1.0f : energy / 3.0f;
    for (int i = 0; i < RN_RIPS; i++) {   /* two rings first touching: every pair once, 496 at most */
        rn_rip_t *a = &w->r[i];
        if (!a->live) continue;
        for (int j = i + 1; j < RN_RIPS; j++) {
            rn_rip_t *b = &w->r[j];
            if (!b->live || (a->met >> j & 1)) continue;
            float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy, sum = a->r + b->r;
            if (sum * sum < d2) continue;
            a->met |= 1u << j;
            b->met |= 1u << i;
            float amp = 0.5f * (rn_left(a) + rn_left(b)) * c->echo, k = sum > 1e-6f ? a->r / sum : 0.5f;
            if (c->echo > 0.0f && amp >= RN_QUIET && rn_rand(w) < c->echo) {
                n = rn_add(h, n, RN_CROSS, i, a->x + dx * k, a->y + dy * k, amp, 0.5f);
                a->lit = b->lit = 1;
            }
        }
    }
    return n;
}

/* ---- MARKOV MELODY ---- */
#define MK_PC 12           /* degrees of a scale, the octave left out (12: chromatic) */
#define MK_SPAN 7          /* the steps learned: -7 .. +7 degrees (bigger leaps count as 7) */
#define MK_IV (2 * MK_SPAN + 1)
#define MK_RC 6            /* the rhythm's gaps: 1, 2, 3, 4, 6, 8 steps */
#define MK_W 8             /* a learned note's weight (a built-in step weighs 1 .. 6) */
#define MK_FULL 60000      /* a count past this halves its row */
static const uint8_t MK_GAP[MK_RC] = {1, 2, 3, 4, 6, 8};

typedef struct {
    uint16_t t1[MK_PC][MK_IV];          /* after degree a: the step to the next note */
    uint16_t t2[MK_PC][MK_PC][MK_IV];   /* after degrees b then a: the step */
    uint16_t r1[MK_RC][MK_RC];          /* after a gap: the next gap */
    int learned;                        /* notes learned (0: the built-in table only) */
} mk_table_t;

static inline int mk_mod(int d, int n) { return ((d % n) + n) % n; }

/* the gentle built-in table for a scale of `size` degrees: small steps likelier, a lean home to the root; gaps of one
 * or two steps, a longer one now and then */
static inline void mk_builtin(mk_table_t *t, int size) {
    memset(t, 0, sizeof *t);
    static const uint8_t W[MK_SPAN + 1] = {1, 6, 3, 2, 1, 1, 0, 1};   /* by the size of the step: 0 .. 7 */
    for (int a = 0; a < MK_PC; a++) {
        for (int iv = -MK_SPAN; iv <= MK_SPAN; iv++) t->t1[a][iv + MK_SPAN] = W[iv < 0 ? -iv : iv];
        int pc = mk_mod(a, size), home = pc <= size / 2 ? -pc : size - pc;   /* the nearest root */
        if (pc && home >= -MK_SPAN && home <= MK_SPAN) t->t1[a][home + MK_SPAN] += 3;
    }
    static const uint8_t R[MK_RC] = {5, 3, 1, 1, 0, 0};
    for (int g = 0; g < MK_RC; g++)
        for (int h = 0; h < MK_RC; h++) t->r1[g][h] = (uint16_t)(R[h] + (g == h && R[h] ? 2 : 0));
}

static inline void mk_bump(uint16_t *row, int n, int i) {
    if (row[i] > MK_FULL)
        for (int k = 0; k < n; k++) row[k] = (uint16_t)(row[k] >> 1);
    row[i] = (uint16_t)(row[i] + MK_W);
}

/* one step learned: from degree d1 (after d2, or -1) to degree d (scale of `size` degrees) */
static inline void mk_learn_step(mk_table_t *t, int size, int d2, int d1, int d) {
    int iv = d - d1, a = mk_mod(d1, size);
    iv = iv < -MK_SPAN ? -MK_SPAN : iv > MK_SPAN ? MK_SPAN : iv;
    mk_bump(t->t1[a], MK_IV, iv + MK_SPAN);
    if (d2 >= 0) mk_bump(t->t2[mk_mod(d2, size)][a], MK_IV, iv + MK_SPAN);   /* (degrees are never negative) */
    t->learned++;
}
static inline void mk_learn_gap(mk_table_t *t, int g1, int g) {
    if (g1 >= 0 && g1 < MK_RC && g >= 0 && g < MK_RC) mk_bump(t->r1[g1], MK_RC, g);
}

/* a gap in steps (any length) as the nearest of MK_GAP, by ratio; -1 past 12 steps (a pause, not a rhythm) */
static inline int mk_gap_class(float steps) {
    if (steps > 12.0f) return -1;
    int best = 0;
    float bd = 1e9f;
    for (int g = 0; g < MK_RC; g++) {
        float d = fabsf(logf(fmaxf(steps, 0.25f) / MK_GAP[g]));
        if (d < bd) { bd = d; best = g; }
    }
    return best;
}

/* pick from n weights with CREATIVITY c (0 .. 1): c 0 the largest (the first of equals), above it a draw with the
 * weights sharpened or flattened (temperature 0.1 .. 2), and over 50 (more towards 100) a little of every choice in
 * `wander` (a mask of the indices it may wander to). `out` gets each choice's share (0 .. 1, for the window) when not NULL */
static inline int mk_pick(const uint16_t *w, int n, float c, uint32_t wander, uint32_t *rng, float *out) {
    float p[MK_IV > MK_RC ? MK_IV : MK_RC], max = 0.0f, sum = 0.0f;
    int best = -1;
    for (int i = 0; i < n; i++) if (w[i] > max) { max = w[i]; best = i; }
    if (best < 0) {   /* an empty row: the middle */
        if (out) for (int i = 0; i < n; i++) out[i] = i == n / 2;
        return n / 2;
    }
    float inv = 1.0f / (0.1f + 1.9f * c), floor_ = c > 0.5f ? 0.6f * (c - 0.5f) * (c - 0.5f) : 0.0f;
    for (int i = 0; i < n; i++) {
        p[i] = w[i] > 0 ? powf(w[i] / max, inv) : 0.0f;
        if (wander >> i & 1) p[i] += floor_;
        sum += p[i];
    }
    if (out) for (int i = 0; i < n; i++) out[i] = p[i] / sum;
    if (c < 0.005f) return best;
    float r = seq_rand01(rng) * sum;
    for (int i = 0; i < n; i++) {
        r -= p[i];
        if (r < 0.0f && p[i] > 0.0f) return i;
    }
    return best;
}

/* ---- the engine's side (src/gens_b_run.h): one per Synth, in its one_t; runtime state only, never saved ---- */
#define GB_STEP 64         /* the pond moves (and the melody looks for its next grid line) every 64 samples */
#define GB_Q 32            /* notes waiting for their sample (or the tempo grid) */
#define GB_VIEW_EVERY 4    /* the windows' views are written every fourth step (about 170 times a second) */
#define GB_FLASH 80        /* steps a ring or a stone stays lit after it played (about 0.12 s) */
#define MK_HIST 24         /* the melody's last notes, for the window */
enum { GBS_DROP, GBS_RIPPLE, GBS_MK_STEP, GBS_MK_HEIGHT, GBS_COUNT };   /* its modulation sources (0 .. 1) */

typedef struct {
    /* SYNC: the beat at sample t0, beats per sample; the sample the host's last song position was given at */
    double b0, bps0;
    int64_t t0, sp_at;
    int follow;
    int64_t t_next;          /* the sample of the next step */
    int ticks;
    struct { int64_t at; int32_t len; uint8_t note, vel; } q[GB_Q];   /* notes waiting for their sample */
    int nq;
    float src[GBS_COUNT];    /* the modulation sources, updated each step */
    /* RAIN AND RIPPLES */
    rn_world_t rw;
    rn_cfg_t rc;             /* the settings as last applied */
    int rn_on, rn_held;      /* on (with the Synth on); HELD KEYS */
    float rn_tokens;         /* MAX NOTES/S: notes it may still play */
    uint8_t rflash[RN_RIPS], sflash[RN_STONES];
    /* MARKOV MELODY */
    mk_table_t mt;
    int mk_init, mk_on, mk_held;
    float mk_forget;         /* FORGET as last seen: each press (a change) clears what it learned */
    uint32_t mk_rng;
    int64_t mk_line;         /* the last grid line it stepped on */
    int mk_wait, mk_gap;     /* steps to wait before its next note; the gap it chose last (MK_GAP index) */
    int g1, g2;              /* its last two degrees (-1: none) */
    int lo, hi;              /* the degrees it plays between */
    float mk_tokens;
    int l1, l2, l_gap;       /* learning: the last two degrees heard (-1: none), the last gap heard */
    int64_t l_t;             /* ... and when */
    float vavg, centre;      /* the velocity and the height (a degree) of what it heard, smoothed */
    float bars[MK_IV];       /* the last choice's shares (the window's bars) */
    int choice;
    struct { int64_t t; int16_t d; } hist[MK_HIST];   /* its last notes (the newest at hist_head - 1) */
    int hist_head, hist_n;
    /* the windows' views, written by the audio thread without a lock: vseq odd while they are written (a seqlock) */
    uint32_t vseq;
    uint8_t vnd, vnr, vns, vmn, vmorder;
    int16_t vd[RN_DROPS][3], vr[RN_RIPS][5], vs[RN_STONES][3];   /* drops x,y,fall; rings x,y,r,e,lit; stones x,y,lit */
    int16_t vmh[MK_HIST], vmspan, vmbar[MK_IV], vmchoice;   /* the melody: heights 0 .. span, the bars 0 .. 1000 */
    int32_t vmage[MK_HIST], vmlearned;   /* each note's age (ms), the notes learned */
    float tf;                /* (2026-10-30) FOLLOW TEMPO: the pond's speed factor less 1, gliding */
} gb_run_t;
