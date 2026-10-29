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
 * wheel -1..1, KB gate, sustain and sostenuto 0 or 1, constant 1, the noise EG 0..1, the ribbon -1..1 (origin
 * centre) or 0..1 (origin left), the X/Y pad's X and Y 0..1 (CC 2, CC 3) and its gate 0 or 1 (CC 18). Option order in
 * params.json ("Off"/"None" first); new sources go at the end so saved slots keep their meaning. */
enum {
    MS_OFF, MS_FENV, MS_AENV, MS_MENV, MS_LFO1, MS_LFO2, MS_VEL, MS_WHEEL, MS_AT, MS_KEY, MS_RAND,
    MS_LFO3, MS_LFO4, MS_BEND, MS_GATE, MS_SUSTAIN, MS_EXP1, MS_EXP2, MS_RAND_UNI, MS_CONST,
    MS_NOISE_EG, MS_SOSTENUTO, MS_RIBBON, MS_PAD_X, MS_PAD_Y, MS_PAD_GATE, MS_COUNT
};

enum { MX_NONE, MX_LIMIT_LOW, MX_LIMIT_HIGH, MX_SQUARED, MX_CUBED, MX_COUNT };   /* transforms, params.json order */

/* destinations, option order in params.json; new ones at the end. At +-100%: pitch +-24 semitones, levels / wave
 * shapes / FM amount / resonance / drive / filter mix / spacing / variation +-100% of their range, cutoffs +-5 octaves,
 * amp level x0..x2, the hard-wired amounts +-100 of their knob, beat +-7 Hz, envelope and LFO times and LFO rates +-the
 * whole knob, LFO rate (st) +-24 semitones, pan +-1 (the centre to a side), noise colour mix, drift and spread +-the
 * whole knob. The effect destinations (MDF_* below, from MD_FX_FIRST) move each effect control by +-100% of its range;
 * the effects run once for all voices, so the engine evaluates the slots that reach them in a pass of their own. The
 * MACRO destinations come after the effects (MD_MAC_*) but are per voice. */
/* the effect destinations, in their params.json order: the synth effect, the master sends, master effect 1 (once the
 * delay) and 2 (once the reverb), the open-source types' controls, then (2026-10-27) the synth effect's two-tap delays
 * and crossover and each master slot's further controls (F1: slot 1's dly_* params, F2: slot 2's rev_*). The engine
 * keeps them as bits of a few 64-bit words (MDF_WORDS). */
