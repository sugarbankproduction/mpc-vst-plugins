/* Low-frequency oscillator for modulation, run at control rate. Seven waves, each with a VARIATION (0..1) that morphs
 * it towards its partner, as on the Moog One's wave categories: Triangle <-> Sine, Saw (falling) <-> Ramp (rising)
 * through a triangle half way, S&H (a random step each cycle) <-> Noise (smooth random), and Square, whose width
 * narrows from 50% to nothing ("always low") as the variation goes up. Bipolar (-1..1) or unipolar (0..1), with a
 * one-pole smoothing filter on the output.
 *
 * An lfo_t is one running LFO: the engine keeps a free-running one per LFO and, for LFOs that restart per note (note-on
 * reset, a repeat count) or are modulated per voice, one per voice. lfo_advance() moves the phase on (the caller works
 * out the step from the rate, the tempo or the matrix), lfo_shape() reads the wave there. No allocation. */
#pragma once
#include <math.h>
#include <stdint.h>

/* option order in params.json. PULSE (2026-10-29): the manual's pulse, its width the variation over the whole range,
 * 0% always low ("off") .. 50% a square .. 100% always high ("on"); Square keeps its old narrowing from 50% */
enum { LFO_TRI, LFO_SINE, LFO_SQUARE, LFO_SAW, LFO_RAMP, LFO_SH, LFO_NOISE, LFO_PULSE, LFO_WAVES };

typedef struct {
    float phase;    /* 0..1 */
    float prev;     /* the last cycle's random value (where smooth noise comes from) */
    float next;     /* this cycle's random value (S&H holds it; smooth noise heads for it) */
    float smooth;   /* the smoothing filter's state */
    int cycles;     /* whole cycles since the last reset (for a repeat count) */
    uint32_t rng;
} lfo_t;

static inline float lfo_rand(lfo_t *l) {   /* xorshift32: -1..1 */
    l->rng ^= l->rng << 13; l->rng ^= l->rng >> 17; l->rng ^= l->rng << 5;
    return (int32_t)l->rng * (1.0f / 2147483648.0f);
}

static inline void lfo_init(lfo_t *l, uint32_t seed) {
    l->phase = 0.0f;
    l->prev = l->next = l->smooth = 0.0f;
    l->cycles = 0;
    l->rng = seed ? seed : 1u;
}

/* restart at a start phase (0..1), as on a note-on reset; the random values carry on */
static inline void lfo_reset(lfo_t *l, float phase) { l->phase = phase - floorf(phase); l->cycles = 0; }

/* move on by inc cycles (frequency x samples / sample rate, below 1); a new cycle draws a new random value */
static inline void lfo_advance(lfo_t *l, float inc) {
    float t = l->phase + inc;
    if (t >= 1.0f) {
        t -= floorf(t);
        l->prev = l->next;
        l->next = lfo_rand(l);
        l->cycles++;
    }
    l->phase = t;
}

static inline float lfo_tri(float t) { return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; }   /* starts at -1 */

/* a rise from -1 to 1 over the first r of the cycle, then a fall back to -1: r = 0 is a saw, 1 a ramp, 0.5 a triangle */
static inline float lfo_slope(float t, float r) {
    if (r <= 0.0f) return 1.0f - 2.0f * t;
    if (r >= 1.0f) return 2.0f * t - 1.0f;
    return t < r ? -1.0f + 2.0f * t / r : 1.0f - 2.0f * (t - r) / (1.0f - r);
}

/* the wave at the current phase, -1..1, with the variation v (0..1) */
static inline float lfo_shape(const lfo_t *l, int wave, float v) {
    float t = l->phase;
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    switch (wave) {
    case LFO_SINE:
        if (v <= 0.0f) return sinf(6.2831853f * t);
        return (1.0f - v) * sinf(6.2831853f * t) + v * lfo_tri(t + 0.25f < 1.0f ? t + 0.25f : t - 0.75f);
    case LFO_SQUARE: return t < 0.5f * (1.0f - v) ? 1.0f : -1.0f;
    case LFO_PULSE:  return t < v ? 1.0f : -1.0f;
    case LFO_SAW:    return lfo_slope(t, v);
    case LFO_RAMP:   return lfo_slope(t, 1.0f - v);
    case LFO_SH:
    case LFO_NOISE: {
        float s = t * t * (3.0f - 2.0f * t), glide = l->prev + (l->next - l->prev) * s;   /* smooth: prev to next */
        float k = wave == LFO_SH ? v : 1.0f - v;   /* the share of smooth noise */
        return (1.0f - k) * l->next + k * glide;
    }
    default:   /* triangle, and its sine aligned with it */
        return v <= 0.0f ? lfo_tri(t) : (1.0f - v) * lfo_tri(t) - v * cosf(6.2831853f * t);
    }
}

/* the smoothing filter: coef 1 lets the wave through, smaller values round it off */
static inline float lfo_smooth(lfo_t *l, float x, float coef) {
    if (coef >= 1.0f) return l->smooth = x;
    return l->smooth += (x - l->smooth) * coef;
}
