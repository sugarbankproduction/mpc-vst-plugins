/* ONE-ish's GAME OF LIFE, GRAVITY ORBITS and PENDULUMS note generators (2026-10-30), in the GENERATIVE section beside
 * the bouncing balls and particles (src/physgen.h). This file is the three worlds only: the cells and how they evolve,
 * the planets and their spokes, the pendulums and their phases, and what each step hit. Which note and velocity a hit
 * plays, the tempo grid, the note rate cap and the window's views are engine.c's (src/gens_a_run.h).
 *   LIFE: a 16 x 8 grid of cells (each row a 16-bit mask, bit x: column x alive), evolved by Conway's rule (born with 3
 *   neighbours, kept with 2 or 3), HighLife's (born with 3 or 6) or Seeds' (born with 2, never kept); the edges wrap
 *   round or are dead. Each generation remembers the last LF_HIST grids (a hash of each) so a grid that comes back
 *   (a still life, a blinker, a glider loop on a wrapped grid) is known to be looping.
 *   ORBITS: up to 6 planets round a sun at the centre (simple 2-D gravity, the sun's pull 1 in its units: a circular
 *   orbit of radius r goes round in 2 pi sqrt(r^3)); each launched at its far point with the speed that makes an orbit
 *   of the chosen eccentricity; PULL adds the planets' pull on each other (0: each orbit closes exactly). Spokes from
 *   the centre (the first straight up, the rest evenly round): a planet crossing one is a hit. Several small steps per
 *   step (the close passes stay smooth), a planet thrown out or falling in is launched again.
 *   PENDULUMS: worked out, not simulated: pendulum i swings SWINGS + i x SPREAD whole times in one CYCLE (so all of
 *   them line up again at its end: the pendulum wave), its phase from the beat; a hit is a pass through the centre or a
 *   turn at an end, found between two beat positions.
 * Fixed-size arrays, seeded random numbers of their own (seq.h's xorshift), no allocation, no locks, no files: safe in
 * the audio thread, and the same seed always gives the same hits. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "seq.h"

/* ---- GAME OF LIFE ---- */
#define LF_COLS 16
#define LF_ROWS 8
#define LF_HIST 16         /* the grids remembered, to know a loop */
#define LF_LOOP_GENS 8     /* a looping grid is seeded again after this many more generations */
enum { LFR_LIFE, LFR_HIGHLIFE, LFR_SEEDS };          /* "life_rule" */
enum { LFM_BIRTHS, LFM_COLUMN, LFM_ROW };            /* "life_mode" */
static const uint16_t LF_BIRTH[3] = {1u << 3, 1u << 3 | 1u << 6, 1u << 2};   /* bit n: born with n neighbours */
static const uint16_t LF_KEEP[3] = {1u << 2 | 1u << 3, 1u << 2 | 1u << 3, 0};  /* bit n: alive stays alive */

typedef struct {
    uint16_t c[LF_ROWS];       /* alive (row 0 at the bottom) */
    uint16_t born[LF_ROWS];    /* born in the last generation */
    uint64_t hist[LF_HIST];
    int nhist, hpos, loops;    /* loops: generations since the grid first came back (0: not looping) */
    uint32_t rng;
} lf_world_t;

static inline int lf_pop(const lf_world_t *w) {
    int n = 0;
    for (int y = 0; y < LF_ROWS; y++) n += __builtin_popcount(w->c[y]);
    return n;
}
static inline uint64_t lf_hash(const uint16_t *c) {
    uint64_t a = 0, b = 0;
    for (int y = 0; y < 4; y++) { a = a << 16 | c[y]; b = b << 16 | c[y + 4]; }
    return a ^ (b * 0x9e3779b97f4a7c15ull) ^ (b >> 29);
}
/* new random cells (each alive with chance `density`), the history forgotten; the random numbers carry on */
static inline void lf_fill(lf_world_t *w, float density) {
    for (int y = 0; y < LF_ROWS; y++) {
        w->c[y] = w->born[y] = 0;
        for (int x = 0; x < LF_COLS; x++)
            if (seq_rand01(&w->rng) < density) w->c[y] |= (uint16_t)(1u << x);
    }
    w->nhist = w->hpos = w->loops = 0;
}
static inline void lf_seed(lf_world_t *w, uint32_t seed, float density) {
    memset(w, 0, sizeof *w);
    w->rng = seed ? seed : 1u;
    lf_fill(w, density);
}

