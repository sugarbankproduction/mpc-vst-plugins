/* Modulation matrix: MOD_SLOTS slots of source -> destination, as on the Moog One. Each slot: a SOURCE with its amount
 * (-300..+300 %), a CONTROLLER ("via", another source) with its own amount (-100..+100 %) and a TRANSFORM. Order, as
 * the manual describes it: the controller scales the source at full strength, the transform reshapes that, and the
 * source amount is applied last.
 *   controller amount c: +100% passes the source times the controller, -100% times (1 - controller), 0 leaves the
 *   source alone (the controller has no say); in between it blends: gain = 1 - |c| + c x controller (c >= 0) or
 *   1 + c x controller (c < 0).
 *   A slot with no source but a controller uses the controller as the source, scaled by the controller amount.
 *   transforms: LIMIT LOW / LIMIT HIGH clamp the signal at a level 0..100 % across the source's own range (-1..1 for a
 *   bipolar source, 0..1 for a unipolar one: so LIMIT LOW 0% and LIMIT HIGH 100% do nothing); SQUARED (x * x) and
 *   CUBED (x * x * x).
 * Evaluated at control rate; the caller turns each destination's sum into its own units. No allocation. */
#pragma once
#include <stdint.h>

#define MOD_SLOTS 16
#define MOD_SLOT_PARAMS 4   /* source, destination, amount (-300..300 %), via (a source; 0 = none) */
#define MOD_SLOT_EXTRA 3    /* via amount (-100..100 %), transform, transform amount (0..100 %): a block of their own */

/* sources: envelopes 0..1, LFOs -1..1 (or 0..1 when unipolar), velocity / mod wheel / aftertouch / expression 0..1,
 * keyboard -1..1 over C-1..C9 around middle C, a per-note random value -1..1 (Random) or 0..1 (Random Uni), the pitch
 * wheel -1..1, KB gate and sustain 0 or 1, constant 1. Option order in params.json ("Off"/"None" first); new sources
 * go at the end so saved slots keep their meaning. */
enum {
    MS_OFF, MS_FENV, MS_AENV, MS_MENV, MS_LFO1, MS_LFO2, MS_VEL, MS_WHEEL, MS_AT, MS_KEY, MS_RAND,
    MS_LFO3, MS_LFO4, MS_BEND, MS_GATE, MS_SUSTAIN, MS_EXP1, MS_EXP2, MS_RAND_UNI, MS_CONST, MS_COUNT
};

enum { MX_NONE, MX_LIMIT_LOW, MX_LIMIT_HIGH, MX_SQUARED, MX_CUBED, MX_COUNT };   /* transforms, params.json order */

/* destinations, option order in params.json; new ones at the end. At +-100%: pitch +-24 semitones, levels / wave
 * shapes / FM amount / resonance / drive / filter mix / spacing / variation +-100% of their range, cutoffs +-5 octaves,
 * amp level x0..x2, the hard-wired amounts +-100 of their knob, beat +-7 Hz, envelope and LFO times and LFO rates +-the
 * whole knob, LFO rate (st) +-24 semitones. */
enum {
    MD_OFF, MD_PITCH, MD_O1_PITCH, MD_O2_PITCH, MD_O3_PITCH, MD_O1_LEVEL, MD_O2_LEVEL, MD_O3_LEVEL, MD_NOISE_LEVEL,
    MD_RING_LEVEL, MD_O1_ANGLE, MD_O2_ANGLE, MD_O3_ANGLE, MD_O1_PW, MD_O2_PW, MD_O3_PW, MD_O1_MIX, MD_O2_MIX, MD_O3_MIX,
    MD_FM_AMT, MD_LAD_CUT, MD_LAD_RES, MD_LAD_DRIVE, MD_SVF_CUT, MD_SVF_RES, MD_FLT_MIX, MD_AMP,
    /* the hard-wired amounts */
    MD_PITCH_LFO1, MD_PITCH_ENV, MD_ANGLE_LFO3, MD_PW_LFO3,
    MD_O1_BEAT, MD_O2_BEAT, MD_O3_BEAT,
    MD_SVF_SPACE, MD_SVF_ENV, MD_SVF_LFO2, MD_SVF_FM, MD_LAD_ENV, MD_LAD_LFO2, MD_LAD_FM,
    /* per envelope (filter, amp, mod), MDE_COUNT apart: delay, attack, hold, decay, sustain, release, time scale */
    MD_FENV_DELAY, MD_ENV_END = MD_FENV_DELAY + 3 * 7 - 1,
    /* per LFO (1-4), MDL_COUNT apart: rate, rate in semitones, variation, delay, fade in, fade out */
    MD_LFO1_RATE, MD_LFO_END = MD_LFO1_RATE + 4 * 6 - 1,
    MD_COUNT
};
enum { MDE_DELAY, MDE_ATTACK, MDE_HOLD, MDE_DECAY, MDE_SUSTAIN, MDE_RELEASE, MDE_SCALE, MDE_COUNT };
enum { MDL_RATE, MDL_SEMIS, MDL_VAR, MDL_DELAY, MDL_FADE_IN, MDL_FADE_OUT, MDL_COUNT };

