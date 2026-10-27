/* Band-limited dual-wave oscillator, as the Moog One manual describes its oscillators: two waves from one phase,
 * blended by MIX:
 *   - a variable-slope triangle: rises over a share `a` of the cycle, falls over the rest. TRI mode sweeps `a` from
 *     saw-like through triangle to ramp-like; SAW mode keeps it a sawtooth and lengthens only the reset (the fall),
 *     from a hard edge up to 1 ms, which darkens it
 *   - a pulse with a variable width (DC removed, so changing the width does not shift the level)
 * Anti-aliasing: PolyBLEP at every jump (the pulse's edges, a hard-sync reset) and PolyBLAMP at every corner (the
 * triangle's turns, a sync reset's change of slope). Each correction spans the sample before the event and the one
 * after, so the output is one sample late (23 us): that lets a sync reset, which can't be seen coming, be smoothed
 * like the rest. Hard sync restarts both waves. A few multiplies per sample, no allocation: safe in the audio path. */
#pragma once
#include <math.h>

enum { WAVE_TRI, WAVE_SAW, WAVE_COUNT };   /* the first wave's mode (option order in params.json) */

typedef struct {
    int mode;       /* WAVE_TRI or WAVE_SAW */
    float a;        /* the triangle's rise share of the cycle, already clamped to whole samples (osc_shape) */
    float pw;       /* pulse width 0.05..0.95 */
    float mix;      /* 0 = triangle/saw only .. 1 = pulse only */
    float up, down; /* the triangle's rising and falling slopes, 2/a and 2/(1-a) (osc_slopes: no divide per sample) */
} osc_shape_t;

static inline void osc_slopes(osc_shape_t *s) { s->up = 2.0f / s->a; s->down = 2.0f / (1.0f - s->a); }

typedef struct {
    float phase;    /* 0..1 */
    float inc;      /* frequency / sample rate, below 0.5 */
    float prev_t;   /* the previous sample of each wave (triangle, pulse), still open to the first half of a correction */
    float prev_p;
    float tri_out;  /* after osc_next: the triangle/saw wave alone in the sample returned (ring mod taps it) */
    float wrap_d;   /* after osc_next: how long ago this oscillator wrapped or was restarted, in samples (0..1), or -1
                     * (sync source) */
} osc_t;

static inline void osc_reset(osc_t *o) { o->phase = 0.0f; o->prev_t = o->prev_p = o->tri_out = 0.0f; o->wrap_d = -1.0f; }

static inline void osc_set_freq(osc_t *o, float hz, float sr) {
    float inc = hz / sr;
    o->inc = inc < 0.49f ? (inc > 0.0f ? inc : 0.0f) : 0.49f;
}

/* the shape for this pitch: TRI mode, angle 0..1 = saw-like .. triangle .. ramp-like; SAW mode, angle 0..1 = a reset of
 * about 6 us .. 1 ms. Each segment of the triangle is kept at least a sample long (shorter is a jump the PolyBLAMP
 * kernels can't round), which at the very top of the keyboard softens the sharpest shapes slightly. */
static inline osc_shape_t osc_shape(int mode, float angle, float pw, float mix, float inc) {
    osc_shape_t s;
    float a;
    if (mode == WAVE_SAW) {
        float reset_s = 6e-6f * exp2f(7.38082f * angle);   /* 6 us .. 1 ms, log (log2(1 ms / 6 us) = 7.38) */
        a = 1.0f - reset_s * inc * 44100.0f;
    } else {
        a = 1.0f - angle;
    }
    float lo = inc > 0.002f ? inc : 0.002f;
    s.mode = mode;
    s.a = a < lo ? lo : a > 1.0f - lo ? 1.0f - lo : a;
    s.pw = pw < 0.05f ? 0.05f : pw > 0.95f ? 0.95f : pw;
    s.mix = mix < 0.0f ? 0.0f : mix > 1.0f ? 1.0f : mix;
    osc_slopes(&s);
    return s;
}

static inline float osc_tri(const osc_shape_t *s, float t) {
    return t < s->a ? -1.0f + s->up * t : 1.0f - s->down * (t - s->a);
}

static inline float osc_tri_slope(const osc_shape_t *s, float t) {   /* per unit of phase */
    return t < s->a ? s->up : -s->down;
}

static inline float osc_pulse(const osc_shape_t *s, float t) {   /* +1/-1 with its mean removed */
    return (t < s->pw ? 1.0f : -1.0f) - (2.0f * s->pw - 1.0f);
}

/* a jump of height h, d samples (0..1) before the current sample: PolyBLEP's residual over the two samples
 * (prev: the wave's previous sample, cur: its correction to the current one) */
static inline void osc_blep(float *prev, float *cur, float h, float d) {
    *prev += 0.5f * h * d * d;
    *cur -= 0.5f * h * (1.0f - d) * (1.0f - d);
}

/* a change of slope ds (per sample), d samples before the current sample: PolyBLAMP's residual (the integral of
 * PolyBLEP's) over the two samples */
static inline void osc_blamp(float *prev, float *cur, float ds, float d) {
    float e = 1.0f - d;
    *prev += ds * d * d * d * (1.0f / 6.0f);
    *cur += ds * e * e * e * (1.0f / 6.0f);
}

