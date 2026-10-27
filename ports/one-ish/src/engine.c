/* ONE-ish engine. Per voice: three band-limited dual-wave oscillators (src/osc.h: a triangle/saw with a wave angle
 * blended with a pulse of any width; octave, frequency +-7 semitones, beat frequency +-7 Hz; 2 and 3 can hard-sync
 * to 1), linear FM (1 to 2, 1 to 3 or 3 to 1), a ring modulator (1 x 2 or 2 x 3, from the triangle/saw waves) and
 * white noise, each sent to the SVF, the ladder, both or neither by its own two buttons; a state-variable filter
 * (LP/BP/HP/notch) and a ladder filter (cutoff, resonance, drive, LP/HP, 24-6 dB) in parallel or in series (SVF into
 * ladder) with a mix between them; three DAHDSR envelopes (filter, amp, mod: src/env.h, with stage curves, loop,
 * latch, tempo sync, time scale, velocity amount, multi trig and reset). Shared: two LFOs (free or tempo-synced).
 * Hard-wired modulation as on the Moog One panel: filter envelope, LFO 2 and keyboard tracking to each cutoff,
 * LFO 1 and the mod envelope to pitch. An 8-slot modulation matrix (source x amount, scaled by a via source) over
 * pitch, levels, both filters and amp; mod wheel, aftertouch, pitch bend and sustain pedal. Up to 8 voices: a new
 * note takes an idle voice, else the quietest releasing one, else the oldest held one; the same key replays its own
 * voice. Mono (1 voice) plays legato with last-note priority. Glide (portamento): each voice slides from its own
 * last pitch at a constant rate (LCR), in a constant time (LCT) or exponentially (EXP); optionally only on legato
 * notes, paused while the key is up (gated), or in semitone steps (glissando). Unison stacks 2-8 voices on each note
 * (a stack is allocated and stolen as one), detuned unevenly around the centre; pitch variance gives each new note a
 * small random pitch offset. After the voices: a modulation effect (chorus,
 * flanger or phaser), a delay (free or tempo-synced, ping-pong) and a plate reverb, in stereo (src/fx.h).
 *
 * Audio runs per sample; modulation (envelopes 1 and 3, LFOs, pitch, cutoffs) every CR samples, with levels and the
 * filter mix ramped across each step so no knob or mod move steps audibly.
 * Implements mpc_engine() (wrapper/engine.h): 44.1 kHz, interleaved int16 stereo, up to 128 frames per render().
 * Everything is allocated in create(); render() and midi() never allocate, lock or touch files. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "engine.h"
#include "osc.h"
#include "env.h"
#include "ladder.h"
#include "svf.h"
#include "lfo.h"
#include "modmatrix.h"
#include "fx.h"

#define SR 44100.0f
#define CR 16              /* control-rate step in samples: modulation is worked out every 0.36 ms */
#define MAX_HELD 16
#define MAX_VOICES 8
#define NOSC 3
#define NSRC 5             /* the mixer's channels: oscillators 1-3, noise, ring mod */
#define NLFO 2
#define HEADROOM 0.5f      /* one full-scale saw at full velocity and volume peaks at -6 dBFS */
#define STATE_MAX 8192     /* the wrapper's chunk size (wrapper/vst2_wrap.c) */
#define NOISE_FLOOR 1e-5f  /* -100 dB of noise into the filters, as in a circuit: starts self-oscillation, avoids denormals */
#define CUT_MIN_HZ 20.0f   /* cutoff knobs: 20 Hz .. 20 kHz on a log scale (0..100) */
#define CUT_RANGE 1000.0f
#define ENV_RANGE 10000.0f /* envelope time knobs: 1 ms .. 10 s on a log scale (0..100) */
#define LFO_MIN_HZ 0.05f   /* LFO rate knobs: 0.05 .. 50 Hz on a log scale (0..100) */
#define LFO_RANGE 1000.0f
#define ENV_CUT_OCT 5.0f   /* filter envelope amount at +-100%: +-5 octaves of cutoff */
#define LFO_CUT_OCT 3.0f   /* LFO 2 amount at +-100%: +-3 octaves */
#define LFO_PITCH_ST 12.0f /* LFO 1 pitch amount at +-100%: +-12 semitones */
#define ENV_PITCH_ST 24.0f /* mod envelope pitch amount at +-100%: +-24 semitones */
#define GLIDE_MS 10.0f     /* cutoff knob moves are smoothed over about this long */
#define MOD_PITCH_ST 24.0f /* the matrix's pitch destinations at +-100%: +-24 semitones */
#define MOD_CUT_OCT 5.0f   /* its cutoff destinations at +-100%: +-5 octaves */
#define FM_DEPTH 2.0f      /* linear FM at 100%: the carrier's frequency swings by up to 2x its own, times the modulator */
#define MODFX_MIN_HZ 0.05f /* mod effect rate knob: 0.05 .. 10 Hz on a log scale */
#define MODFX_RANGE 200.0f
#define DLY_MIN_MS 10.0f   /* delay time knob: 10 ms .. 2 s on a log scale */
#define DLY_RANGE 200.0f
#define UNI_DETUNE_ST 0.5f /* unison detune at 100%: the outermost stacked voices sit +-50 cents from the note */
#define GLIDE_EXP_K 3.0f   /* EXP glide: the remaining distance shrinks by e every third of the glide time (95% there) */

/* engine-side slots, matched to params.json by key (so the VST order there can grow freely). Per oscillator the
 * slots run WAVE .. LAD from P_O1_WAVE + OSC_STRIDE * n (osc 1's SYNC slot is unused);
 * the envelopes run A, D, S, R from P_ENV_A + ENV_STRIDE * n in ENV_FILTER, ENV_AMP, ENV_MOD order; the LFOs run
 * RATE, WAVE, SYNC from P_LFO1_RATE + LFO_STRIDE * n; the source routes and levels in source order. */
