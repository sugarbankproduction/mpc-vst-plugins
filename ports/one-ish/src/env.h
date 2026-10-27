/* ADSR envelope: linear attack, exponential decay and release (the shape analog envelopes have). Times are set
 * outside the audio path (env_set_times); env_next is a couple of multiplies per sample, no allocation. */
#pragma once
#include <math.h>

enum { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    int stage;
    float level;
    float attack_inc;               /* level added per sample in the attack */
    float decay_coef, release_coef; /* share of the remaining distance covered per sample */
    float sustain;                  /* 0..1 */
} env_t;

/* exponential segments reach 99% of the way in `ms` (ln 100 = 4.6 time constants) */
static inline float env_coef(float ms, float sr) {
    float samples = ms * 0.001f * sr;
    return samples < 1.0f ? 1.0f : 1.0f - expf(-4.6f / samples);
}

static inline void env_set_times(env_t *e, float a_ms, float d_ms, float s, float r_ms, float sr) {
    float a = a_ms * 0.001f * sr;
    e->attack_inc = a < 1.0f ? 1.0f : 1.0f / a;
    e->decay_coef = env_coef(d_ms, sr);
    e->release_coef = env_coef(r_ms, sr);
    e->sustain = s;
}

/* a retrigger starts the attack from the current level, so a new note never clicks down to zero first */
static inline void env_gate_on(env_t *e) { e->stage = ENV_ATTACK; }
static inline void env_gate_off(env_t *e) { if (e->stage != ENV_IDLE) e->stage = ENV_RELEASE; }

static inline float env_next(env_t *e) {
    switch (e->stage) {
    case ENV_ATTACK:
        e->level += e->attack_inc;
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = ENV_DECAY; }
        break;
    case ENV_DECAY:
        e->level += (e->sustain - e->level) * e->decay_coef;
        /* 1e-3 (-60 dB): float steps near 0.7 are too fine for a long decay to get closer by itself */
        if (fabsf(e->level - e->sustain) < 1e-3f) { e->level = e->sustain; e->stage = ENV_SUSTAIN; }
        break;
    case ENV_SUSTAIN:
        e->level = e->sustain;   /* follows a sustain change while held */
        break;
    case ENV_RELEASE:
        e->level -= e->level * e->release_coef;
        if (e->level < 1e-4f) { e->level = 0.0f; e->stage = ENV_IDLE; }
        break;
    default:
        e->level = 0.0f;
    }
    return e->level;
}
