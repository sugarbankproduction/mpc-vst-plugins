/* ONE-ish's physical note generators (2026-10-29), in the GENERATIVE section beside the Euclidean rhythm and the
 * Turing machine (beyond the Moog One): BOUNCING BALLS (1-8 balls in a square box, with speed, gravity, the box's size
 * and how much of its speed a ball keeps at each bounce; each wall a ball hits plays a note) and PARTICLES (an emitter
 * at the top of the box sends out particles at a rate, with a spread of directions and a life; gravity and an
 * attraction to the box's centre move them; a particle crossing a line, two particles colliding, or a particle dying
 * plays a note). This file is the world only: positions in the box (0..1 each way, y up), its step, and what each step
 * hit (which wall or event, where, how hard, and when within the step). Which note and velocity a hit plays, the
 * tempo grid, the note rate cap and the window's view are engine.c's (src/physgen_run.h).
 * Fixed-size arrays (8 balls, a pool of 32 particles), a seeded random sequence of its own (seq.h's xorshift), no
 * allocation, no locks, no files: safe in the audio thread, and the same seed always gives the same hits.
 * (2026-10-30) Each wall PLAYS (as before), is SILENT (a ball bounces off it without a note: the hit is reported with
 * `silent` set, for the matrix's BALL HIT) or OPEN (a ball leaves through it and another is thrown in, or with KEY
 * LAUNCH it is gone; a particle leaves). Particles never play the walls, so for them only OPEN changes anything. With
 * the keys' LAUNCH and EMIT ways (`keyed`) only the balls marked live are in the box, and each ball and particle keeps
 * the key it came from (its notes start there: src/physgen_run.h). Every wall PLAYS and not keyed: the very same steps,
 * random numbers and hits as before. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "seq.h"

#define PH_BALLS 8         /* balls at most */
#define PH_PARTS 32        /* the particle pool */
#define PH_HITS 48         /* the hits one step reports at most (more are dropped) */
#define PH_BALL_R 0.035f   /* a ball's radius, in box widths */
#define PH_PART_R 0.04f    /* a particle's reach for COLLIDE (two touch at twice this) */
#define PH_LINE_Y 0.25f    /* the LINE particles play crossing, a quarter of the way up */
#define PH_EMIT_X 0.5f     /* the emitter, near the top in the middle */
#define PH_EMIT_Y 0.88f
#define PH_LINE_COOL 0.15f /* a particle that just played waits this long (s) before it plays again ... */
#define PH_HIT_COOL 0.25f  /* ... or after a collision */
#define PH_REST 0.12f      /* a ball bouncing lower than this share of its fall speed is resting: it is thrown again */

enum { PH_FLOOR, PH_CEIL, PH_LEFT, PH_RIGHT, PH_LINE, PH_COLLIDE, PH_DIE };   /* what a hit was */
enum { PHT_LINE, PHT_COLLIDE, PHT_DIE };                                      /* "phys_trigger": what particles play */
enum { PHW_PLAY, PHW_SILENT, PHW_OPEN };   /* (2026-10-30) "phys_floor", "phys_ceil", "phys_left", "phys_right" */

/* live, key (2026-10-30): read only when keyed (KEY LAUNCH, KEY EMIT); key: the note its notes start from */
typedef struct { float x, y, vx, vy; uint8_t live; int16_t key; } ph_ball_t;
typedef struct { float x, y, vx, vy, age, life, cool; uint8_t live; int16_t key; } ph_part_t;
/* frac: when in the step, 0 .. <1; silent (2026-10-30): off a SILENT wall (no note) */
typedef struct { uint8_t kind, who; float x, y, speed, frac; uint8_t silent; } ph_hit_t;

/* the settings a step reads, in box units (the box is 1 x 1; speed and gravity already divided by its size) */
typedef struct {
    int balls, nballs;          /* the balls on, and how many */
    float speed, gravity;       /* box widths per second, per second squared */
    float bounce;               /* the share of its speed a ball (or particle) keeps at a wall, 0.5 .. 1 */
    int parts, trigger;         /* the particles on, and what plays (PHT_*) */
    float rate, spread, life;   /* particles per second; 0 .. 1 of a full circle around straight down; seconds */
    float attract;              /* the pull towards the centre, per second squared per box width */
    /* (2026-10-30; all zero: as before) each wall's way (PHW_*; floor, ceiling, left, right: the PH_FLOOR order);
     * keyed: only the balls marked live are in the box; refill: a keyed ball that leaves (an open wall) or comes to rest
     * is thrown in again (else it is gone: KEY LAUNCH). Not keyed, it is always thrown in again. */
    uint8_t wall[4];
    int keyed, refill;
} ph_cfg_t;