/* one generation */
static inline void lf_step(lf_world_t *w, int rule, int wrap) {
    uint16_t nc[LF_ROWS];
    uint16_t birth = LF_BIRTH[rule < 0 || rule > 2 ? 0 : rule], keep = LF_KEEP[rule < 0 || rule > 2 ? 0 : rule];
    for (int y = 0; y < LF_ROWS; y++) {
        nc[y] = w->born[y] = 0;
        for (int x = 0; x < LF_COLS; x++) {
            int n = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= LF_ROWS) {
                    if (!wrap) continue;
                    yy = (yy + LF_ROWS) % LF_ROWS;
                }
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int xx = x + dx;
                    if (xx < 0 || xx >= LF_COLS) {
                        if (!wrap) continue;
                        xx = (xx + LF_COLS) % LF_COLS;
                    }
                    n += w->c[yy] >> xx & 1;
                }
            }
            int alive = w->c[y] >> x & 1, next = alive ? keep >> n & 1 : birth >> n & 1;
            nc[y] |= (uint16_t)(next << x);
            if (next && !alive) w->born[y] |= (uint16_t)(1u << x);
        }
    }
    memcpy(w->c, nc, sizeof nc);
    uint64_t h = lf_hash(nc);
    int seen = 0;
    for (int i = 0; i < w->nhist; i++) seen |= w->hist[i] == h;
    w->loops = seen ? w->loops + 1 : 0;
    w->hist[w->hpos] = h;
    w->hpos = (w->hpos + 1) % LF_HIST;
    if (w->nhist < LF_HIST) w->nhist++;
}

/* ---- GRAVITY ORBITS ---- */
#define OB_PLANETS 6
#define OB_SPOKES 8
#define OB_SUB 4           /* small steps per step */
#define OB_EPS2 0.0004f    /* the pull's softening (0.02 squared): no infinite pull at a close pass */
#define OB_OUT 1.6f        /* a planet further out than this ... */
#define OB_IN 0.02f        /* ... or closer in than this is launched again */
#define OB_COOL 0.03f      /* a planet that just played waits this long (s, its own time) before it plays again */
#define OB_HITS 24

typedef struct { float x, y, vx, vy, cool; int sector; } ob_planet_t;
typedef struct { uint8_t planet, spoke; float speed, frac; } ob_hit_t;
typedef struct {
    int n, spokes;       /* planets on (1-6), spokes (1-8) */
    float pull;          /* each planet's pull on the others, as a share of the sun's */
    float ecc, spread;   /* eccentricity 0 .. 0.8; how far apart the orbits are, 0 .. 1 */
} ob_cfg_t;
typedef struct { ob_planet_t p[OB_PLANETS]; uint32_t rng; } ob_world_t;

static inline float ob_wrap(float a) {   /* 0 .. 2 pi */
    a = fmodf(a, 6.2831853f);
    return a < 0.0f ? a + 6.2831853f : a;
}
/* the angle round from straight up, counter-clockwise, 0 .. 2 pi */
static inline float ob_angle(float x, float y) { return ob_wrap(atan2f(y, x) - 1.5707963f); }
static inline int ob_sector(float x, float y, int spokes) {
    int s = (int)(ob_angle(x, y) * spokes / 6.2831853f);
    return s >= spokes ? spokes - 1 : s;
}

/* planet i at its far point (its own radius by SPREAD, a random angle), counter-clockwise at the speed of its orbit */
static inline void ob_launch(ob_world_t *w, const ob_cfg_t *c, int i) {
    ob_planet_t *p = &w->p[i];
    float r = 0.55f + ((float)i / (OB_PLANETS - 1) - 0.5f) * 0.7f * c->spread + 0.03f * (seq_rand01(&w->rng) - 0.5f);
    float a = seq_rand01(&w->rng) * 6.2831853f, v = sqrtf((1.0f - c->ecc) / r);
    p->x = r * cosf(a);
    p->y = r * sinf(a);
    p->vx = -v * sinf(a);
    p->vy = v * cosf(a);
    p->cool = 0.0f;
    p->sector = ob_sector(p->x, p->y, c->spokes);
}
static inline void ob_restart(ob_world_t *w, const ob_cfg_t *c, uint32_t seed) {
    memset(w, 0, sizeof *w);
    w->rng = seed ? seed : 1u;
    for (int i = 0; i < OB_PLANETS; i++) ob_launch(w, c, i);
}

