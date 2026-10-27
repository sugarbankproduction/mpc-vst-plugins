/* Modulation matrix: MOD_SLOTS slots of source -> destination x amount, each optionally scaled by a second "via"
 * source (e.g. LFO 1 -> pitch via the mod wheel: the wheel sets the vibrato depth). Evaluated at control rate; the
 * caller turns each destination's sum into its own units. No allocation. */
#pragma once

#define MOD_SLOTS 8
#define MOD_SLOT_PARAMS 4   /* source, destination, amount (-100..100 %), via (a source; 0 = none) */

/* sources: envelopes 0..1, LFOs -1..1, velocity / mod wheel / aftertouch 0..1, keyboard -1..1 over C-1..C9 around
 * middle C, a per-note random value -1..1. Option order in params.json ("Off"/"None" first). */
enum { MS_OFF, MS_FENV, MS_AENV, MS_MENV, MS_LFO1, MS_LFO2, MS_VEL, MS_WHEEL, MS_AT, MS_KEY, MS_RAND, MS_COUNT };

/* destinations, option order in params.json. At +-100%: pitch +-24 semitones, levels / resonance / drive / filter mix
 * +-100% of their range, cutoffs +-5 octaves, amp level x0..x2. */
enum {
    MD_OFF, MD_PITCH, MD_O1_PITCH, MD_O2_PITCH, MD_O3_PITCH, MD_O1_LEVEL, MD_O2_LEVEL, MD_O3_LEVEL, MD_NOISE_LEVEL,
    MD_LAD_CUT, MD_LAD_RES, MD_LAD_DRIVE, MD_SVF_CUT, MD_SVF_RES, MD_FLT_MIX, MD_AMP, MD_COUNT
};

/* slots: MOD_SLOTS x (source, destination, amount, via) as the param values; src[MS_COUNT] with src[MS_OFF] unused.
 * dst[MD_COUNT] gets each destination's summed modulation, -1..1 per full-amount slot. */
static inline void mod_eval(const float *slots, const float *src, float *dst) {
    for (int d = 0; d < MD_COUNT; d++) dst[d] = 0.0f;
    for (int i = 0; i < MOD_SLOTS; i++) {
        const float *sl = slots + i * MOD_SLOT_PARAMS;
        int s = (int)sl[0], d = (int)sl[1], via = (int)sl[3];
        if (s <= MS_OFF || s >= MS_COUNT || d <= MD_OFF || d >= MD_COUNT || sl[2] == 0.0f) continue;
        float g = via > MS_OFF && via < MS_COUNT ? src[via] : 1.0f;
        dst[d] += src[s] * g * sl[2] * 0.01f;
    }
}
