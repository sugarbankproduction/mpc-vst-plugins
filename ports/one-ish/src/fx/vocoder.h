/* ONE-ish vocoders: the 10 and 16 band vocoder effect types (Moog One manual p109), an original filter-bank design
 * (the textbook channel vocoder). The MODULATOR is the audio input (the desktop plugin's, through the engine: the
 * mixer's EXT signal, mono); the CARRIER is what the slot is given (the synth effect: the voices; a master slot: its
 * send). Per band, the same band-pass filter (two state-variable band-passes in series, 4th order, unity at the
 * centre) splits both: an envelope follower on the modulator's band sets the level of the carrier's band, and the
 * bands are summed. Centres are spaced evenly in octaves (10 bands 160 Hz .. 6.4 kHz, 16 bands 110 Hz .. 7.5 kHz),
 * neighbours crossing 6 dB down (selective: 2.5 to 5 times away from a modulator tone the carrier passes 20 dB less
 * with 10 bands, 25 dB less with 16; offline_check measures it).
 *   SIBILANCE   the modulator above the bands (a 24 dB/oct high-pass at 6 kHz) added straight to the output, so
 *               consonants ("s", "t") stay clear
 *   SENSITIVITY the modulator's gate (the manual's mic gate): below a threshold, -20 dBFS at 0% to -80 dBFS at 100%,
 *               the modulator is shut (opens in 1 ms, closes over 40 ms), so room noise and hum don't play the bands
 *   TONE        the bands' balance: at 100% all bands at full level; lower, the bands above 200 Hz x 100^TONE fall
 *               away at 6 dB per octave (darker), the sibilance with them
 *   NOISE       white noise added to the carrier, so the bands have something to play where the voices have no
 *               energy (whispered sounds, consonants); with no modulator it stays silent too
 * Silence in (no modulator, or a modulator under the gate) gives silence out: once every band has died away the bank
 * stops working and writes zeros. The output is mono (both sides); the synth effect places it at the synth pan.
 * Its state lives in the slot's union (synth_fx.h), cleared by fx.h when the slot changes type. No allocation. */
#pragma once
#include "common.h"

#define VOC_MAX 16
#define VOC_BLOCK 128       /* samples per call at most (fx.h FX_BLOCK) */
#define VOC_GAIN 8.0f        /* make-up: a band's carrier times its modulator envelope is two levels multiplied */
#define VOC_SIB_HZ 6000.0f
#define VOC_NOISE 0.5f       /* NOISE at 100%: white noise at this level into the carrier */
#define VOC_ATTACK_MS 2.0f   /* each band's envelope follower */
#define VOC_RELEASE_MS 20.0f

typedef struct {
    int nb;                        /* bands (0: not set up yet) */
    float a1[VOC_MAX], a2[VOC_MAX], a3[VOC_MAX], k[VOC_MAX];   /* each band's band-pass (TPT state-variable) */
    float hz[VOC_MAX];             /* its centre */
    float ms[VOC_MAX][2][2];       /* the modulator's filters: band, stage, (ic1, ic2) */
    float cs[VOC_MAX][2][2];       /* the carrier's */
    float env[VOC_MAX];            /* each band's modulator envelope */
    float lev, gate;               /* the modulator's peak level and the gate's gain */
    float sib[2][2];               /* the sibilance high-pass: two stages, (ic1, ic2) */
    float sa1, sa2, sa3;
    uint32_t rng;
    int quiet;                     /* every band has died away (the bank is idle until the modulator returns) */
} voc_t;

static void voc_init(voc_t *v, int bands) {
    const float lo = bands == 10 ? 160.0f : 110.0f, hi = bands == 10 ? 6400.0f : 7500.0f;
    const float r = powf(hi / lo, 1.0f / (float)(bands - 1)), x = sqrtf(r) - 1.0f / sqrtf(r);
    const float q = 1.0f / x;   /* each of the two stages 3 dB down halfway (in octaves) to the next band */
    v->nb = bands;
    for (int b = 0; b < bands; b++) {
        float f = lo * powf(r, (float)b), g = tanf(FX_PI * f / FX_SR), k = 1.0f / q;
        v->hz[b] = f;
        v->k[b] = k;
        v->a1[b] = 1.0f / (1.0f + g * (g + k));
        v->a2[b] = g * v->a1[b];
        v->a3[b] = g * v->a2[b];
    }
    float g = tanf(FX_PI * VOC_SIB_HZ / FX_SR), k = 1.41421356f;   /* Butterworth stages */
    v->sa1 = 1.0f / (1.0f + g * (g + k));
    v->sa2 = g * v->sa1;
    v->sa3 = g * v->sa2;
    v->rng = 0x2468ACEu;
    v->quiet = 1;
}