static inline float mod_transform(float x, int xf, float amt, int bipolar) {
    float lim = bipolar ? 2.0f * amt - 1.0f : amt;
    switch (xf) {
    case MX_LIMIT_LOW:  return x < lim ? lim : x;
    case MX_LIMIT_HIGH: return x > lim ? lim : x;
    case MX_SQUARED:    return x * x;
    case MX_CUBED:      return x * x * x;
    default:            return x;
    }
}

/* the slots that do something (a destination, and a source with an amount or a controller alone with its amount),
 * listed in `list` (MOD_SLOTS entries); returns how many. Worked out when a setting changes, so the control-rate
 * evaluation skips the empty slots. */
static inline int mod_active(const float *slots, const float *extra, uint8_t *list) {
    int n = 0;
    for (int i = 0; i < MOD_SLOTS; i++) {
        const float *sl = slots + i * MOD_SLOT_PARAMS;
        int s = (int)sl[0], d = (int)sl[1], via = (int)sl[3];
        if (d <= MD_OFF || d >= MD_COUNT) continue;
        if (s > MS_OFF && s < MS_COUNT ? sl[2] != 0.0f : via > MS_OFF && via < MS_COUNT && extra[i * MOD_SLOT_EXTRA] != 0.0f)
            list[n++] = (uint8_t)i;
    }
    return n;
}

/* slots: MOD_SLOTS x (source, destination, amount, via) as the param values; extra: MOD_SLOTS x (via amount,
 * transform, transform amount); list / nlist: the slots to evaluate (mod_active); src[MS_COUNT] with src[MS_OFF]
 * unused; bipolar: bit s set when source s is bipolar. dst[MD_COUNT] gets each destination's summed modulation, -1..1
 * per full-amount slot. */
static inline void mod_eval_list(const float *slots, const float *extra, const uint8_t *list, int nlist, const float *src,
                                 uint32_t bipolar, float *dst) {
    for (int d = 0; d < MD_COUNT; d++) dst[d] = 0.0f;
    for (int k = 0; k < nlist; k++) {
        int i = list[k];
        const float *sl = slots + i * MOD_SLOT_PARAMS, *ex = extra + i * MOD_SLOT_EXTRA;
        int s = (int)sl[0], d = (int)sl[1], via = (int)sl[3];
        int has_via = via > MS_OFF && via < MS_COUNT;
        float c = ex[0] * 0.01f, x, amt;
        if (s > MS_OFF && s < MS_COUNT) {
            x = src[s];
            if (has_via) x *= c >= 0.0f ? 1.0f - c + c * src[via] : 1.0f + c * src[via];
            amt = sl[2] * 0.01f;
        } else {   /* a controller alone acts as the source, its amount as the source's */
            s = via;
            x = src[via];
            amt = c;
        }
        dst[d] += mod_transform(x, (int)ex[1], ex[2] * 0.01f, (int)(bipolar >> s) & 1) * amt;
    }
}

/* every slot, for callers without a list */
static inline void mod_eval(const float *slots, const float *extra, const float *src, uint32_t bipolar, float *dst) {
    uint8_t list[MOD_SLOTS];
    mod_eval_list(slots, extra, list, mod_active(slots, extra, list), src, bipolar, dst);
}