/* the world moves on by dt of its own time; returns the hits (into h, OB_HITS at most) */
static inline int ob_step(ob_world_t *w, const ob_cfg_t *c, float dt, ob_hit_t *h) {
    int n = c->n < 1 ? 1 : c->n > OB_PLANETS ? OB_PLANETS : c->n, nh = 0;
    float x0[OB_PLANETS], y0[OB_PLANETS], d = dt / OB_SUB;
    for (int i = 0; i < n; i++) { x0[i] = w->p[i].x; y0[i] = w->p[i].y; }
    for (int k = 0; k < OB_SUB; k++) {
        for (int i = 0; i < n; i++) {   /* kick (the sun, and the others' pull), then drift: energy stays bounded */
            ob_planet_t *p = &w->p[i];
            float r2 = p->x * p->x + p->y * p->y + OB_EPS2, f = 1.0f / (r2 * sqrtf(r2)), ax = -p->x * f, ay = -p->y * f;
            for (int j = 0; c->pull > 0.0f && j < n; j++) {
                if (j == i) continue;
                float dx = w->p[j].x - p->x, dy = w->p[j].y - p->y, q2 = dx * dx + dy * dy + OB_EPS2;
                float g = c->pull / (q2 * sqrtf(q2));
                ax += dx * g;
                ay += dy * g;
            }
            p->vx += ax * d;
            p->vy += ay * d;
        }
        for (int i = 0; i < n; i++) { w->p[i].x += w->p[i].vx * d; w->p[i].y += w->p[i].vy * d; }
    }
    for (int i = 0; i < n; i++) {
        ob_planet_t *p = &w->p[i];
        float r2 = p->x * p->x + p->y * p->y;
        if (r2 > OB_OUT * OB_OUT || r2 < OB_IN * OB_IN || !isfinite(r2)) { ob_launch(w, c, i); continue; }
        if (p->cool > 0.0f) p->cool -= dt;
        int s = ob_sector(p->x, p->y, c->spokes), was = p->sector;
        p->sector = s;
        if (s == was || p->cool > 0.0f || nh >= OB_HITS) continue;
        int spoke = s == (was + 1) % c->spokes ? s : was == (s + 1) % c->spokes ? was : s;   /* the spoke between */
        float a0 = ob_angle(x0[i], y0[i]), a1 = ob_angle(p->x, p->y), sa = spoke * 6.2831853f / c->spokes;
        float span = ob_wrap(a1 - a0), part = ob_wrap(sa - a0), frac;
        if (span > 3.1415927f) { span = 6.2831853f - span; part = 6.2831853f - part; }   /* (going clockwise) */
        frac = span > 1e-6f ? part / span : 0.5f;
        h[nh++] = (ob_hit_t){(uint8_t)i, (uint8_t)spoke, sqrtf(p->vx * p->vx + p->vy * p->vy),
                             frac < 0.0f ? 0.0f : frac > 0.999f ? 0.999f : frac};
        p->cool = OB_COOL;
    }
    return nh;
}

/* ---- PENDULUMS ---- */
#define PD_MAX 12
enum { PDT_CENTRE, PDT_ENDS, PDT_BOTH };   /* "pend_trigger" */
typedef struct { uint8_t who, end; float frac; } pd_hit_t;   /* end: 0 the centre, 1 an end */