enum {
    P_O1_WAVE, P_O1_OCT, P_O1_FREQ, P_O1_BEAT, P_O1_ANGLE, P_O1_PW, P_O1_MIX, P_O1_LEVEL, P_O1_SYNC, P_O1_SVF, P_O1_LAD,
    P_O2_WAVE, P_O2_OCT, P_O2_FREQ, P_O2_BEAT, P_O2_ANGLE, P_O2_PW, P_O2_MIX, P_O2_LEVEL, P_O2_SYNC, P_O2_SVF, P_O2_LAD,
    P_O3_WAVE, P_O3_OCT, P_O3_FREQ, P_O3_BEAT, P_O3_ANGLE, P_O3_PW, P_O3_MIX, P_O3_LEVEL, P_O3_SYNC, P_O3_SVF, P_O3_LAD,
    P_NOISE_LEVEL, P_NOISE_SVF, P_NOISE_LAD, P_RING_LEVEL, P_RING_SVF, P_RING_LAD, P_RING_SRC, P_FM_AMT, P_FM_ROUTE,
    P_FLT_CUT, P_FLT_RES, P_FLT_DRIVE, P_FLT_MODE, P_FLT_SLOPE, P_SVF_CUT, P_SVF_RES, P_SVF_MODE, P_FLT_ROUTE, P_FLT_MIX,
    P_ENV_A, P_ENV_D, P_ENV_S, P_ENV_R,                 /* filter envelope */
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,                 /* amp envelope */
    P_MENV_A, P_MENV_D, P_MENV_S, P_MENV_R,             /* mod envelope */
    P_LAD_ENV, P_LAD_LFO, P_LAD_KBD, P_SVF_ENV, P_SVF_LFO, P_SVF_KBD, P_PITCH_LFO, P_PITCH_ENV,
    P_LFO1_RATE, P_LFO1_WAVE, P_LFO1_SYNC, P_LFO2_RATE, P_LFO2_WAVE, P_LFO2_SYNC,
    P_BEND_RANGE, P_VOICES, P_MOD_BASE,                           /* then MOD_SLOTS x (source, destination, amount, via) */
    P_VOLUME = P_MOD_BASE + MOD_SLOTS * MOD_SLOT_PARAMS,
    P_MODFX_TYPE, P_MODFX_RATE, P_MODFX_DEPTH, P_MODFX_FB, P_MODFX_MIX,
    P_DLY_TIME, P_DLY_SYNC, P_DLY_FB, P_DLY_TONE, P_DLY_PP, P_DLY_MIX,
    P_REV_DECAY, P_REV_DAMP, P_REV_PRE, P_REV_MIX,
    P_GLIDE, P_GLIDE_TIME, P_GLIDE_TYPE, P_GLIDE_LEGATO, P_GLIDE_GATED, P_GLISSANDO,
    P_UNISON, P_UNI_COUNT, P_UNI_DETUNE, P_PITCH_VAR,
    /* each envelope's extras, in ENV_FILTER, ENV_AMP, ENV_MOD order, EX_STRIDE apart (X_* below) */
    P_FENV_DELAY, P_FENV_HOLD, P_FENV_ACURVE, P_FENV_DCURVE, P_FENV_RCURVE, P_FENV_LOOP, P_FENV_LATCH, P_FENV_SYNC,
    P_FENV_MULTI, P_FENV_RESET, P_FENV_SCALE, P_FENV_VEL,
    P_AMP_DELAY, P_AMP_HOLD, P_AMP_ACURVE, P_AMP_DCURVE, P_AMP_RCURVE, P_AMP_LOOP, P_AMP_LATCH, P_AMP_SYNC,
    P_AMP_MULTI, P_AMP_RESET, P_AMP_SCALE, P_AMP_VEL,
    P_MENV_DELAY, P_MENV_HOLD, P_MENV_ACURVE, P_MENV_DCURVE, P_MENV_RCURVE, P_MENV_LOOP, P_MENV_LATCH, P_MENV_SYNC,
    P_MENV_MULTI, P_MENV_RESET, P_MENV_SCALE, P_MENV_VEL,
    P_COUNT
};
enum { GLIDE_LCR, GLIDE_LCT, GLIDE_EXP };       /* "glide_type" options */
enum { OSC_STRIDE = P_O2_WAVE - P_O1_WAVE, O_WAVE = 0, O_OCT, O_FREQ, O_BEAT, O_ANGLE, O_PW, O_MIX };
/* each mixer channel's level and filter buttons, in channel order (osc 1-3, noise, ring mod: the matrix's order) */
static const int LEVEL_SLOT[NSRC] = {P_O1_LEVEL, P_O2_LEVEL, P_O3_LEVEL, P_NOISE_LEVEL, P_RING_LEVEL};
static const int SVF_SLOT[NSRC] = {P_O1_SVF, P_O2_SVF, P_O3_SVF, P_NOISE_SVF, P_RING_SVF};
static const int LAD_SLOT[NSRC] = {P_O1_LAD, P_O2_LAD, P_O3_LAD, P_NOISE_LAD, P_RING_LAD};
enum { NOISE = 3, RING = 4 };                   /* the noise and ring modulator channels */
enum { RING_1X2, RING_2X3 };                    /* "ring_src" options */
enum { FM_1TO2, FM_1TO3, FM_3TO1 };             /* "fm_route" options */
enum { ENV_FILTER, ENV_AMP, ENV_MOD, NENV, ENV_STRIDE = P_AMP_A - P_ENV_A };
enum { LFO_STRIDE = P_LFO2_RATE - P_LFO1_RATE };
enum { X_DELAY, X_HOLD, X_ACURVE, X_DCURVE, X_RCURVE, X_LOOP, X_LATCH, X_SYNC, X_MULTI, X_RESET, X_SCALE, X_VEL,
       EX_STRIDE = P_AMP_DELAY - P_FENV_DELAY };
static const char *const KEYS[P_COUNT] = {
#define OSC_KEYS(n) "o" #n "_wave", "o" #n "_oct", "o" #n "_freq", "o" #n "_beat", "o" #n "_angle", "o" #n "_pw", \
                    "o" #n "_mix", "o" #n "_level"
    OSC_KEYS(1), "", "o1_svf", "o1_lad",
    OSC_KEYS(2), "o2_sync", "o2_svf", "o2_lad",
    OSC_KEYS(3), "o3_sync", "o3_svf", "o3_lad",
#undef OSC_KEYS
    "noise_level", "noise_svf", "noise_lad", "ring_level", "ring_svf", "ring_lad", "ring_src", "fm_amt", "fm_route",
    "flt_cut", "flt_res", "flt_drive", "flt_mode", "flt_slope", "svf_cut", "svf_res", "svf_mode", "flt_route", "flt_mix",
    "fenv_a", "fenv_d", "fenv_s", "fenv_r",
    "amp_a", "amp_d", "amp_s", "amp_r",
    "menv_a", "menv_d", "menv_s", "menv_r",
    "lad_env", "lad_lfo", "lad_kbd", "svf_env", "svf_lfo", "svf_kbd", "pitch_lfo", "pitch_env",
    "lfo1_rate", "lfo1_wave", "lfo1_sync", "lfo2_rate", "lfo2_wave", "lfo2_sync",
    "bend_range", "voices",
#define SLOT_KEYS(n) [P_MOD_BASE + 4 * ((n) - 1)] = "mod" #n "_src", [P_MOD_BASE + 4 * ((n) - 1) + 1] = "mod" #n "_dst", \
                     [P_MOD_BASE + 4 * ((n) - 1) + 2] = "mod" #n "_amt", [P_MOD_BASE + 4 * ((n) - 1) + 3] = "mod" #n "_via"
    SLOT_KEYS(1), SLOT_KEYS(2), SLOT_KEYS(3), SLOT_KEYS(4), SLOT_KEYS(5), SLOT_KEYS(6), SLOT_KEYS(7), SLOT_KEYS(8),
#undef SLOT_KEYS
    [P_VOLUME] = "volume",
    "modfx_type", "modfx_rate", "modfx_depth", "modfx_fb", "modfx_mix",
    "dly_time", "dly_sync", "dly_fb", "dly_tone", "dly_pp", "dly_mix",
    "rev_decay", "rev_damp", "rev_pre", "rev_mix",
    "glide", "glide_time", "glide_type", "glide_legato", "glide_gated", "glissando",
    "unison", "uni_count", "uni_detune", "pitch_var",
#define ENV_EXTRA_KEYS(e) e "_delay", e "_hold", e "_acurve", e "_dcurve", e "_rcurve", e "_loop", e "_latch", e "_sync", \
                          e "_multi", e "_reset", e "_scale", e "_vel"
    ENV_EXTRA_KEYS("fenv"), ENV_EXTRA_KEYS("amp"), ENV_EXTRA_KEYS("menv")
#undef ENV_EXTRA_KEYS
};
#define OCT_CENTRE 2   /* option index of 8' (no octave shift) in the "oct" lists */
enum { FLT_PARALLEL, FLT_SERIES };