/* osc_next for a sample with an event in it (a corner, an edge, the wrap or a sync reset), kept out of line so the
 * common sample stays short */
__attribute__((noinline)) static float osc_next_events(osc_t *o, const osc_shape_t *s, float sync_d) {
    float inc = o->inc, t0 = o->phase, end = t0 + inc;   /* end: the phase now, before wrapping */
    float stop = sync_d >= 0.0f ? end - sync_d * inc : end;   /* where the phase got to before a sync reset */
    float ct = 0.0f, cp = 0.0f, t;                          /* this sample's corrections: triangle, pulse */
    float corner = (s->up + s->down) * inc;   /* the triangle's slope change at each turn, per sample */
    o->wrap_d = -1.0f;
    if (inc > 0.0f) {
        /* every event in (t0, stop]: the triangle's peak at a, the pulse's fall at pw, the wrap at 1 (triangle's
         * bottom turn and pulse's rise), and a second peak or fall just after a wrap at high pitches */
        const float ev[5] = {s->a, s->pw, 1.0f, 1.0f + s->a, 1.0f + s->pw};
        for (int k = 0; k < 5; k++) {
            if (ev[k] <= t0 || ev[k] > stop) continue;
            float d = (end - ev[k]) / inc;
            switch (k) {
            case 0: case 3: osc_blamp(&o->prev_t, &ct, -corner, d); break;   /* the peak: rising turns to falling */
            case 1: case 4: osc_blep(&o->prev_p, &cp, -2.0f, d); break;      /* the pulse falls */
            default:
                o->wrap_d = d;
                osc_blamp(&o->prev_t, &ct, corner, d);                        /* the bottom: falling turns to rising */
                osc_blep(&o->prev_p, &cp, 2.0f, d);                           /* the pulse rises */
            }
        }
    }
    if (sync_d >= 0.0f) {   /* restart at the reset: a jump in level and a change of slope */
        float at = stop >= 1.0f ? stop - 1.0f : stop;
        osc_blep(&o->prev_t, &ct, osc_tri(s, 0.0f) - osc_tri(s, at), sync_d);
        osc_blamp(&o->prev_t, &ct, (osc_tri_slope(s, 0.0f) - osc_tri_slope(s, at)) * inc, sync_d);
        osc_blep(&o->prev_p, &cp, osc_pulse(s, 0.0f) - osc_pulse(s, at), sync_d);
        t = sync_d * inc;
        o->wrap_d = sync_d;   /* the restart is this oscillator's latest wrap: an oscillator synced to it follows */
    } else {
        t = end >= 1.0f ? end - 1.0f : end;
    }
    o->phase = t;
    float tri = o->prev_t, pul = o->prev_p;
    o->prev_t = osc_tri(s, t) + ct;
    o->prev_p = osc_pulse(s, t) + cp;
    o->tri_out = tri;
    return tri + s->mix * (pul - tri);
}

/* restart at phase 0 now (NOTE-ON RESET): the jump and change of slope land exactly on the sample not yet returned,
 * smoothed like a sync reset (d = 1: the next sample needs no correction) */
static inline void osc_restart(osc_t *o, const osc_shape_t *s) {
    float t = o->phase, ct = 0.0f, cp = 0.0f;
    osc_blep(&o->prev_t, &ct, osc_tri(s, 0.0f) - osc_tri(s, t), 1.0f);
    osc_blamp(&o->prev_t, &ct, (osc_tri_slope(s, 0.0f) - osc_tri_slope(s, t)) * o->inc, 1.0f);
    osc_blep(&o->prev_p, &cp, osc_pulse(s, 0.0f) - osc_pulse(s, t), 1.0f);
    o->phase = 0.0f;
}

/* what wrap_d will be after the next osc_next(o, ..., sync_d), without running it: so an oscillator can sync to one
 * that runs after it */
static inline float osc_wrap_ahead(const osc_t *o, float sync_d) {
    float end = o->phase + o->inc;
    return sync_d >= 0.0f ? sync_d : end >= 1.0f && o->inc > 0.0f ? (end - 1.0f) / o->inc : -1.0f;
}

/* the next sample, the two waves blended by the shape's mix. sync_d >= 0: the master wrapped sync_d samples ago, so
 * this oscillator restarts at that moment */
static inline float osc_next(osc_t *o, const osc_shape_t *s, float sync_d) {
    float t0 = o->phase, end = t0 + o->inc, a = s->a, pw = s->pw;
    /* most samples hold no event: no sync, the phase stays below the wrap and crosses neither the peak nor the fall */
    if (!(sync_d < 0.0f && end < 1.0f && (t0 >= a || end < a) && (t0 >= pw || end < pw))) return osc_next_events(o, s, sync_d);
    float tri = o->prev_t, pul = o->prev_p;
    o->wrap_d = -1.0f;
    o->phase = end;
    o->prev_t = end < a ? -1.0f + s->up * end : 1.0f - s->down * (end - a);
    o->prev_p = (end < pw ? 1.0f : -1.0f) - (2.0f * pw - 1.0f);
    o->tri_out = tri;
    return tri + s->mix * (pul - tri);
}