/* half-swings of pendulum i (swings: the first one's whole swings a cycle) from the start, at `cycles` cycles in */
static inline double pd_half(int i, int swings, int spread, double cycles) {
    return 2.0 * (swings + i * spread) * cycles;
}
/* the hits between u0 and u1 half-swings of one pendulum: the centre at m + 0.5, an end at each whole m (at most 4) */
static inline int pd_hits(int who, double u0, double u1, int trig, pd_hit_t *h, int nh, int max) {
    if (!(u1 > u0) || u1 - u0 > 4.0) return nh;   /* (backwards, or a jump of the song position: no burst) */
    for (int end = 0; end < 2; end++) {
        if (end == 0 ? trig == PDT_ENDS : trig == PDT_CENTRE) continue;
        double off = end ? 0.0 : 0.5;
        for (double m = floor(u0 - off) + 1.0; m + off <= u1 && nh < max; m += 1.0)
            if (m + off > u0) h[nh++] = (pd_hit_t){(uint8_t)who, (uint8_t)end, (float)((m + off - u0) / (u1 - u0))};
    }
    return nh;
}

/* ---- the engine's side (src/gens_a_run.h): one per Synth, in its one_t; runtime state only, never saved ---- */
#define GA_STEP 64         /* the worlds move every 64 samples (1.45 ms); a hit's note falls on its own sample */
#define GA_Q 32            /* notes waiting for their sample (or the tempo grid), per generator */
#define GA_VIEW_EVERY 4    /* the window's views are written every fourth step */
#define GA_FLASH 80        /* steps a cell, planet, spoke or pendulum stays lit after it played (about 0.12 s) */
enum { GA_LIFE = 1, GA_ORB = 2, GA_PEND = 4 };   /* the generators' bits in on and held */

typedef struct {
    float tokens;            /* MAX NOTES/S: notes it may still play */
    struct { int64_t at; uint8_t note, vel; } q[GA_Q];
    int nq;
} ga_out_t;

typedef struct {
    int on;                  /* GA_* bits of the generators on (with the Synth on) */
    int held;                /* GA_* bits of those set to HELD KEYS */
    int64_t t_next;          /* the sample of the next step */
    int ticks;
    double b0, bps0;         /* the beat clock: the beat at sample t0, and beats per sample ... */
    int64_t t0, sp_at;       /* ... and the sample the host's last song position was given at */
    int follow;              /* the beat follows the host's song position (its transport plays) */
    /* GAME OF LIFE */
    lf_world_t lf;
    ga_out_t lo;
    int lhead, lseed, lpress;   /* the scan's place; the SEED it was seeded from; RESEED presses since it went on */
    int64_t lgrid;           /* the last tempo grid line it stepped on */
    float lacc, lpop;        /* FREE: steps owed; the population 0 .. 1 (a modulation source) */
    uint16_t llit[LF_ROWS];  /* the cells that played at the last step ... */
    int lflash;              /* ... lit for this many more steps */
    /* GRAVITY ORBITS */
    ob_world_t ob;
    ob_cfg_t oc;
    ga_out_t oo;
    uint8_t oflash[OB_PLANETS], sflash[OB_SPOKES];
    float ox, oy;            /* planet 1 across and up, 0 .. 1 (modulation sources) */
    /* PENDULUMS */
    double pstart;           /* the beat the swing started at */
    float pamp, p1, psync;   /* the swing's size 0 .. 1; pendulum 1's place and how in step they are, 0 .. 1 */
    ga_out_t po;
    uint8_t pflash[PD_MAX];
    int16_t px[PD_MAX];      /* each one's place, -1000 .. 1000 */
    /* the window's views, written by the audio thread without a lock: each counter odd while it is written */
    uint32_t lvseq, ovseq, pvseq;
    uint16_t vrows[LF_ROWS], vlit[LF_ROWS];
    uint8_t vmode, vhead, vpop;
    uint8_t von, vspokes, vslit, vof[OB_PLANETS];
    int16_t vox[OB_PLANETS], voy[OB_PLANETS];
    uint8_t vpn, vpf[PD_MAX];
    int16_t vpx[PD_MAX];
    float tf;                /* (2026-10-30) FOLLOW TEMPO: LIFE FREE's and the orbits' speed factor less 1, gliding */
    /* (2026-10-30) KEY LAUNCH: GA_* bits of those set to it, of those that take the keys (HELD or LAUNCH), of those a
     * key has launched and that still play; each one's key (LIFE, ORBITS, PENDULUMS) */
    int launch, takes, lgo;
    int lkey[3];
} ga_run_t;