typedef struct {
    ph_ball_t b[PH_BALLS];
    ph_part_t p[PH_PARTS];
    float emit;      /* particles owed (the rate's fraction carried over) */
    int16_t emit_key;   /* (2026-10-30) the key the particles it emits itself play from */
    uint32_t rng;    /* its random numbers: never 0 (xorshift) */
} ph_world_t;

static inline float ph_rand(ph_world_t *w) { return seq_rand01(&w->rng); }

/* a ball thrown at `speed` in a random direction from a random place (gravity: from the top half, falling) */
static inline void ph_throw(ph_world_t *w, ph_ball_t *b, float speed, int from_top) {
    float a = ph_rand(w) * 6.2831853f, lo = PH_BALL_R, span = 1.0f - 2.0f * PH_BALL_R;
    b->x = lo + span * ph_rand(w);
    b->y = from_top ? lo + span * (0.5f + 0.5f * ph_rand(w)) : lo + span * ph_rand(w);
    b->vx = speed * cosf(a);
    b->vy = speed * sinf(a);
}

/* everything from the start: the same seed, the same balls and the same particles every time (seed 0 is made 1) */
static inline void ph_restart(ph_world_t *w, uint32_t seed, float speed) {
    memset(w, 0, sizeof *w);
    w->rng = seed ? seed : 1u;
    for (int i = 0; i < PH_BALLS; i++) ph_throw(w, &w->b[i], speed, 0);
}

static inline int ph_add(ph_hit_t *h, int n, int kind, int who, float x, float y, float speed, float frac) {
    if (n >= PH_HITS) return n;
    h[n] = (ph_hit_t){(uint8_t)kind, (uint8_t)who, x, y, speed, frac < 0.0f ? 0.0f : frac > 0.999f ? 0.999f : frac, 0};
    return n + 1;
}

/* (2026-10-30) a wall hit (kind PH_FLOOR .. PH_RIGHT): reported, marked silent when its wall is SILENT */
static inline int ph_add_wall(const ph_cfg_t *c, ph_hit_t *h, int n, int kind, int who, float x, float y, float speed,
                              float frac) {
    int m = ph_add(h, n, kind, who, x, y, speed, frac);
    if (m > n && c->wall[kind] == PHW_SILENT) h[n].silent = 1;
    return m;
}

/* (2026-10-30) whether something at (x, y) has gone out through an OPEN wall */
static inline int ph_out(const ph_cfg_t *c, float x, float y) {
    float lo = PH_BALL_R, hi = 1.0f - PH_BALL_R;
    return (y < lo && c->wall[PH_FLOOR] == PHW_OPEN) || (y > hi && c->wall[PH_CEIL] == PHW_OPEN) ||
           (x < lo && c->wall[PH_LEFT] == PHW_OPEN) || (x > hi && c->wall[PH_RIGHT] == PHW_OPEN);
}

/* (2026-10-30) a ball that rested or left: thrown in again, or (keyed without refill: KEY LAUNCH) gone */
static inline void ph_again(ph_world_t *w, const ph_cfg_t *c, ph_ball_t *b, int from_top) {
    if (c->refill || !c->keyed) ph_throw(w, b, c->speed, from_top);
    else b->live = 0;
}

/* one axis of a ball against its two walls: reflected, its speed times bounce; the hit's place in the step from where
 * it crossed. *hit: -1 the low wall, 1 the high one, 0 none; *imp: the speed it hit with */
static inline float ph_wall(float p0, float *p, float *v, float bounce, int *hit, float *imp) {
    float lo = PH_BALL_R, hi = 1.0f - PH_BALL_R, d = *p - p0, f = 0.0f;
    *hit = 0;
    if (*p < lo) { f = d != 0.0f ? (lo - p0) / d : 0.0f; *p = lo + (lo - *p) * bounce; *hit = -1; }
    else if (*p > hi) { f = d != 0.0f ? (hi - p0) / d : 0.0f; *p = hi - (*p - hi) * bounce; *hit = 1; }
    if (*hit) { *imp = fabsf(*v); *v = -*v * bounce; }
    if (*p < lo) *p = lo;   /* (a fast ball in a small box: kept inside) */
    if (*p > hi) *p = hi;
    return f;
}