/* tempo-synced LFO rates (the rate knob picks one): beats per LFO cycle */
static const struct { const char *name; float beats; } SYNC_DIV[] = {
    {"4 bars", 16}, {"2 bars", 8}, {"1 bar", 4}, {"1/2", 2}, {"1/4", 1}, {"1/4T", 2.0f / 3}, {"1/8", 0.5f},
    {"1/8T", 1.0f / 3}, {"1/16", 0.25f}, {"1/16T", 1.0f / 6}, {"1/32", 0.125f},
};
#define NDIV ((int)(sizeof SYNC_DIV / sizeof SYNC_DIV[0]))

typedef struct {
    osc_t osc[NOSC];
    osc_shape_t shape[NOSC];     /* each oscillator's wave shape at the last control step (ramp start) */
    osc_shape_t tgt_shape[NOSC]; /* and at this one (ramp end), modulation included */
    float inc0[NOSC];        /* each oscillator's frequency / sample rate before FM */
    float fm, tgt_fm;        /* the FM depth (ramp start, end) */
    float fm_prev;           /* oscillator 3's last sample, for FM 3 > 1 */
    ladder_t lad;
    svf_t svf;
    env_t env[NENV];
    uint32_t noise;          /* xorshift state */
    int note;                /* the sounding note (glide's target), -1 before the first */
    float pitch;             /* the glided pitch in semitones (MIDI note numbers), equal to note once a glide ends */
    float glide_span;        /* how far this glide goes, in semitones (LCT covers it in the glide time) */
    int gliding;             /* the pitch is still on its way to note */
    float uni_pos;           /* place in the unison stack, -1..1 (0: the centre or no unison), times the detune */
    float uni_gain;          /* 1 / sqrt(stack size): a detuned stack about as loud as one voice */
    float pvar;              /* this note's pitch variance offset, semitones */
    float note_hz, velocity;
    float gain;              /* smoothed towards velocity x volume, so neither jumps the level mid-note */
    float cut, svf_cut;      /* the cutoff knobs (0..100) as this voice's filters follow them, smoothed */
    float rand;              /* this note's random value (-1..1), a matrix source */
    float to_lad[NSRC], to_svf[NSRC], mix_lad;   /* last control step's levels and filter mix (ramp start) */
    float tgt_lad[NSRC], tgt_svf[NSRC], tgt_mix; /* this control step's (ramp end), modulation included */
    float amp_mod;           /* the matrix's amp level factor (0..2) */
    int gate;                /* key down, or released while the sustain pedal is down */
    int sustained;           /* released while the pedal is down: let go when it comes up */
    unsigned age;            /* when its note started, for stealing the oldest */
} voice_t;

typedef struct {
    float val[P_COUNT];      /* each value in its own units: option index, semitones, cents, % or a 0..100 knob */
    float ratio[NOSC];       /* each oscillator's pitch against the played note (octave and frequency) */
    voice_t voice[MAX_VOICES];
    unsigned age;            /* counts note starts */
    int voices;              /* the voices in use last applied (a change lets go of voices past it) */
    float glide_frac;        /* one control step as a fraction of the glide time (>= 1: no glide) */
    float glide_exp;         /* EXP glide: the fraction of the remaining distance covered per control step */
    fx_t fx;                 /* effect buffers (about 1.3 MB) */
    int sustain;             /* the pedal (CC 64) is down */
    lfo_t lfo[NLFO];
    float lfo_val[NLFO];     /* this control step's LFO outputs */
    float bpm;               /* the host's tempo, for synced LFOs, delay and envelopes */
    env_cfg_t ecfg[NENV];    /* each envelope's settings (times, curves, loop, latch...), shared by the voices */
    float wheel, aftertouch, bend;   /* 0..1, 0..1, -1..1 (times the bend range) */
    float gain_coef, glide;  /* per sample / per control step */
    int phase;               /* samples into the current control step */
    int held[MAX_HELD];      /* keys down, oldest first (mono: releasing the newest falls back to the one before) */
    int nheld;
    char state_buf[STATE_MAX];   /* set_state's working copy (allocated with the instance, not per call) */
} one_t;

static int slot(const char *key) {
    for (int i = 0; i < P_COUNT; i++)
        if (KEYS[i][0] && !strcmp(KEYS[i], key)) return i;
    return -1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline float white(uint32_t *x) {   /* xorshift32: -1..1 */
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return (int32_t)*x * (1.0f / 2147483648.0f);
}

/* store a value in its param's range; NaN, infinities and unknown keys leave the old value (a damaged project) */
static void store(one_t *s, const char *key, const char *text) {
    int k = slot(key);
    float v = (float)atof(text);
    if (k < 0 || !isfinite(v)) return;
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        if (strcmp(p->key, key)) continue;
        s->val[k] = p->nopts ? clampf(roundf(v), 0, p->nopts - 1) : clampf(v, p->min, p->max);
        return;
    }
}

/* knob (0..100) to units */
static float cut_hz(float knob) { return CUT_MIN_HZ * powf(CUT_RANGE, knob * 0.01f); }
static float env_ms(float knob) { return powf(ENV_RANGE, knob * 0.01f); }
static float glide_ms(float knob) { return knob > 0.0f ? env_ms(knob) : 0.0f; }   /* 0, then 1 ms .. 10 s */
static int sync_div(float knob) { return (int)clampf(roundf(knob * 0.01f * (NDIV - 1)), 0, NDIV - 1); }

static float modfx_hz(float knob) { return MODFX_MIN_HZ * powf(MODFX_RANGE, knob * 0.01f); }

static float dly_ms(const one_t *s) {
    if (s->val[P_DLY_SYNC] > 0.5f) return 60000.0f / s->bpm * SYNC_DIV[sync_div(s->val[P_DLY_TIME])].beats;
    return DLY_MIN_MS * powf(DLY_RANGE, s->val[P_DLY_TIME] * 0.01f);
}

/* envelope times. A synced time knob picks a tempo division, longest on the right; free times are scaled by the
 * envelope's time scale (10% .. 1000%, a log knob with 100% in the middle). Delay and hold at 0 are off. */
static float env_scale(float knob) { return powf(10.0f, (knob - 50.0f) * 0.02f); }
static int env_div(float knob) { return NDIV - 1 - sync_div(knob); }
static float env_time(const one_t *s, int n, float knob, int zero_off) {
    const float *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
    if (zero_off && knob <= 0.0f) return 0.0f;
    if (x[X_SYNC] > 0.5f) return 60000.0f / s->bpm * SYNC_DIV[env_div(knob)].beats;
    return env_ms(knob) * env_scale(x[X_SCALE]);
}

/* the envelope's output at this velocity (0..1): VEL MOD AMT +100% scales it by the velocity, -100% by its inverse,
 * 0 leaves it at full */
static float env_vel(const one_t *s, int n, float velocity) {
    float a = s->val[P_FENV_VEL + EX_STRIDE * n] * 0.01f;
    return a >= 0.0f ? 1.0f - a + a * velocity : 1.0f + a * velocity;
}

