/* DAHDSR envelope: delay, attack, hold, decay, sustain, release, each timed stage with its own curve from log through
 * linear to exponential, plus loop (the whole envelope repeats as a trapezoid while held) and latch (key up is
 * ignored: it holds at the sustain level, or keeps looping).
 *
 * Every timed stage moves a progress value u from 0 to 1 with u = u * m + b per sample: m = 1 is a straight line,
 * m < 1 bends it to a fast start and slow end, m > 1 to a slow start and fast end. m and b are worked out once per
 * setting (env_config, outside the audio path) so u reaches 1 in exactly the stage's time whatever its curve, and the
 * curve is time-invariant: an attack restarted from the current level just continues the same curve from there, at
 * the same rate. Per sample that is one multiply-add and a compare, no exp or divide.
 * The level follows u: delay holds the level it started at, attack is u (0 to 1), hold is 1, decay is 1 to sustain,
 * release is its starting level to 0. Settings are shared by every voice (env_cfg_t, one per envelope); each voice's
 * env_t points at its envelope's. No allocation. */
#pragma once
#include <math.h>

enum { ENV_IDLE, ENV_DELAY, ENV_ATTACK, ENV_HOLD, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_NSTAGE };

#define ENV_BLOCK 16   /* env_run's fast path: the engine's control-rate step */

/* a timed stage: u -> u * m + b per sample, done at u >= 1; mn, bn do ENV_BLOCK samples in one step */
typedef struct { double m, b, mn, bn; } env_seg_t;

typedef struct {
    env_seg_t seg[ENV_NSTAGE];   /* by stage: delay, attack, hold, decay, release (idle and sustain unused) */
    int has_delay, has_hold;     /* zero delay or hold time: the stage is skipped, not one sample long */
    float sustain;               /* 0..1 */
    float kill_coef;             /* All Sound Off's fast fade, and how fast a held sustain follows its knob */
    float kill_coef_n;           /* the same over ENV_BLOCK samples */
    int loop, latch;             /* the LOOP and LATCH buttons */
    int multi, reset;            /* MULTI TRIG and RESET: read by the engine and env_gate_on */
} env_cfg_t;

typedef struct {
    const env_cfg_t *cfg;
    int stage;
    int fast;                    /* releasing in ENV_KILL_MS (All Sound Off), not the set release time */
    int held;                    /* key down or latched: a looping envelope starts again after its release */
    float level;
    float start;                 /* the level the delay or release stage began at */
    double u;                    /* progress through the current timed stage, 0..1 */
} env_t;

#define ENV_KILL_MS 2.0f   /* All Sound Off: fast enough to be "now", slow enough not to click */
#define ENV_TAU_MAX 9.21   /* a curve at 100%: the stage covers ln(1e4) = 9.21 time constants (-80 dB of the way) */

/* one-pole coefficient: exponential segments reach 99% of the way in `ms` (ln 100 = 4.6 time constants) */
static inline float env_coef(float ms, float sr) {
    float samples = ms * 0.001f * sr;
    return samples < 1.0f ? 1.0f : 1.0f - expf(-4.6f / samples);
}

/* a stage of `ms` with a bend of -1..1: 0 is a straight line, +1 the fastest start (a classic exponential decay),
 * -1 the slowest start. u_n = F (1 - m^n) with m = e^(-tau / n_total) and F = 1 / (1 - e^-tau) lands on 1 at the end. */
static inline env_seg_t env_seg(float ms, float bend, float sr) {
    env_seg_t g = {1.0, 1.0, 1.0, 0.0};
    double n = (double)ms * 0.001 * sr;
    double tau = (double)bend * ENV_TAU_MAX;
    if (n < 1.0) g.b = 1.0;                      /* shorter than a sample: done in one */
    else if (fabs(tau) < 1e-6) g.b = 1.0 / n;
    else {
        g.m = exp(-tau / n);
        g.b = expm1(-tau / n) / expm1(-tau);     /* F (1 - m), written so it stays accurate when m is close to 1 */
    }
    for (int i = 0; i < ENV_BLOCK; i++) { g.bn = g.bn * g.m + g.b; g.mn *= g.m; }   /* the step applied ENV_BLOCK times */
    return g;
}

/* times in ms (delay, attack, hold, decay, release), sustain 0..1, curves -1 (log) .. 0 (linear) .. +1 (exp): an
 * exponential attack starts slowly and speeds up, an exponential decay or release falls fast and then slows */
static inline void env_config(env_cfg_t *c, const float ms[5], float sustain, float acurve, float dcurve, float rcurve,
                              float sr) {
    c->seg[ENV_DELAY] = env_seg(ms[0], 0.0f, sr);
    c->seg[ENV_ATTACK] = env_seg(ms[1], -acurve, sr);
    c->seg[ENV_HOLD] = env_seg(ms[2], 0.0f, sr);
    c->seg[ENV_DECAY] = env_seg(ms[3], dcurve, sr);
    c->seg[ENV_RELEASE] = env_seg(ms[4], rcurve, sr);
    c->has_delay = ms[0] > 0.0f;
    c->has_hold = ms[2] > 0.0f;
    c->sustain = sustain;
    c->kill_coef = env_coef(ENV_KILL_MS, sr);
    c->kill_coef_n = 1.0f - powf(1.0f - c->kill_coef, ENV_BLOCK);
}