static int ph_step_balls(ph_world_t *w, const ph_cfg_t *c, float dt, ph_hit_t *h, int n) {
    float fall = sqrtf(2.0f * c->gravity);   /* the speed of a fall through the whole box */
    for (int i = 0; i < c->nballs && i < PH_BALLS; i++) {
        ph_ball_t *b = &w->b[i];
        if (c->keyed && !b->live) continue;
        float x0 = b->x, y0 = b->y, imp = 0.0f;
        int hx, hy;
        b->vy -= c->gravity * dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        if (ph_out(c, b->x, b->y)) {   /* (2026-10-30) out through an OPEN wall: another one in (KEY LAUNCH: gone) */
            ph_again(w, c, b, c->gravity > 0.0f);
            continue;
        }
        float fx = ph_wall(x0, &b->x, &b->vx, c->bounce, &hx, &imp);
        if (hx) n = ph_add_wall(c, h, n, hx < 0 ? PH_LEFT : PH_RIGHT, i, hx < 0 ? 0.0f : 1.0f, b->y, imp, fx);
        float fy = ph_wall(y0, &b->y, &b->vy, c->bounce, &hy, &imp);
        if (hy < 0 && c->gravity > 0.0f && imp < PH_REST * fall) {   /* resting on the floor: thrown again from above */
            ph_again(w, c, b, 1);
            continue;
        }
        if (hy) n = ph_add_wall(c, h, n, hy < 0 ? PH_FLOOR : PH_CEIL, i, b->x, hy < 0 ? 0.0f : 1.0f, imp, fy);
        if (c->gravity <= 0.0f && c->bounce < 1.0f && b->vx * b->vx + b->vy * b->vy < 0.0625f * c->speed * c->speed)
            ph_again(w, c, b, 0);   /* no gravity, slowed to a quarter of its speed: thrown again */
    }
    return n;
}

/* one particle from the emitter, its notes from `key` (2026-10-30: KEY LAUNCH's bursts and KEY EMIT call it too) */
static void ph_emit_key(ph_world_t *w, const ph_cfg_t *c, int key) {
    for (int i = 0; i < PH_PARTS; i++) {
        ph_part_t *p = &w->p[i];
        if (p->live) continue;
        float a = -1.5707963f + (ph_rand(w) - 0.5f) * 6.2831853f * c->spread, v = c->speed * (0.7f + 0.6f * ph_rand(w));
        *p = (ph_part_t){PH_EMIT_X, PH_EMIT_Y, v * cosf(a), v * sinf(a), 0.0f, c->life * (0.75f + 0.5f * ph_rand(w)),
                         0.0f, 1, (int16_t)key};
        return;
    }   /* (the pool full: none this time) */
}
static void ph_emit(ph_world_t *w, const ph_cfg_t *c) { ph_emit_key(w, c, w->emit_key); }

static int ph_step_parts(ph_world_t *w, const ph_cfg_t *c, float dt, ph_hit_t *h, int n) {
    w->emit += c->rate * dt;
    while (w->emit >= 1.0f) { w->emit -= 1.0f; ph_emit(w, c); }
    for (int i = 0; i < PH_PARTS; i++) {
        ph_part_t *p = &w->p[i];
        if (!p->live) continue;
        p->age += dt;
        if (p->age >= p->life) {   /* its life is over */
            p->live = 0;
            if (c->trigger == PHT_DIE) n = ph_add(h, n, PH_DIE, i, p->x, p->y, sqrtf(p->vx * p->vx + p->vy * p->vy), 0.5f);
            continue;
        }
        float x0 = p->x, y0 = p->y, imp;
        int hit;
        p->vx += c->attract * (0.5f - p->x) * dt;
        p->vy += (c->attract * (0.5f - p->y) - c->gravity) * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (ph_out(c, p->x, p->y)) {   /* (2026-10-30) out through an OPEN wall: gone, silently */
            p->live = 0;
            continue;
        }
        ph_wall(x0, &p->x, &p->vx, c->bounce, &hit, &imp);   /* off the walls, silently */
        ph_wall(y0, &p->y, &p->vy, c->bounce, &hit, &imp);
        if (p->cool > 0.0f) p->cool -= dt;
        if (c->trigger == PHT_LINE && p->cool <= 0.0f && (y0 < PH_LINE_Y) != (p->y < PH_LINE_Y)) {
            float d = p->y - y0;
            n = ph_add(h, n, PH_LINE, i, p->x, PH_LINE_Y, fabsf(p->vy), d != 0.0f ? (PH_LINE_Y - y0) / d : 0.5f);
            p->cool = PH_LINE_COOL;
        }
    }
    if (c->trigger != PHT_COLLIDE) return n;
    const float touch = 4.0f * PH_PART_R * PH_PART_R;
    for (int i = 0; i < PH_PARTS; i++) {   /* every pair once: 496 at most */
        ph_part_t *a = &w->p[i];
        if (!a->live || a->cool > 0.0f) continue;
        for (int j = i + 1; j < PH_PARTS; j++) {
            ph_part_t *b = &w->p[j];
            if (!b->live || b->cool > 0.0f) continue;
            float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
            if (d2 >= touch) continue;
            float rvx = b->vx - a->vx, rvy = b->vy - a->vy, along = rvx * dx + rvy * dy;
            if (along >= 0.0f) continue;   /* already parting */
            float k = d2 > 1e-12f ? along / d2 : 0.0f;   /* equal masses: they swap their speeds along the line between */
            a->vx += k * dx; a->vy += k * dy;
            b->vx -= k * dx; b->vy -= k * dy;
            n = ph_add(h, n, PH_COLLIDE, i, 0.5f * (a->x + b->x), 0.5f * (a->y + b->y), sqrtf(rvx * rvx + rvy * rvy), 0.5f);
            a->cool = b->cool = PH_HIT_COOL;
            break;
        }
    }
    return n;
}