/* the envelopes' settings, which every voice's envelopes read */
static void apply_envs(one_t *s) {
    for (int n = 0; n < NENV; n++) {
        const float *e = &s->val[P_ENV_A + ENV_STRIDE * n], *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
        env_cfg_t *c = &s->ecfg[n];
        const float ms[5] = {env_time(s, n, x[X_DELAY], 1), env_time(s, n, e[0], 0), env_time(s, n, x[X_HOLD], 1),
                             env_time(s, n, e[1], 0), env_time(s, n, e[3], 0)};
        env_config(c, ms, e[2] * 0.01f, x[X_ACURVE] * 0.01f, x[X_DCURVE] * 0.01f, x[X_RCURVE] * 0.01f, SR);
        c->loop = x[X_LOOP] > 0.5f;
        c->latch = x[X_LATCH] > 0.5f;
        c->multi = x[X_MULTI] > 0.5f;
        c->reset = x[X_RESET] > 0.5f;
    }
}

static float lfo_hz(const one_t *s, int n) {
    const float *l = &s->val[P_LFO1_RATE + LFO_STRIDE * n];
    if (l[2] > 0.5f) return s->bpm / 60.0f / SYNC_DIV[sync_div(l[0])].beats;
    return LFO_MIN_HZ * powf(LFO_RANGE, l[0] * 0.01f);
}

/* recompute what the audio path reads from the parameter values */
static void apply(one_t *s) {
    for (int n = 0; n < NOSC; n++) {
        const float *o = &s->val[P_O1_WAVE + OSC_STRIDE * n];
        s->ratio[n] = powf(2.0f, (o[O_OCT] - OCT_CENTRE) + o[O_FREQ] / 12.0f);
    }
    apply_envs(s);
    float t = glide_ms(s->val[P_GLIDE_TIME]) * 0.001f;
    s->glide_frac = t > 0.0f ? (float)CR / (SR * t) : 2.0f;
    s->glide_exp = 1.0f - expf(-GLIDE_EXP_K * fminf(s->glide_frac, 1.0f));
}

static const int VOICE_COUNTS[] = {1, 2, 4, 6, 8};   /* the "voices" options */
static int voice_count(const one_t *s) { return VOICE_COUNTS[(int)clampf(s->val[P_VOICES], 0, 4)]; }
static int is_mono(const one_t *s) { return voice_count(s) == 1; }

/* voices stacked on each note: 1 without unison; the unison count, at most the voice count except in mono (mono +
 * unison stacks the unison count on its one note) */
static int stack_size(const one_t *s) {
    if (s->val[P_UNISON] < 0.5f) return 1;
    int n = (int)clampf(s->val[P_UNI_COUNT], 0, MAX_VOICES - 2) + 2, pool = voice_count(s);
    return is_mono(s) || n < pool ? n : pool;
}

/* the voices the allocator uses: whole stacks only (6 voices with stacks of 4 use 4) */
static int voices_used(const one_t *s) {
    int stack = stack_size(s);
    return is_mono(s) ? stack : voice_count(s) / stack * stack;
}

static float gain_target(const one_t *s, const voice_t *v) {
    return env_vel(s, ENV_AMP, v->velocity) * s->val[P_VOLUME] * 0.01f * HEADROOM * v->amp_mod * v->uni_gain;
}

/* the pitch the voice plays (semitones): the glided pitch, in whole semitones with glissando */
static float voice_key(const one_t *s, const voice_t *v) {
    return s->val[P_GLISSANDO] > 0.5f ? roundf(v->pitch) : v->pitch;
}

/* the voice's base frequency: its key, pitch variance and place in the unison stack (detune follows the knob live) */
static void update_note_hz(const one_t *s, voice_t *v) {
    float st = voice_key(s, v) + v->pvar + v->uni_pos * s->val[P_UNI_DETUNE] * 0.01f * UNI_DETUNE_ST;
    v->note_hz = 440.0f * exp2f((st - 69.0f) / 12.0f);
}

/* one control step of glide towards the voice's note: constant rate (LCR, the glide time per octave), constant time
 * (LCT, every glide takes the glide time) or exponential (EXP); gated glide holds still while the key is up */
static void glide_step(const one_t *s, voice_t *v) {
    float d = (float)v->note - v->pitch;
    if (!v->gliding || d == 0.0f || s->val[P_GLIDE] < 0.5f || s->glide_frac >= 1.0f) {
        v->pitch = (float)v->note;
        v->gliding = 0;
        return;
    }
    if (s->val[P_GLIDE_GATED] > 0.5f && !v->gate) return;
    int type = (int)s->val[P_GLIDE_TYPE];
    if (type == GLIDE_EXP) {
        v->pitch += d * s->glide_exp;
        if (fabsf((float)v->note - v->pitch) < 0.005f) { v->pitch = (float)v->note; v->gliding = 0; }   /* half a cent */
        return;
    }
    float step = (type == GLIDE_LCR ? 12.0f : v->glide_span) * s->glide_frac;
    if (fabsf(d) <= step) { v->pitch = (float)v->note; v->gliding = 0; }
    else v->pitch += copysignf(step, d);
}

/* each mixer channel's level into each filter (its two buttons), the filter mix, the oscillators' wave shapes and the
 * FM depth, with the matrix's modulation dm (MD_COUNT values) added */
static void set_targets(const one_t *s, voice_t *v, const float *dm) {
    const float *p = s->val;
    for (int k = 0; k < NSRC; k++) {
        float l = clampf(p[LEVEL_SLOT[k]] * 0.01f + dm[MD_O1_LEVEL + k], 0.0f, 1.0f);
        v->tgt_lad[k] = p[LAD_SLOT[k]] > 0.5f ? l : 0.0f;
        v->tgt_svf[k] = p[SVF_SLOT[k]] > 0.5f ? l : 0.0f;
    }
    for (int n = 0; n < NOSC; n++) {
        const float *o = &p[P_O1_WAVE + OSC_STRIDE * n];
        v->tgt_shape[n] = osc_shape((int)o[O_WAVE], clampf(o[O_ANGLE] * 0.01f + dm[MD_O1_ANGLE + n], 0.0f, 1.0f),
                                    o[O_PW] * 0.01f + dm[MD_O1_PW + n] * 0.9f, o[O_MIX] * 0.01f + dm[MD_O1_MIX + n],
                                    v->inc0[n]);
    }
    v->tgt_fm = clampf(p[P_FM_AMT] * 0.01f + dm[MD_FM_AMT], 0.0f, 1.0f) * FM_DEPTH;
    v->tgt_mix = clampf(p[P_FLT_MIX] * 0.01f + dm[MD_FLT_MIX], 0.0f, 1.0f);
    v->amp_mod = clampf(1.0f + dm[MD_AMP], 0.0f, 2.0f);
}

/* each oscillator's frequency: the note times its octave and frequency settings and the pitch modulation (semitones,
 * shared and per oscillator), plus its beat frequency in Hz */
static void set_pitch(const one_t *s, voice_t *v, float semis, const float *osc_semis) {
    for (int n = 0; n < NOSC; n++) {
        float hz = v->note_hz * s->ratio[n] * exp2f((semis + osc_semis[n]) / 12.0f) + s->val[P_O1_BEAT + OSC_STRIDE * n];
        osc_set_freq(&v->osc[n], hz, SR);
        v->inc0[n] = v->osc[n].inc;
    }
}

static void load_defaults(one_t *s) {
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        int k = slot(p->key);
        if (k < 0) continue;
        s->val[k] = p->nopts ? roundf(p->def * (p->nopts - 1)) : p->min + p->def * (p->max - p->min);
    }
}