enum {
    MDF_MIX, MDF_GAIN, MDF_CTRL1, MDF_CTRL2, MDF_RATE, MDF_RATE_R, MDF_DEPTH, MDF_FB, MDF_FB_R, MDF_TIME, MDF_TIME_R,
    MDF_DTIME, MDF_DFB, MDF_TONE, MDF_SPREAD, MDF_SWEEP, MDF_NOISE, MDF_VRATE, MDF_VDEPTH, MDF_FLUTTER, MDF_WOW,
    MDF_BIAS, MDF_ROOM, MDF_DECAY, MDF_BITS, MDF_SRATE, MDF_RESO, MDF_CUT1, MDF_CUT2, MDF_CUT3, MDF_LVL1, MDF_LVL2,
    MDF_LVL3, MDF_SEND1, MDF_SEND2, MDF_SERIES, MDF_WIDTH, MDF_DLY_TIME, MDF_DLY_TIME_R, MDF_DLY_FB, MDF_DLY_TONE,
    MDF_DLY_OFFSET, MDF_REV_DECAY, MDF_REV_SIZE, MDF_REV_PRE, MDF_REV_DAMP, MDF_REV_LOWDAMP, MDF_REV_DIFF,
    MDF_REV_TONE, MDF_REV_MOD, MDF_REV_EARLY,
    /* the open-source synth effect types' controls (Rings, Clouds, Airwindows) */
    MDF_PITCH, MDF_STRUCT, MDF_POS, MDF_SIZE, MDF_DENSITY, MDF_TEXTURE, MDF_DRIVE, MDF_BUMP, MDF_LOWCUT,
    MDF_SYN_DTIME_R, MDF_SYN_DEPTH_R, MDF_SYN_OFFSET, MDF_SYN_FBL, MDF_SYN_FBR, MDF_SYN_XOVER, MDF_F1_MIX,
    MDF_F1_GAIN, MDF_F1_FB_R, MDF_F1_FBL, MDF_F1_FBR, MDF_F1_RATE, MDF_F1_RATE_R, MDF_F1_DEPTH,
    MDF_F1_DEPTH_R, MDF_F1_SPREAD, MDF_F1_NOISE, MDF_F1_SWEEP, MDF_F1_DFB, MDF_F1_VRATE, MDF_F1_VDEPTH,
    MDF_F1_FLUTTER, MDF_F1_WOW, MDF_F1_BIAS, MDF_F1_ROOM, MDF_F1_DECAY, MDF_F1_SIZE, MDF_F1_PRE,
    MDF_F1_DAMP, MDF_F1_LOWDAMP, MDF_F1_DIFF, MDF_F1_MOD, MDF_F1_EARLY, MDF_F1_DISTANCE, MDF_F1_HIFREQ,
    MDF_F1_HLEVEL, MDF_F1_MLEVEL, MDF_F1_LLEVEL, MDF_F1_LDECAY, MDF_F1_HDECAY, MDF_F1_PITCH, MDF_F1_PITCH_B,
    MDF_F1_PDECAY, MDF_F1_GRAVITY, MDF_F1_XOVER, MDF_F1_BITS, MDF_F1_SRATE, MDF_F1_RESO, MDF_F1_CUT1,
    MDF_F1_CUT2, MDF_F1_CUT3, MDF_F1_LVL1, MDF_F1_LVL2, MDF_F1_LVL3, MDF_F2_MIX, MDF_F2_GAIN,
    MDF_F2_TIME, MDF_F2_TIME_R, MDF_F2_FB, MDF_F2_FB_R, MDF_F2_FBL, MDF_F2_FBR, MDF_F2_RATE,
    MDF_F2_RATE_R, MDF_F2_DEPTH, MDF_F2_DEPTH_R, MDF_F2_OFFSET, MDF_F2_SPREAD, MDF_F2_NOISE, MDF_F2_SWEEP,
    MDF_F2_DFB, MDF_F2_VRATE, MDF_F2_VDEPTH, MDF_F2_FLUTTER, MDF_F2_WOW, MDF_F2_BIAS, MDF_F2_ROOM,
    MDF_F2_DISTANCE, MDF_F2_HIFREQ, MDF_F2_HLEVEL, MDF_F2_MLEVEL, MDF_F2_LLEVEL, MDF_F2_LDECAY, MDF_F2_HDECAY,
    MDF_F2_PITCH, MDF_F2_PITCH_B, MDF_F2_PDECAY, MDF_F2_GRAVITY, MDF_F2_XOVER, MDF_F2_BITS, MDF_F2_SRATE,
    MDF_F2_RESO, MDF_F2_CUT1, MDF_F2_CUT2, MDF_F2_CUT3, MDF_F2_LVL1, MDF_F2_LVL2, MDF_F2_LVL3,
    MDF_COUNT
};
#define MDF_WORDS ((MDF_COUNT + 63) / 64)
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
    MD_PAN, MD_NOISE_MIX, MD_DRIFT, MD_SPREAD,
    MD_FX_FIRST, MD_FX_END = MD_FX_FIRST + MDF_COUNT - 1,   /* the effects (MDF_* order) */
    /* the MACRO channel (Plaits), per voice like the ones before the effects: harmonics, timbre, morph and level
     * +-100% of their range */
    MD_MAC_HARM, MD_MAC_TIMBRE, MD_MAC_MORPH, MD_MAC_LEVEL,
    /* the mixer's EXT channel (the audio input), per voice: level +-100% of its range */
    MD_EXT_LEVEL,
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
 * per full-amount slot; only dst[0 .. nd - 1] are cleared first (the listed slots reach no destination past them). */
static inline void mod_eval_list(const float *slots, const float *extra, const uint8_t *list, int nlist, const float *src,
                                 uint32_t bipolar, float *dst, int nd) {
    for (int d = 0; d < nd; d++) dst[d] = 0.0f;
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
    mod_eval_list(slots, extra, list, mod_active(slots, extra, list), src, bipolar, dst, MD_COUNT);
}
