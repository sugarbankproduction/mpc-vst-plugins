/* Low-frequency oscillator for modulation: triangle, sine, square, saw (falling), ramp (rising), sample & hold.
 * Run at control rate: lfo_next() advances by a number of samples and returns the value there (-1..1).
 * Free-running, or tempo-synced by the caller passing a frequency worked out from the host's BPM. No allocation. */
#pragma once
#include <math.h>
#include <stdint.h>

enum { LFO_TRI, LFO_SINE, LFO_SQUARE, LFO_SAW, LFO_RAMP, LFO_SH, LFO_WAVES };   /* option order in params.json */

typedef struct {
    float phase;    /* 0..1 */
    float held;     /* sample & hold value for the current cycle */
    uint32_t rng;
} lfo_t;

static inline void lfo_init(lfo_t *l, uint32_t seed) { l->phase = 0.0f; l->held = 0.0f; l->rng = seed ? seed : 1u; }

static inline float lfo_next(lfo_t *l, int wave, float hz, int samples, float sr) {
    float t = l->phase + hz * samples / sr;
    if (t >= 1.0f) {   /* a new cycle: a new random step */
        t -= floorf(t);
        l->rng ^= l->rng << 13; l->rng ^= l->rng >> 17; l->rng ^= l->rng << 5;
        l->held = (int32_t)l->rng * (1.0f / 2147483648.0f);
    }
    l->phase = t;
    switch (wave) {
    case LFO_SINE:   return sinf(6.2831853f * t);
    case LFO_SQUARE: return t < 0.5f ? 1.0f : -1.0f;
    case LFO_SAW:    return 1.0f - 2.0f * t;
    case LFO_RAMP:   return 2.0f * t - 1.0f;
    case LFO_SH:     return l->held;
    default:         return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;   /* triangle, starts at -1 */
    }
}