static void *create(const char *data_dir) {
    (void)data_dir;
    one_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    load_defaults(s);
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s->voice[i];
        for (int n = 0; n < NOSC; n++) osc_reset(&v->osc[n]);
        v->noise = 0x9e3779b9u + 0x6d2b79f5u * (uint32_t)i;   /* each voice its own noise */
        v->note = -1;
        v->amp_mod = 1.0f;
        v->uni_gain = 1.0f;
        for (int n = 0; n < NENV; n++) v->env[n].cfg = &s->ecfg[n];
        v->cut = s->val[P_FLT_CUT];
        v->svf_cut = s->val[P_SVF_CUT];
    }
    for (int n = 0; n < NLFO; n++) lfo_init(&s->lfo[n], 0x2545f491u * (n + 1));
    s->bpm = 120.0f;
    s->voices = voices_used(s);
    s->gain_coef = env_coef(5.0f, SR);
    s->glide = 1.0f - expf(-(float)CR / (GLIDE_MS * 0.001f * SR));
    apply(s);
    return s;
}

static void destroy(void *inst) { free(inst); }

/* legato: another key is held as this note starts. The voice glides from its own last pitch when glide is on, it has
 * played before, and (with legato glide) the note is legato; else it jumps straight to the note. */
static void start_note(one_t *s, voice_t *v, int note, int legato) {
    static const float no_mod[MD_COUNT], no_semis[NOSC];
    int glide = s->val[P_GLIDE] > 0.5f && v->note >= 0 && (legato || s->val[P_GLIDE_LEGATO] < 0.5f);
    if (glide) v->glide_span = fabsf((float)note - v->pitch);
    else v->pitch = (float)note;
    v->gliding = glide && v->glide_span > 0.0f;
    v->note = note;
    update_note_hz(s, v);
    set_pitch(s, v, 0.0f, no_semis);   /* pitch at once; the next control step adds the modulation */
    if (v->env[ENV_AMP].stage == ENV_IDLE) {   /* from silence every note starts the same way */
        for (int n = 0; n < NOSC; n++) osc_reset(&v->osc[n]);
        ladder_reset(&v->lad);
        svf_reset(&v->svf);
        set_targets(s, v, no_mod);   /* unmodulated, until the next control step adds the matrix */
        v->mix_lad = v->tgt_mix;
        memcpy(v->shape, v->tgt_shape, sizeof v->shape);
        v->fm = v->tgt_fm;
        v->fm_prev = 0.0f;
        for (int n = 0; n < NENV; n++) { v->env[n].level = 0.0f; v->env[n].stage = ENV_IDLE; }
        v->gain = gain_target(s, v);
        v->cut = s->val[P_FLT_CUT];
        v->svf_cut = s->val[P_SVF_CUT];
        memset(v->to_lad, 0, sizeof v->to_lad);   /* the levels ramp up from 0 in the first control step */
        memset(v->to_svf, 0, sizeof v->to_svf);
        /* the filters' unmodulated settings, for the samples before the next control step works out the rest */
        ladder_set(&v->lad, cut_hz(v->cut), s->val[P_FLT_RES] * 0.01f, s->val[P_FLT_DRIVE] * 0.01f,
                   (int)s->val[P_FLT_MODE], (int)s->val[P_FLT_SLOPE], SR);
        svf_set(&v->svf, cut_hz(v->svf_cut), s->val[P_SVF_RES] * 0.01f, (int)s->val[P_SVF_MODE], SR);
    }
}

static void forget(one_t *s, int note) {
    for (int i = 0; i < s->nheld; i++)
        if (s->held[i] == note) {
            memmove(s->held + i, s->held + i + 1, sizeof s->held[0] * (s->nheld - i - 1));
            s->nheld--;
            return;
        }
}

static void gate_on(voice_t *v) {
    /* a legato note (this voice's key still down) restarts an envelope only with its MULTI TRIG on */
    int legato = v->gate && !v->sustained && v->env[ENV_AMP].stage != ENV_IDLE;
    for (int n = 0; n < NENV; n++)
        if (!legato || v->env[n].cfg->multi) env_gate_on(&v->env[n]);
    v->gate = 1;
    v->sustained = 0;
}

static void gate_off(voice_t *v) {
    for (int n = 0; n < NENV; n++) env_gate_off(&v->env[n]);
    v->gate = 0;
    v->sustained = 0;
}

static void key_up(one_t *s, voice_t *v) {   /* a released key: held on by the sustain pedal, or let go */
    if (s->sustain) v->sustained = 1;
    else gate_off(v);
}

/* a stack of voices (voices first .. first + stack - 1) taken as one: idle when all are, held when any is */
typedef struct { int idle, gated, sustained; float level; unsigned age; } stack_info_t;
static stack_info_t stack_state(const one_t *s, int first, int stack) {
    stack_info_t st = {1, 0, 0, 0.0f, 0};
    for (int i = first; i < first + stack; i++) {
        const voice_t *v = &s->voice[i];
        st.idle &= v->env[ENV_AMP].stage == ENV_IDLE;
        st.gated |= v->gate;
        st.sustained |= v->sustained;
        st.level = fmaxf(st.level, v->env[ENV_AMP].level);
        if (v->age > st.age) st.age = v->age;
    }
    return st;
}

/* the first voice of the stack for a new note, among the voices in use (whole stacks; one voice each without
 * unison): the stack already playing this key, else an idle one, else the quietest releasing one, else the oldest one
 * held only by the sustain pedal, else the oldest held one (stolen whole: it restarts from its current level, no
 * click) */
static int pick_stack(one_t *s, int note) {
    int stack = stack_size(s), used = voices_used(s), best = -1;
    stack_info_t b = {0};
    for (int i = 0; i < used; i += stack)
        if (s->voice[i].note == note && !stack_state(s, i, stack).idle) return i;
    for (int i = 0; i < used; i += stack)
        if (stack_state(s, i, stack).idle) return i;
    for (int i = 0; i < used; i += stack) {
        stack_info_t st = stack_state(s, i, stack);
        if (!st.gated && (best < 0 || st.level < b.level)) { best = i; b = st; }
    }
    if (best >= 0) return best;
    for (int i = 0; i < used; i += stack) {   /* next: the oldest stack held only by the sustain pedal */
        stack_info_t st = stack_state(s, i, stack);
        if (st.sustained && (best < 0 || st.age < b.age)) { best = i; b = st; }
    }
    if (best >= 0) return best;
    for (int i = 0; i < used; i += stack) {
        stack_info_t st = stack_state(s, i, stack);
        if (best < 0 || st.age < b.age) { best = i; b = st; }
    }
    return best;
}

/* voice k of a stack of n: its place in the detune spread, -1..1, symmetric about the centre but uneven (the inner
 * voices sit closer in: u * sqrt|u|), and its share of the level */
static void stack_voice(voice_t *v, int k, int n) {
    float u = n > 1 ? 2.0f * k / (n - 1) - 1.0f : 0.0f;
    v->uni_pos = u * sqrtf(fabsf(u));
    v->uni_gain = 1.0f / sqrtf((float)n);
}

static void note_on(one_t *s, int note, int vel) {
    forget(s, note);              /* a repeated key moves to the top instead of being held twice */
    if (s->nheld == MAX_HELD) forget(s, s->held[0]);   /* drop the oldest key */
    s->held[s->nheld++] = note;
    int stack = stack_size(s), first = is_mono(s) ? 0 : pick_stack(s, note);
    unsigned age = ++s->age;
    float var = s->val[P_PITCH_VAR] * 0.01f;   /* cents to semitones */
    for (int k = 0; k < stack; k++) {
        voice_t *v = &s->voice[first + k];
        v->velocity = vel / 127.0f;
        v->rand = white(&v->noise);
        v->pvar = var > 0.0f ? white(&v->noise) * var : 0.0f;   /* each voice its own, within +-the variance */
        v->age = age;
        stack_voice(v, k, stack);
        start_note(s, v, note, s->nheld > 1);
        gate_on(v);
    }
}