static inline void env_enter(env_t *e, int stage) {
    const env_cfg_t *c = e->cfg;
    if (stage == ENV_DELAY && !c->has_delay) stage = ENV_ATTACK;
    if (stage == ENV_HOLD && !c->has_hold) stage = ENV_DECAY;
    e->stage = stage;
    e->u = 0.0;
    if (stage == ENV_DELAY || stage == ENV_RELEASE) e->start = e->level;
    else if (stage == ENV_ATTACK) e->u = e->level;   /* on from the current level, at the attack's own rate */
    else if (stage == ENV_HOLD) e->level = 1.0f;
}

/* a new note: from the current level (no click), or from zero with RESET on. Starts at the delay stage. */
static inline void env_gate_on(env_t *e) {
    e->held = 1;
    e->fast = 0;
    if (e->cfg->reset) e->level = 0.0f;
    env_enter(e, ENV_DELAY);
}

/* let go, latched or not (All Notes Off, LATCH switched off): release from the current level, then stop */
static inline void env_release(env_t *e) {
    e->held = 0;
    if (e->stage != ENV_IDLE && e->stage != ENV_RELEASE) env_enter(e, ENV_RELEASE);
}

/* key up: ignored while latched */
static inline void env_gate_off(env_t *e) { if (!e->cfg->latch) env_release(e); }

static inline void env_kill(env_t *e) {
    e->held = 0;
    if (e->stage != ENV_IDLE) { e->stage = ENV_RELEASE; e->fast = 1; }
}

/* a timed stage has run its course */
static inline void env_stage_done(env_t *e) {
    const env_cfg_t *c = e->cfg;
    switch (e->stage) {
    case ENV_DELAY: env_enter(e, ENV_ATTACK); break;
    case ENV_ATTACK: e->level = 1.0f; env_enter(e, ENV_HOLD); break;
    case ENV_HOLD: env_enter(e, ENV_DECAY); break;
    case ENV_DECAY:
        e->level = c->sustain;
        if (c->loop) env_enter(e, ENV_RELEASE);                    /* a loop goes straight on to its release */
        else e->stage = c->sustain > 0.0f ? ENV_SUSTAIN : ENV_IDLE;   /* nothing left to hear: a pluck frees its voice */
        break;
    default:   /* release */
        e->level = 0.0f;
        if (c->loop && e->held) env_enter(e, ENV_DELAY);           /* and round again */
        else e->stage = ENV_IDLE;
    }
}

/* the level at the current stage's progress */
static inline float env_level(env_t *e) {
    float u = (float)e->u;
    switch (e->stage) {
    case ENV_ATTACK: e->level = u; break;
    case ENV_DECAY: e->level = 1.0f + (e->cfg->sustain - 1.0f) * u; break;
    case ENV_RELEASE: e->level = e->start * (1.0f - u); break;
    default: break;   /* delay keeps its starting level, hold stays at 1 */
    }
    return e->level;
}

static inline float env_next(env_t *e) {
    const env_cfg_t *c = e->cfg;
    switch (e->stage) {
    case ENV_IDLE:
        e->level = 0.0f;
        return 0.0f;
    case ENV_SUSTAIN:
        if (c->loop) { env_enter(e, ENV_RELEASE); break; }        /* LOOP switched on while sustaining */
        e->level += (c->sustain - e->level) * c->kill_coef;      /* follows a sustain change while held, without a step */
        return e->level;
    case ENV_RELEASE:
        if (e->fast) {
            e->level -= e->level * c->kill_coef;
            if (e->level < 1e-4f) { e->level = 0.0f; e->stage = ENV_IDLE; e->fast = 0; }
            return e->level;
        }
        break;
    default:
        break;
    }
    const env_seg_t *g = &c->seg[e->stage];
    e->u = e->u * g->m + g->b;
    if (e->u >= 1.0 - 1e-9) {
        env_stage_done(e);
        return e->level;
    }
    return env_level(e);
}

/* advance n samples and return the level reached: for envelopes read at control rate. A step of ENV_BLOCK samples
 * that stays inside one stage is worked out in one go (u only grows, so it ends inside if its end point does). */
static inline float env_run(env_t *e, int n) {
    const env_cfg_t *c = e->cfg;
    if (n == ENV_BLOCK) {
        int st = e->stage;
        if (st == ENV_IDLE) return e->level = 0.0f;
        if (st == ENV_SUSTAIN && !c->loop) return e->level += (c->sustain - e->level) * c->kill_coef_n;
        if (st != ENV_SUSTAIN && !e->fast) {
            const env_seg_t *g = &c->seg[st];
            double u = e->u * g->mn + g->bn;
            if (u < 1.0 - 1e-9) { e->u = u; return env_level(e); }
        }
    }
    for (int i = 1; i < n; i++) env_next(e);
    return env_next(e);
}
