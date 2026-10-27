/* Band-limited oscillator: a naive saw/square with a PolyBLEP correction at every jump in the waveform -- its own
 * wrap and square edge, and a hard-sync reset from a master oscillator -- which removes most of the aliasing (the
 * whistle of high notes folding back below Nyquist) for a few multiplies per sample. A jump's correction spans the
 * sample before it and the one after, so the output is one sample late (23 us): that lets a sync reset, which can't
 * be seen coming, be smoothed like the rest. No allocation: safe in the audio path. */
#pragma once

enum { WAVE_SAW, WAVE_SQUARE, WAVE_COUNT };

typedef struct {
    float phase;    /* 0..1 */
    float inc;      /* frequency / sample rate, below 0.5 */
    float prev;     /* the previous sample, still open to the first half of a jump's correction */
    float wrap_d;   /* after osc_next: how long ago this oscillator wrapped, in samples (0..1), or -1 (sync source) */
} osc_t;

static inline void osc_reset(osc_t *o) { o->phase = 0.0f; o->prev = 0.0f; o->wrap_d = -1.0f; }

static inline void osc_set_freq(osc_t *o, float hz, float sr) {
    float inc = hz / sr;
    o->inc = inc < 0.49f ? inc : 0.49f;
}

static inline float osc_naive(int wave, float t) {
    return wave == WAVE_SQUARE ? (t < 0.5f ? 1.0f : -1.0f) : 2.0f * t - 1.0f;
}

/* a jump of height h that happened d samples (0..1) before the current sample: PolyBLEP's residual, split between
 * the previous sample and the current one */
static inline void osc_blep(osc_t *o, float *cur, float h, float d) {
    o->prev += 0.5f * h * d * d;
    *cur -= 0.5f * h * (1.0f - d) * (1.0f - d);
}

/* the next sample. sync_d >= 0: the master wrapped sync_d samples ago, so this oscillator restarts at that moment */
static inline float osc_next(osc_t *o, int wave, float sync_d) {
    float inc = o->inc, t0 = o->phase, end = t0 + inc;   /* end: the phase now, before wrapping */
    float stop = sync_d >= 0.0f ? end - sync_d * inc : end;   /* where the phase got to before a sync reset */
    float corr = 0.0f, t;
    o->wrap_d = -1.0f;
    if (wave == WAVE_SQUARE && t0 < 0.5f && stop >= 0.5f) osc_blep(o, &corr, -2.0f, (end - 0.5f) / inc);
    if (stop >= 1.0f) {
        o->wrap_d = (end - 1.0f) / inc;
        osc_blep(o, &corr, wave == WAVE_SQUARE ? 2.0f : -2.0f, o->wrap_d);
    }
    if (sync_d >= 0.0f) {
        float at = stop >= 1.0f ? stop - 1.0f : stop;
        osc_blep(o, &corr, osc_naive(wave, 0.0f) - osc_naive(wave, at), sync_d);
        t = sync_d * inc;
    } else {
        t = end >= 1.0f ? end - 1.0f : end;
    }
    o->phase = t;
    float out = o->prev;
    o->prev = osc_naive(wave, t) + corr;
    return out;
}