static void note_off(one_t *s, int note) {
    int was_top = s->nheld > 0 && s->held[s->nheld - 1] == note;
    forget(s, note);
    if (is_mono(s)) {   /* mono: back to the key still held, without a retrigger; else let go */
        if (s->nheld == 0) {
            for (int i = 0; i < MAX_VOICES; i++)   /* the stack, and any left from before a switch to mono */
                if (s->voice[i].gate && !s->voice[i].sustained) key_up(s, &s->voice[i]);
        } else if (was_top) {
            int stack = stack_size(s);
            for (int k = 0; k < stack; k++) {
                voice_t *v = &s->voice[k];
                int start = !v->gate;   /* this voice wasn't playing (a switch from poly or to unison): start it */
                if (start) stack_voice(v, k, stack);
                start_note(s, v, s->held[s->nheld - 1], 1);
                if (start) gate_on(v);
            }
        }
        return;
    }
    for (int i = 0; i < MAX_VOICES; i++)
        if (s->voice[i].note == note && s->voice[i].gate && !s->voice[i].sustained) key_up(s, &s->voice[i]);
}

static void sustain_pedal(one_t *s, int down) {
    s->sustain = down;
    if (down) return;
    for (int i = 0; i < MAX_VOICES; i++)
        if (s->voice[i].sustained) gate_off(&s->voice[i]);
}

static void all_off(one_t *s, int hard) {
    s->nheld = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s->voice[i];
        if (hard)   /* All Sound Off: silent within a few ms, without a click */
            for (int n = 0; n < NENV; n++) env_kill(&v->env[n]);
        else   /* latched envelopes too */
            for (int n = 0; n < NENV; n++) env_release(&v->env[n]);
        v->gate = 0;
        v->sustained = 0;
    }
}

static void midi(void *inst, const uint8_t *m, int len) {
    one_t *s = inst;
    if (len < 2) return;
    int st = m[0] & 0xf0, d1 = m[1] & 0x7f, d2 = len > 2 ? m[2] & 0x7f : 0;
    if (st == 0xd0) { s->aftertouch = d1 / 127.0f; return; }   /* channel pressure: two bytes */
    if (len < 3) return;
    if (st == 0x90 && d2) note_on(s, d1, d2);
    else if (st == 0x80 || st == 0x90) note_off(s, d1);
    else if (st == 0xe0) s->bend = (((d2 << 7) | d1) - 8192) / 8192.0f;
    else if (st == 0xb0 && d1 == 1) s->wheel = d2 / 127.0f;
    else if (st == 0xb0 && d1 == 64) sustain_pedal(s, d2 >= 64);
    else if (st == 0xb0 && d1 == 121) {   /* reset all controllers */
        s->wheel = s->aftertouch = s->bend = 0.0f;
        sustain_pedal(s, 0);
    }
    else if (st == 0xb0 && (d1 == 120 || d1 == 123)) all_off(s, d1 == 120);
}