/* the world moves on by dt seconds; returns the hits (into h, PH_HITS at most), in the order found */
static inline int ph_step(ph_world_t *w, const ph_cfg_t *c, float dt, ph_hit_t *h) {
    int n = 0;
    if (c->balls) n = ph_step_balls(w, c, dt, h, n);
    if (c->parts) n = ph_step_parts(w, c, dt, h, n);
    return n;
}

/* ---- the engine's side (src/physgen_run.h): one per Synth, in its one_t; runtime state only, never saved ---- */
#define PH_STEP 64         /* the world moves every 64 samples (1.45 ms); a hit's note falls on its own sample */
#define PH_Q 32            /* hits waiting for their sample (or the tempo grid) */
#define PH_VIEW (PH_BALLS + PH_PARTS)
#define PH_VIEW_EVERY 4    /* the window's view is written every fourth step (about 170 times a second) */
#define PH_FLASH 80        /* steps an object (or a wall) stays lit after it played (about 0.12 s) */

typedef struct {
    ph_world_t w;
    ph_cfg_t cfg;            /* the settings as last applied */
    int on;                  /* bit 0: balls, bit 1: particles (with the Synth on) */
    int held;                /* HELD KEYS: the keys go to the generator (the step clock's way) and transpose it */
    int64_t t_next;          /* the sample of the next step */
    float tokens;            /* the note rate cap: notes it may still play (refilled at MAX NOTES a second) */
    struct { int64_t at; uint8_t note, vel; } q[PH_Q];   /* notes waiting for their sample */
    int nq;
    double b0, bps0;         /* SYNC: the beat at sample t0, and beats per sample */
    int64_t t0, sp_at;       /* ... and the sample the host's last song position was given at */
    int follow;              /* the beat follows the host's song position (its transport plays) */
    int ticks;
    uint8_t flash[PH_VIEW], wall[5];   /* steps left lit: each ball then each particle; each wall and the LINE */
    /* the window's view, written by the audio thread without a lock: vseq is odd while it is written (a seqlock) */
    uint32_t vseq;
    uint8_t vnb, vnp, vwall, vf[PH_VIEW];
    int16_t vx[PH_VIEW], vy[PH_VIEW];   /* 0 .. 1000 across and up the box */
    /* (2026-10-30) the keys' ways, FOLLOW OCTAVE and the matrix sources */
    int mode;                /* PHYS KEYS (PHK_*) */
    int takes;               /* the keys go to the generator (HELD KEYS, KEY LAUNCH, KEY EMIT: route_of) */
    int kb_oct;              /* the window's keyboard octave, -2 .. 2 ("kboct" from the window; never saved) */
    int launch;              /* KEY LAUNCH: the ball the next key throws in (oldest first) */
    float rate;              /* PARTICLE RATE, a second (the world's own is 0 while the keys emit) */
    float kemit[128];        /* KEY EMIT: the particles each held key owes */
    float m_raw[4];          /* the first ball's height and place, the balls' speed, the pool's fill, as the world has them */
    float m_hb, m_hp, m_eu, m_tm;   /* the pulses (1 at a hit, then falling) and the Turing machine's value */
    float m_out[8];          /* the sources, smoothed at control rate (MS_BALL_Y .. MS_EUCLID) */
} ph_run_t;
enum { PHK_FREE, PHK_HELD, PHK_LAUNCH, PHK_EMIT };   /* "phys_keys" (2026-10-30: LAUNCH and EMIT appended) */