/* one TPT state-variable step on x with state s (ic1, ic2): band-pass (v1) and low-pass (v2) */
static inline float voc_bp(float *s, float a1, float a2, float a3, float k, float x) {
    float v3 = x - s[1], v1 = a1 * s[0] + a2 * v3, v2 = s[1] + a2 * s[0] + a3 * v3;
    s[0] = 2.0f * v1 - s[0];
    s[1] = 2.0f * v2 - s[1];
    return k * v1;   /* unity at the centre */
}

/* mod: the modulator (mono, n samples) or NULL for silence; inL, inR: the carrier; wl, wr: the wet output */
static void voc_process(voc_t *v, const fxs_params_t *p, const float *mod, const float *inL, const float *inR, float *wl,
                        float *wr, int n) {
    float m[VOC_BLOCK], c[VOC_BLOCK], y[VOC_BLOCK];
    if (n > VOC_BLOCK) n = VOC_BLOCK;
    if (!mod && v->quiet) {   /* nothing to play the bands with: silence, no work */
        memset(wl, 0, sizeof(float) * n);
        memset(wr, 0, sizeof(float) * n);
        return;
    }
    /* the gate (SENSITIVITY) on the modulator */
    const float thr = powf(10.0f, -(1.0f + 3.0f * fx_clamp(p->sens, 0.0f, 1.0f)));
    const float lrel = 1.0f - expf(-1.0f / (0.05f * FX_SR)), gopen = 1.0f - expf(-1.0f / (0.001f * FX_SR));
    const float gclose = 1.0f - expf(-1.0f / (0.04f * FX_SR));
    for (int i = 0; i < n; i++) {
        float x = mod ? mod[i] : 0.0f, a = fabsf(x);
        v->lev = a > v->lev ? a : v->lev + (a - v->lev) * lrel;
        float t = v->lev > thr ? 1.0f : 0.0f;
        v->gate += (t - v->gate) * (t > v->gate ? gopen : gclose);
        m[i] = x * v->gate;
        c[i] = 0.5f * (inL[i] + inR[i]) + (p->noise > 0.0f ? p->noise * VOC_NOISE * fx_white(&v->rng) : 0.0f);
        y[i] = 0.0f;
    }
    if (v->gate < 1e-9f) v->gate = 0.0f;
    if (v->lev < 1e-12f) v->lev = 0.0f;
    const float atk = 1.0f - expf(-1.0f / (VOC_ATTACK_MS * 0.001f * FX_SR));
    const float rel = 1.0f - expf(-1.0f / (VOC_RELEASE_MS * 0.001f * FX_SR));
    const float fc = 200.0f * powf(100.0f, fx_clamp(p->tone, 0.0f, 1.0f));   /* TONE: the bands above fall away */
    float peak = 0.0f;
    for (int b = 0; b < v->nb; b++) {
        const float a1 = v->a1[b], a2 = v->a2[b], a3 = v->a3[b], k = v->k[b];
        const float gain = VOC_GAIN * (p->tone >= 0.999f || v->hz[b] <= fc ? 1.0f : fc / v->hz[b]);
        float *m0 = v->ms[b][0], *m1 = v->ms[b][1], *c0 = v->cs[b][0], *c1 = v->cs[b][1], e = v->env[b];
        for (int i = 0; i < n; i++) {
            float ym = voc_bp(m1, a1, a2, a3, k, voc_bp(m0, a1, a2, a3, k, m[i] + FX_DENORM));
            float am = fabsf(ym);
            e += (am - e) * (am > e ? atk : rel);
            float yc = voc_bp(c1, a1, a2, a3, k, voc_bp(c0, a1, a2, a3, k, c[i] + FX_DENORM));
            y[i] += yc * e * gain;
        }
        if (e < 1e-9f) e = 0.0f;
        v->env[b] = e;
        if (e > peak) peak = e;
    }
    if (p->sibil > 0.0f || mod) {   /* SIBILANCE: the gated modulator above the bands (its filter runs on) */
        const float sg = p->sibil * (p->tone >= 0.999f || VOC_SIB_HZ <= fc ? 1.0f : fc / VOC_SIB_HZ);
        for (int i = 0; i < n; i++) {
            float x = m[i] + FX_DENORM;
            for (int s = 0; s < 2; s++) {   /* high-pass: x - k v1 - v2 */
                float *st = v->sib[s];
                float v3 = x - st[1], v1 = v->sa1 * st[0] + v->sa2 * v3, v2 = st[1] + v->sa2 * st[0] + v->sa3 * v3;
                st[0] = 2.0f * v1 - st[0];
                st[1] = 2.0f * v2 - st[1];
                x = x - 1.41421356f * v1 - v2;
            }
            y[i] += sg * x;
        }
    }
    v->quiet = !mod && peak == 0.0f;
    if (v->quiet) {   /* died away: start the bank clean next time (no stale ringing) */
        memset(v->ms, 0, sizeof v->ms);
        memset(v->cs, 0, sizeof v->cs);
        memset(v->sib, 0, sizeof v->sib);
    }
    for (int i = 0; i < n; i++) wl[i] = wr[i] = y[i];
}