/* "state": every value as key=value;... (the wrapper saves it with the project) */
static void set_state(one_t *s, const char *str) {
    char *buf = s->state_buf;
    if (strlen(str) >= sizeof s->state_buf) return;   /* never restore half a state */
    strcpy(buf, str);
    load_defaults(s);   /* a state saved before a param existed leaves that param at its default */
    char *save = NULL;
    for (char *kv = strtok_r(buf, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        store(s, kv, eq + 1);
    }
}

static int get_state(const one_t *s, char *buf, int n) {
    int used = 0;
    for (int i = 0; i < P_COUNT && used < n; i++)
        if (KEYS[i][0]) used += snprintf(buf + used, n - used, "%s%s=%g", used ? ";" : "", KEYS[i], s->val[i]);
    return used < n ? used : 0;
}

static void set_param(void *inst, const char *key, const char *val) {
    one_t *s = inst;
    if (!strcmp(key, "lfo_bpm")) {   /* the host's tempo (HAS_LFO_BPM), not a param: only synced envelopes need it applied */
        float bpm = (float)atof(val);
        if (isfinite(bpm) && bpm >= 20.0f && bpm <= 400.0f && bpm != s->bpm) {
            s->bpm = bpm;
            apply_envs(s);   /* synced envelope times follow the tempo */
        }
        return;
    }
    if (!strcmp(key, "state")) set_state(s, val);
    else store(s, key, val);
    apply(s);
    int count = voices_used(s);
    if (count < s->voices)   /* fewer voices: let go of the ones past the new count (they finish their release) */
        for (int i = count; i < MAX_VOICES; i++) {
            gate_off(&s->voice[i]);
            for (int n = 0; n < NENV; n++) env_release(&s->voice[i].env[n]);   /* latched ones too */
        }
    s->voices = count;
    for (int i = 0; i < MAX_VOICES; i++)   /* LATCH off: envelopes it held open after key up now release */
        for (int n = 0; n < NENV; n++)
            if (!s->ecfg[n].latch && s->voice[i].env[n].held && !s->voice[i].gate) env_release(&s->voice[i].env[n]);
}

static int time_text(char *buf, int n, float ms) {
    if (ms < 10.0f) return snprintf(buf, n, "%.1f ms", ms);
    if (ms < 1000.0f) return snprintf(buf, n, "%.0f ms", ms);
    return snprintf(buf, n, "%.2f s", ms * 0.001f);
}

/* "<key>_display": the text MPC shows for a knob whose 0..100 position is a log scale or a tempo division */
static int display_text(const one_t *s, int k, char *buf, int n) {
    if (k == P_FLT_CUT || k == P_SVF_CUT) {
        float hz = cut_hz(s->val[k]);
        return hz < 1000.0f ? snprintf(buf, n, "%.0f Hz", hz) : snprintf(buf, n, "%.2f kHz", hz * 0.001f);
    }
    if (k >= P_ENV_A && k < P_ENV_A + NENV * ENV_STRIDE && (k - P_ENV_A) % ENV_STRIDE != 2) {   /* A, D, R: not S */
        if (s->val[P_FENV_SYNC + EX_STRIDE * ((k - P_ENV_A) / ENV_STRIDE)] > 0.5f)
            return snprintf(buf, n, "%s", SYNC_DIV[env_div(s->val[k])].name);
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k >= P_FENV_DELAY && k < P_FENV_DELAY + NENV * EX_STRIDE) {   /* delay, hold, time scale */
        int x = (k - P_FENV_DELAY) % EX_STRIDE, e = (k - P_FENV_DELAY) / EX_STRIDE;
        if (x == X_SCALE) return snprintf(buf, n, "%.0f%%", env_scale(s->val[k]) * 100.0f);
        if (x != X_DELAY && x != X_HOLD) return 0;
        if (s->val[k] <= 0.0f) return snprintf(buf, n, "0 ms");
        if (s->val[P_FENV_SYNC + EX_STRIDE * e] > 0.5f) return snprintf(buf, n, "%s", SYNC_DIV[env_div(s->val[k])].name);
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k == P_GLIDE_TIME) return s->val[k] > 0.0f ? time_text(buf, n, glide_ms(s->val[k])) : snprintf(buf, n, "0 ms");
    if (k == P_MODFX_RATE) {
        float hz = modfx_hz(s->val[k]);
        return snprintf(buf, n, hz < 1.0f ? "%.2f Hz" : "%.1f Hz", hz);
    }
    if (k == P_DLY_TIME) {
        if (s->val[P_DLY_SYNC] > 0.5f) return snprintf(buf, n, "%s", SYNC_DIV[sync_div(s->val[k])].name);
        return time_text(buf, n, dly_ms(s));
    }
    if (k == P_LFO1_RATE || k == P_LFO2_RATE) {
        int lfo = k == P_LFO2_RATE;
        if (s->val[k + 2] > 0.5f) return snprintf(buf, n, "%s", SYNC_DIV[sync_div(s->val[k])].name);
        float hz = lfo_hz(s, lfo);
        return snprintf(buf, n, hz < 1.0f ? "%.2f Hz" : "%.1f Hz", hz);
    }
    return 0;
}

static int get_param(void *inst, const char *key, char *buf, int n) {
    one_t *s = inst;
    if (!strcmp(key, "state")) return get_state(s, buf, n);
    size_t len = strlen(key);
    if (len > 8 && !strcmp(key + len - 8, "_display")) {
        char base[32];
        if (len - 8 >= sizeof base) return 0;
        memcpy(base, key, len - 8);
        base[len - 8] = 0;
        int k = slot(base);
        return k < 0 ? 0 : display_text(s, k, buf, n);
    }
    int k = slot(key);
    return k < 0 ? 0 : snprintf(buf, n, "%g", s->val[k]);
}

/* linear up to half scale (one oscillator at full velocity is untouched), then bends smoothly towards full scale,
 * like an analog mixer driven hard, instead of wrapping or clipping */
static inline float soft_limit(float x) {
    float a = fabsf(x);
    if (a <= 0.5f) return x;
    float u = (a - 0.5f) * 2.0f;
    return copysignf(0.5f + 0.5f * u / (1.0f + u), x);
}

/* the shared LFOs, once per control step */
static void control_lfos(one_t *s) {
    for (int n = 0; n < NLFO; n++)
        s->lfo_val[n] = lfo_next(&s->lfo[n], (int)s->val[P_LFO1_WAVE + LFO_STRIDE * n], lfo_hz(s, n), CR, SR);
}

/* one voice's modulation for the next control step: the matrix, pitch, both cutoffs and filter settings, levels */
static void control_voice(one_t *s, voice_t *v) {
    const float *p = s->val;
    float src[MS_COUNT], dm[MD_COUNT];
    glide_step(s, v);
    update_note_hz(s, v);
    float key = voice_key(s, v);   /* the glided pitch drives keyboard tracking and the Keyboard source */
    src[MS_OFF] = 0.0f;
    src[MS_FENV] = env_run(&v->env[ENV_FILTER], CR) * env_vel(s, ENV_FILTER, v->velocity);
    src[MS_AENV] = v->env[ENV_AMP].level * env_vel(s, ENV_AMP, v->velocity);
    src[MS_MENV] = env_run(&v->env[ENV_MOD], CR) * env_vel(s, ENV_MOD, v->velocity);
    src[MS_LFO1] = s->lfo_val[0];
    src[MS_LFO2] = s->lfo_val[1];
    src[MS_VEL] = v->velocity;
    src[MS_WHEEL] = s->wheel;
    src[MS_AT] = s->aftertouch;
    src[MS_KEY] = (key - 60.0f) / 60.0f;
    src[MS_RAND] = v->rand;
    mod_eval(&p[P_MOD_BASE], src, dm);
    float fenv = src[MS_FENV], key_oct = (key - 60.0f) / 12.0f;   /* tracking pivots on middle C, as on the Moog One */

    float semis = s->lfo_val[0] * p[P_PITCH_LFO] * 0.01f * LFO_PITCH_ST + src[MS_MENV] * p[P_PITCH_ENV] * 0.01f * ENV_PITCH_ST
                  + s->bend * p[P_BEND_RANGE] + dm[MD_PITCH] * MOD_PITCH_ST;
    const float osc_semis[NOSC] = {dm[MD_O1_PITCH] * MOD_PITCH_ST, dm[MD_O2_PITCH] * MOD_PITCH_ST, dm[MD_O3_PITCH] * MOD_PITCH_ST};
    set_pitch(s, v, semis, osc_semis);

    v->cut += (p[P_FLT_CUT] - v->cut) * s->glide;
    v->svf_cut += (p[P_SVF_CUT] - v->svf_cut) * s->glide;
    float lad_oct = fenv * p[P_LAD_ENV] * 0.01f * ENV_CUT_OCT + s->lfo_val[1] * p[P_LAD_LFO] * 0.01f * LFO_CUT_OCT
                    + key_oct * p[P_LAD_KBD] * 0.01f + dm[MD_LAD_CUT] * MOD_CUT_OCT;
    float svf_oct = fenv * p[P_SVF_ENV] * 0.01f * ENV_CUT_OCT + s->lfo_val[1] * p[P_SVF_LFO] * 0.01f * LFO_CUT_OCT
                    + key_oct * p[P_SVF_KBD] * 0.01f + dm[MD_SVF_CUT] * MOD_CUT_OCT;
    ladder_set(&v->lad, cut_hz(v->cut) * exp2f(lad_oct), clampf(p[P_FLT_RES] * 0.01f + dm[MD_LAD_RES], 0.0f, 1.0f),
               clampf(p[P_FLT_DRIVE] * 0.01f + dm[MD_LAD_DRIVE], 0.0f, 1.0f), (int)p[P_FLT_MODE], (int)p[P_FLT_SLOPE], SR);
    svf_set(&v->svf, cut_hz(v->svf_cut) * exp2f(svf_oct), clampf(p[P_SVF_RES] * 0.01f + dm[MD_SVF_RES], 0.0f, 1.0f),
            (int)p[P_SVF_MODE], SR);
    set_targets(s, v, dm);
}

/* a value ramped across a control step: its start (the step's target when mid-step) and its step per sample */
static inline float ramp_from(float from, float to, int first, float inv, float *step) {
    *step = first ? (to - from) * inv : 0.0f;
    return first ? from : to;
}

/* render n (<= CR) samples of one voice, adding into acc; levels, wave shapes, FM depth and the filter mix ramp from
 * the last control step's values to this one's */
static void render_voice(one_t *s, voice_t *v, float *acc, int n, int first) {
    const float *p = s->val;
    float lad[NSRC], svf[NSRC], dlad[NSRC], dsvf[NSRC];
    int sync2 = p[P_O2_SYNC] > 0.5f, sync3 = p[P_O3_SYNC] > 0.5f;
    int series = (int)p[P_FLT_ROUTE] == FLT_SERIES, any_svf = 0;
    int ring23 = (int)p[P_RING_SRC] == RING_2X3, route = (int)p[P_FM_ROUTE];
    float inv = 1.0f / (float)n;
    int heard[NSRC], fading = 0;   /* fading: a level changes during this step */
    for (int k = 0; k < NSRC; k++) {
        lad[k] = ramp_from(v->to_lad[k], v->tgt_lad[k], first, inv, &dlad[k]);
        svf[k] = ramp_from(v->to_svf[k], v->tgt_svf[k], first, inv, &dsvf[k]);
        heard[k] = lad[k] != 0.0f || svf[k] != 0.0f || v->tgt_lad[k] != 0.0f || v->tgt_svf[k] != 0.0f;
        any_svf |= svf[k] != 0.0f || v->tgt_svf[k] != 0.0f;
        fading |= dlad[k] != 0.0f || dsvf[k] != 0.0f;
        v->to_lad[k] = v->tgt_lad[k];
        v->to_svf[k] = v->tgt_svf[k];
    }
    osc_shape_t sh[NOSC];
    float da[NOSC], dpw[NOSC], dmx[NOSC], dml, dfm;
    int moving = 0;   /* a wave shape changes during this step */
    for (int k = 0; k < NOSC; k++) {
        const osc_shape_t *t = &v->tgt_shape[k];
        sh[k] = *t;
        sh[k].a = ramp_from(v->shape[k].a, t->a, first, inv, &da[k]);
        if (da[k] != 0.0f) osc_slopes(&sh[k]);
        sh[k].pw = ramp_from(v->shape[k].pw, t->pw, first, inv, &dpw[k]);
        sh[k].mix = ramp_from(v->shape[k].mix, t->mix, first, inv, &dmx[k]);
        moving |= da[k] != 0.0f || dpw[k] != 0.0f || dmx[k] != 0.0f;
        v->shape[k] = *t;
    }
    float ml = ramp_from(v->mix_lad, v->tgt_mix, first, inv, &dml);
    float fm = ramp_from(v->fm, v->tgt_fm, first, inv, &dfm);
    int fm_on = fm != 0.0f || v->tgt_fm != 0.0f;
    v->mix_lad = v->tgt_mix;
    v->fm = v->tgt_fm;
    /* oscillators 2 and 3 run only when heard or feeding the ring modulator or FM (osc 1 always runs: it is the sync
     * master); osc 2 is in both ring pairs */
    int run2 = heard[1] || heard[RING];
    int run3 = heard[2] || (heard[RING] && ring23) || (fm_on && route == FM_3TO1);
    int run_lad = series || ml != 0.0f || v->mix_lad != 0.0f;
    if (!any_svf) svf_reset(&v->svf);   /* bypassed: start clean when something is routed to it again */
    if (!run_lad) ladder_reset(&v->lad);
    float target = gain_target(s, v);
    osc_t *o1 = &v->osc[0], *o2 = &v->osc[1], *o3 = &v->osc[2];
    for (int i = 0; i < n; i++) {
        /* linear FM: the carrier's frequency moves with the modulator's output (3 > 1 uses osc 3's last sample) */
        if (fm_on && route == FM_3TO1) o1->inc = clampf(v->inc0[0] * (1.0f + fm * v->fm_prev), 0.0f, 0.49f);
        float a = osc_next(o1, &sh[0], -1.0f), wrap = o1->wrap_d;
        if (fm_on && route == FM_1TO2) o2->inc = clampf(v->inc0[1] * (1.0f + fm * a), 0.0f, 0.49f);
        if (fm_on && route == FM_1TO3) o3->inc = clampf(v->inc0[2] * (1.0f + fm * a), 0.0f, 0.49f);
        float b = run2 ? osc_next(o2, &sh[1], sync2 ? wrap : -1.0f) : 0.0f;
        float c = run3 ? osc_next(o3, &sh[2], sync3 ? wrap : -1.0f) : 0.0f;
        v->fm_prev = c;
        float rm = heard[RING] ? o2->tri_out * (ring23 ? o3->tri_out : o1->tri_out) : 0.0f;
        float nz = white(&v->noise);
        float lad_in = a * lad[0] + b * lad[1] + c * lad[2] + nz * lad[NOISE] + rm * lad[RING];
        float svf_out = 0.0f, lad_out = 0.0f;
        if (any_svf) {
            svf_out = svf_next(&v->svf, a * svf[0] + b * svf[1] + c * svf[2] + nz * svf[NOISE] + rm * svf[RING]
                                        + nz * NOISE_FLOOR);
            if (series) lad_in += svf_out;
        }
        if (run_lad) lad_out = ladder_next(&v->lad, lad_in + nz * NOISE_FLOOR);
        float f = lad_out * ml + svf_out * (1.0f - ml);
        v->gain += (target - v->gain) * s->gain_coef;
        if (fabsf(v->gain - target) < 1e-7f) v->gain = target;   /* settle exactly: no denormal tail */
        acc[i] += f * env_next(&v->env[ENV_AMP]) * v->gain;
        if (fading)
            for (int k = 0; k < NSRC; k++) { lad[k] += dlad[k]; svf[k] += dsvf[k]; }
        for (int k = 0; k < NOSC && moving; k++) {
            sh[k].pw += dpw[k];
            sh[k].mix += dmx[k];
            if (da[k] != 0.0f) { sh[k].a += da[k]; osc_slopes(&sh[k]); }   /* divides only while the angle moves */
        }
        ml += dml;
        fm += dfm;
    }
}

/* the effect settings in the units fx.h reads */
static void fx_params(const one_t *s, fx_params_t *f) {
    const float *p = s->val;
    f->mod_type = (int)p[P_MODFX_TYPE];
    f->mod_rate_hz = modfx_hz(p[P_MODFX_RATE]);
    f->mod_depth = p[P_MODFX_DEPTH] * 0.01f;
    f->mod_fb = p[P_MODFX_FB] * 0.0095f;   /* +-95% */
    f->mod_mix = p[P_MODFX_MIX] * 0.01f;
    f->dly_ms = dly_ms(s);
    f->dly_fb = p[P_DLY_FB] * 0.0095f;
    f->dly_tone = p[P_DLY_TONE] * 0.01f;
    f->dly_pingpong = p[P_DLY_PP] > 0.5f;
    f->dly_mix = p[P_DLY_MIX] * 0.01f;
    f->rev_decay = 0.2f + 0.77f * p[P_REV_DECAY] * 0.01f;   /* tank gain: 0.2 .. 0.97 */
    f->rev_damp = p[P_REV_DAMP] * 0.009f;
    f->rev_predelay_ms = p[P_REV_PRE];
    f->rev_mix = p[P_REV_MIX] * 0.01f;
}

static void render(void *inst, int16_t *out, int frames) {
    one_t *s = inst;
    float acc[128], l[128], r[128];
    fx_params_t fxp;
    fx_params(s, &fxp);
    int done = 0;
    while (done < frames) {
        int first = s->phase == 0;
        int n = CR - s->phase;
        if (n > frames - done) n = frames - done;
        if (first) control_lfos(s);
        if (n > 128) n = 128;
        memset(acc, 0, sizeof(float) * n);
        for (int k = 0; k < MAX_VOICES; k++) {   /* voices past the current count still finish their release */
            voice_t *v = &s->voice[k];
            if (v->env[ENV_AMP].stage == ENV_IDLE) continue;
            if (first) control_voice(s, v);
            render_voice(s, v, acc, n, first);
        }
        fx_process(&s->fx, &fxp, acc, l, r, n);
        for (int i = 0; i < n; i++) {   /* a last guard: never NaN to the host */
            float yl = isfinite(l[i]) ? soft_limit(l[i]) : 0.0f, yr = isfinite(r[i]) ? soft_limit(r[i]) : 0.0f;
            out[2 * (done + i)] = (int16_t)(yl * 32767.0f);
            out[2 * (done + i) + 1] = (int16_t)(yr * 32767.0f);
        }
        done += n;
        s->phase = (s->phase + n) % CR;
    }
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
