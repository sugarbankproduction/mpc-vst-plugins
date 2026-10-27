/* Band-limited oscillator: a naive saw/square with PolyBLEP corrections at each jump, which removes most of the
 * aliasing (the whistle of high notes folding back below Nyquist) for a few multiplies per sample. No state beyond
 * the phase, no allocation: safe in the audio path. */
#pragma once

enum { WAVE_SAW, WAVE_SQUARE, WAVE_COUNT };

typedef struct {
    float phase;   /* 0..1 */
    float inc;     /* frequency / sample rate, below 0.5 */
} osc_t;

/* the residual that turns a hard step of height 2 at t = 0 into a smooth one, spread over one sample each side */
static inline float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline void osc_set_freq(osc_t *o, float hz, float sr) {
    float inc = hz / sr;
    o->inc = inc < 0.49f ? inc : 0.49f;
}

static inline float osc_next(osc_t *o, int wave) {
    float t = o->phase, dt = o->inc, v;
    if (wave == WAVE_SQUARE) {
        float t2 = t + 0.5f;
        if (t2 >= 1.0f) t2 -= 1.0f;
        v = (t < 0.5f ? 1.0f : -1.0f) + poly_blep(t, dt) - poly_blep(t2, dt);
    } else {
        v = 2.0f * t - 1.0f - poly_blep(t, dt);
    }
    t += dt;
    if (t >= 1.0f) t -= 1.0f;
    o->phase = t;
    return v;
}
