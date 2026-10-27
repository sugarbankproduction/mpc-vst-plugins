/* ONE-ish engine, Phase 6. Per voice: three band-limited oscillators (saw or square; octave, semitone, fine, level;
 * 2 and 3 can hard-sync to 1) and white noise, each sent to the SVF, the ladder or both; a state-variable filter
 * (LP/BP/HP/notch) and a ladder filter (cutoff, resonance, drive, LP/HP, 24-6 dB) in parallel or in series (SVF into
 * ladder) with a mix between them; three envelopes (filter, amp, mod). Shared: two LFOs (free or tempo-synced).
 * Hard-wired modulation as on the Moog One panel: filter envelope, LFO 2 and keyboard tracking to each cutoff,
 * LFO 1 and the mod envelope to pitch. An 8-slot modulation matrix (source x amount, scaled by a via source) over
 * pitch, levels, both filters and amp; mod wheel, aftertouch and pitch bend. One voice so far (mono, last-note
 * priority).
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

#define SR 44100.0f
#define CR 16              /* control-rate step in samples: modulation is worked out every 0.36 ms */
#define MAX_HELD 16
#define NOSC 3
#define NSRC 4             /* oscillators 1-3 and noise */
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

/* engine-side slots, matched to params.json by key (so the VST order there can grow freely). Per oscillator the
 * slots run WAVE, OCT, SEMI, FINE, LEVEL, SYNC from P_O1_WAVE + OSC_STRIDE * n (osc 1's SYNC slot is unused);
 * the envelopes run A, D, S, R from P_ENV_A + ENV_STRIDE * n in ENV_FILTER, ENV_AMP, ENV_MOD order; the LFOs run
 * RATE, WAVE, SYNC from P_LFO1_RATE + LFO_STRIDE * n; the source routes and levels in source order. */
enum {
    P_O1_WAVE, P_O1_OCT, P_O1_SEMI, P_O1_FINE, P_O1_LEVEL, P_O1_SYNC,
    P_O2_WAVE, P_O2_OCT, P_O2_SEMI, P_O2_FINE, P_O2_LEVEL, P_O2_SYNC,
    P_O3_WAVE, P_O3_OCT, P_O3_SEMI, P_O3_FINE, P_O3_LEVEL, P_O3_SYNC,
    P_NOISE_LEVEL, P_FLT_CUT, P_FLT_RES, P_FLT_DRIVE, P_FLT_MODE, P_FLT_SLOPE,
    P_SVF_CUT, P_SVF_RES, P_SVF_MODE, P_FLT_ROUTE, P_FLT_MIX, P_O1_ROUTE, P_O2_ROUTE, P_O3_ROUTE, P_NOISE_ROUTE,
    P_ENV_A, P_ENV_D, P_ENV_S, P_ENV_R,                 /* filter envelope */
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,                 /* amp envelope */
    P_MENV_A, P_MENV_D, P_MENV_S, P_MENV_R,             /* mod envelope */
    P_LAD_ENV, P_LAD_LFO, P_LAD_KBD, P_SVF_ENV, P_SVF_LFO, P_SVF_KBD, P_PITCH_LFO, P_PITCH_ENV,
    P_LFO1_RATE, P_LFO1_WAVE, P_LFO1_SYNC, P_LFO2_RATE, P_LFO2_WAVE, P_LFO2_SYNC,
    P_BEND_RANGE, P_MOD_BASE,                           /* then MOD_SLOTS x (source, destination, amount, via) */
    P_VOLUME = P_MOD_BASE + MOD_SLOTS * MOD_SLOT_PARAMS, P_COUNT
};
enum { OSC_STRIDE = P_O2_WAVE - P_O1_WAVE, O_WAVE = 0, O_OCT, O_SEMI, O_FINE, O_LEVEL, O_SYNC };
enum { ENV_FILTER, ENV_AMP, ENV_MOD, NENV, ENV_STRIDE = P_AMP_A - P_ENV_A };
enum { LFO_STRIDE = P_LFO2_RATE - P_LFO1_RATE };
static const char *const KEYS[P_COUNT] = {
    "o1_wave", "o1_oct", "o1_semi", "o1_fine", "o1_level", "",
    "o2_wave", "o2_oct", "o2_semi", "o2_fine", "o2_level", "o2_sync",
    "o3_wave", "o3_oct", "o3_semi", "o3_fine", "o3_level", "o3_sync",
    "noise_level", "flt_cut", "flt_res", "flt_drive", "flt_mode", "flt_slope",
    "svf_cut", "svf_res", "svf_mode", "flt_route", "flt_mix", "o1_route", "o2_route", "o3_route", "noise_route",
    "fenv_a", "fenv_d", "fenv_s", "fenv_r",
    "amp_a", "amp_d", "amp_s", "amp_r",
    "menv_a", "menv_d", "menv_s", "menv_r",
    "lad_env", "lad_lfo", "lad_kbd", "svf_env", "svf_lfo", "svf_kbd", "pitch_lfo", "pitch_env",
    "lfo1_rate", "lfo1_wave", "lfo1_sync", "lfo2_rate", "lfo2_wave", "lfo2_sync",
    "bend_range",
#define SLOT_KEYS(n) [P_MOD_BASE + 4 * ((n) - 1)] = "mod" #n "_src", [P_MOD_BASE + 4 * ((n) - 1) + 1] = "mod" #n "_dst", \
                     [P_MOD_BASE + 4 * ((n) - 1) + 2] = "mod" #n "_amt", [P_MOD_BASE + 4 * ((n) - 1) + 3] = "mod" #n "_via"
    SLOT_KEYS(1), SLOT_KEYS(2), SLOT_KEYS(3), SLOT_KEYS(4), SLOT_KEYS(5), SLOT_KEYS(6), SLOT_KEYS(7), SLOT_KEYS(8),
#undef SLOT_KEYS
    [P_VOLUME] = "volume"
};
#define OCT_CENTRE 2   /* option index of 8' (no octave shift) in the "oct" lists */
enum { ROUTE_LADDER, ROUTE_SVF, ROUTE_BOTH };   /* a source's filter buttons (option order in params.json) */
enum { FLT_PARALLEL, FLT_SERIES };

/* tempo-synced LFO rates (the rate knob picks one): beats per LFO cycle */
static const struct { const char *name; float beats; } SYNC_DIV[] = {
    {"4 bars", 16}, {"2 bars", 8}, {"1 bar", 4}, {"1/2", 2}, {"1/4", 1}, {"1/4T", 2.0f / 3}, {"1/8", 0.5f},
    {"1/8T", 1.0f / 3}, {"1/16", 0.25f}, {"1/16T", 1.0f / 6}, {"1/32", 0.125f},
};
#define NDIV ((int)(sizeof SYNC_DIV / sizeof SYNC_DIV[0]))

typedef struct {
    osc_t osc[NOSC];
    ladder_t lad;
    svf_t svf;
    env_t env[NENV];
    uint32_t noise;          /* xorshift state */
    int note;                /* the sounding note, -1 before the first */
    float note_hz, velocity;
    float gain;              /* smoothed towards velocity x volume, so neither jumps the level mid-note */
    float cut, svf_cut;      /* the cutoff knobs (0..100) as this voice's filters follow them, smoothed */
    float rand;              /* this note's random value (-1..1), a matrix source */
    float to_lad[NSRC], to_svf[NSRC], mix_lad;   /* last control step's levels and filter mix (ramp start) */
    float tgt_lad[NSRC], tgt_svf[NSRC], tgt_mix; /* this control step's (ramp end), modulation included */
    float amp_mod;           /* the matrix's amp level factor (0..2) */
} voice_t;

typedef struct {
    float val[P_COUNT];      /* each value in its own units: option index, semitones, cents, % or a 0..100 knob */
    float ratio[NOSC];       /* each oscillator's pitch against the played note */
    voice_t voice;
    lfo_t lfo[NLFO];
    float lfo_val[NLFO];     /* this control step's LFO outputs */
    float bpm;               /* the host's tempo, for synced LFOs */
    float wheel, aftertouch, bend;   /* 0..1, 0..1, -1..1 (times the bend range) */
    float gain_coef, glide;  /* per sample / per control step */
    int phase;               /* samples into the current control step */
    int held[MAX_HELD];      /* keys down, oldest first: releasing the newest falls back to the one before (mono legato) */
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
static int sync_div(float knob) { return (int)clampf(roundf(knob * 0.01f * (NDIV - 1)), 0, NDIV - 1); }

static float lfo_hz(const one_t *s, int n) {
    const float *l = &s->val[P_LFO1_RATE + LFO_STRIDE * n];
    if (l[2] > 0.5f) return s->bpm / 60.0f / SYNC_DIV[sync_div(l[0])].beats;
    return LFO_MIN_HZ * powf(LFO_RANGE, l[0] * 0.01f);
}

/* recompute what the audio path reads from the parameter values */
static void apply(one_t *s) {
    for (int n = 0; n < NOSC; n++) {
        const float *o = &s->val[P_O1_WAVE + OSC_STRIDE * n];
        s->ratio[n] = powf(2.0f, (o[O_OCT] - OCT_CENTRE) + o[O_SEMI] / 12.0f + o[O_FINE] / 1200.0f);
    }
    for (int n = 0; n < NENV; n++) {
        const float *e = &s->val[P_ENV_A + ENV_STRIDE * n];
        env_set_times(&s->voice.env[n], env_ms(e[0]), env_ms(e[1]), e[2] * 0.01f, env_ms(e[3]), SR);
    }
}

static float gain_target(const one_t *s, const voice_t *v) {
    return v->velocity * s->val[P_VOLUME] * 0.01f * HEADROOM * v->amp_mod;
}

/* each source's level into each filter (its routing buttons) and the filter mix, with the matrix's level and mix
 * modulation dm (MD_COUNT values) added */
static void set_targets(const one_t *s, voice_t *v, const float *dm) {
    const float *p = s->val;
    const float lv[NSRC] = {p[P_O1_LEVEL], p[P_O2_LEVEL], p[P_O3_LEVEL], p[P_NOISE_LEVEL]};
    for (int k = 0; k < NSRC; k++) {
        float l = clampf(lv[k] * 0.01f + dm[MD_O1_LEVEL + k], 0.0f, 1.0f);
        int r = (int)p[P_O1_ROUTE + k];
        v->tgt_lad[k] = r != ROUTE_SVF ? l : 0.0f;
        v->tgt_svf[k] = r != ROUTE_LADDER ? l : 0.0f;
    }
    v->tgt_mix = clampf(p[P_FLT_MIX] * 0.01f + dm[MD_FLT_MIX], 0.0f, 1.0f);
    v->amp_mod = clampf(1.0f + dm[MD_AMP], 0.0f, 2.0f);
}

static void *create(const char *data_dir) {
    (void)data_dir;
    one_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        int k = slot(p->key);
        if (k < 0) continue;
        s->val[k] = p->nopts ? roundf(p->def * (p->nopts - 1)) : p->min + p->def * (p->max - p->min);
    }
    voice_t *v = &s->voice;
    for (int n = 0; n < NOSC; n++) osc_reset(&v->osc[n]);
    v->noise = 0x9e3779b9u;
    v->note = -1;
    v->amp_mod = 1.0f;
    v->cut = s->val[P_FLT_CUT];
    v->svf_cut = s->val[P_SVF_CUT];
    for (int n = 0; n < NLFO; n++) lfo_init(&s->lfo[n], 0x2545f491u * (n + 1));
    s->bpm = 120.0f;
    s->gain_coef = env_coef(5.0f, SR);
    s->glide = 1.0f - expf(-(float)CR / (GLIDE_MS * 0.001f * SR));
    apply(s);
    return s;
}

static void destroy(void *inst) { free(inst); }

static void start_note(one_t *s, int note) {
    voice_t *v = &s->voice;
    if (v->env[ENV_AMP].stage == ENV_IDLE) {   /* from silence every note starts the same way */
        for (int n = 0; n < NOSC; n++) osc_reset(&v->osc[n]);
        ladder_reset(&v->lad);
        svf_reset(&v->svf);
        static const float no_mod[MD_COUNT];
        set_targets(s, v, no_mod);   /* unmodulated, until the next control step adds the matrix */
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
    v->note = note;
    v->note_hz = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    for (int n = 0; n < NOSC; n++) osc_set_freq(&v->osc[n], v->note_hz * s->ratio[n], SR);   /* pitch at once */
}

static void forget(one_t *s, int note) {
    for (int i = 0; i < s->nheld; i++)
        if (s->held[i] == note) {
            memmove(s->held + i, s->held + i + 1, sizeof s->held[0] * (s->nheld - i - 1));
            s->nheld--;
            return;
        }
}

static void note_on(one_t *s, int note, int vel) {
    forget(s, note);              /* a repeated key moves to the top instead of being held twice */
    if (s->nheld == MAX_HELD) forget(s, s->held[0]);   /* drop the oldest key */
    s->held[s->nheld++] = note;
    s->voice.velocity = vel / 127.0f;
    s->voice.rand = white(&s->voice.noise);
    start_note(s, note);
    for (int n = 0; n < NENV; n++) env_gate_on(&s->voice.env[n]);
}

static void note_off(one_t *s, int note) {
    int was_top = s->nheld > 0 && s->held[s->nheld - 1] == note;
    forget(s, note);
    if (s->nheld == 0)
        for (int n = 0; n < NENV; n++) env_gate_off(&s->voice.env[n]);
    else if (was_top) start_note(s, s->held[s->nheld - 1]);   /* back to the key still held, no retrigger */
}

static void all_off(one_t *s, int hard) {
    s->nheld = 0;
    for (int n = 0; n < NENV; n++) {
        if (hard) env_kill(&s->voice.env[n]);   /* All Sound Off: silent within a few ms, without a click */
        else env_gate_off(&s->voice.env[n]);
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
    else if (st == 0xb0 && (d1 == 120 || d1 == 123)) all_off(s, d1 == 120);
}

/* "state": every value as key=value;... (the wrapper saves it with the project) */
static void set_state(one_t *s, const char *str) {
    char *buf = s->state_buf;
    if (strlen(str) >= sizeof s->state_buf) return;   /* never restore half a state */
    strcpy(buf, str);
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
    if (!strcmp(key, "state")) set_state(s, val);
    else if (!strcmp(key, "lfo_bpm")) {   /* the host's tempo (HAS_LFO_BPM), not a param */
        float bpm = (float)atof(val);
        if (isfinite(bpm) && bpm >= 20.0f && bpm <= 400.0f) s->bpm = bpm;
    } else store(s, key, val);
    apply(s);
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
    if (k >= P_ENV_A && k < P_ENV_A + NENV * ENV_STRIDE && (k - P_ENV_A) % ENV_STRIDE != 2)   /* A, D, R: not S */
        return time_text(buf, n, env_ms(s->val[k]));
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
    src[MS_OFF] = 0.0f;
    src[MS_FENV] = env_run(&v->env[ENV_FILTER], CR);
    src[MS_AENV] = v->env[ENV_AMP].level;
    src[MS_MENV] = env_run(&v->env[ENV_MOD], CR);
    src[MS_LFO1] = s->lfo_val[0];
    src[MS_LFO2] = s->lfo_val[1];
    src[MS_VEL] = v->velocity;
    src[MS_WHEEL] = s->wheel;
    src[MS_AT] = s->aftertouch;
    src[MS_KEY] = (v->note - 60) / 60.0f;
    src[MS_RAND] = v->rand;
    mod_eval(&p[P_MOD_BASE], src, dm);
    float fenv = src[MS_FENV], key_oct = (v->note - 60) / 12.0f;   /* tracking pivots on middle C, as on the Moog One */

    float semis = s->lfo_val[0] * p[P_PITCH_LFO] * 0.01f * LFO_PITCH_ST + src[MS_MENV] * p[P_PITCH_ENV] * 0.01f * ENV_PITCH_ST
                  + s->bend * p[P_BEND_RANGE] + dm[MD_PITCH] * MOD_PITCH_ST;
    for (int n = 0; n < NOSC; n++)
        osc_set_freq(&v->osc[n], v->note_hz * s->ratio[n] * exp2f((semis + dm[MD_O1_PITCH + n] * MOD_PITCH_ST) / 12.0f), SR);

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

/* render n (<= CR) samples of one voice, adding into acc; levels and the filter mix ramp from the last control
 * step's values to this one's */
static void render_voice(one_t *s, voice_t *v, float *acc, int n, int first) {
    const float *p = s->val;
    float lad[NSRC], svf[NSRC], dlad[NSRC], dsvf[NSRC];
    int w1 = (int)p[P_O1_WAVE], w2 = (int)p[P_O2_WAVE], w3 = (int)p[P_O3_WAVE];
    int sync2 = p[P_O2_SYNC] > 0.5f, sync3 = p[P_O3_SYNC] > 0.5f;
    int series = (int)p[P_FLT_ROUTE] == FLT_SERIES, any_svf = 0;
    float mix_lad = v->tgt_mix;
    float inv = 1.0f / (float)n;
    for (int k = 0; k < NSRC; k++) {
        float tl = v->tgt_lad[k], ts = v->tgt_svf[k];
        lad[k] = first ? v->to_lad[k] : tl;   /* mid-step calls hold the step's target */
        svf[k] = first ? v->to_svf[k] : ts;
        dlad[k] = first ? (tl - lad[k]) * inv : 0.0f;
        dsvf[k] = first ? (ts - svf[k]) * inv : 0.0f;
        v->to_lad[k] = tl;
        v->to_svf[k] = ts;
        any_svf |= svf[k] != 0.0f || ts != 0.0f;
    }
    float ml = first ? v->mix_lad : mix_lad, dml = first ? (mix_lad - ml) * inv : 0.0f;
    v->mix_lad = mix_lad;
    /* oscillators 2 and 3 silent in this step and not heard: skip them (osc 1 always runs: it is the sync master) */
    int run2 = lad[1] != 0.0f || svf[1] != 0.0f || v->to_lad[1] != 0.0f || v->to_svf[1] != 0.0f;
    int run3 = lad[2] != 0.0f || svf[2] != 0.0f || v->to_lad[2] != 0.0f || v->to_svf[2] != 0.0f;
    int run_lad = series || ml != 0.0f || mix_lad != 0.0f;
    if (!any_svf) svf_reset(&v->svf);   /* bypassed: start clean when something is routed to it again */
    if (!run_lad) ladder_reset(&v->lad);
    float target = gain_target(s, v);
    for (int i = 0; i < n; i++) {
        float a = osc_next(&v->osc[0], w1, -1.0f), wrap = v->osc[0].wrap_d;
        float b = run2 ? osc_next(&v->osc[1], w2, sync2 ? wrap : -1.0f) : 0.0f;
        float c = run3 ? osc_next(&v->osc[2], w3, sync3 ? wrap : -1.0f) : 0.0f;
        float nz = white(&v->noise);
        float lad_in = a * lad[0] + b * lad[1] + c * lad[2] + nz * lad[3];
        float svf_out = 0.0f, lad_out = 0.0f;
        if (any_svf) {
            svf_out = svf_next(&v->svf, a * svf[0] + b * svf[1] + c * svf[2] + nz * svf[3] + nz * NOISE_FLOOR);
            if (series) lad_in += svf_out;
        }
        if (run_lad) lad_out = ladder_next(&v->lad, lad_in + nz * NOISE_FLOOR);
        float f = lad_out * ml + svf_out * (1.0f - ml);
        v->gain += (target - v->gain) * s->gain_coef;
        if (fabsf(v->gain - target) < 1e-7f) v->gain = target;   /* settle exactly: no denormal tail */
        acc[i] += f * env_next(&v->env[ENV_AMP]) * v->gain;
        for (int k = 0; k < NSRC; k++) { lad[k] += dlad[k]; svf[k] += dsvf[k]; }
        ml += dml;
    }
}

static void render(void *inst, int16_t *out, int frames) {
    one_t *s = inst;
    voice_t *v = &s->voice;
    float acc[128];
    int done = 0;
    while (done < frames) {
        int first = s->phase == 0;
        int n = CR - s->phase;
        if (n > frames - done) n = frames - done;
        if (first) control_lfos(s);
        if (n > 128) n = 128;
        memset(acc, 0, sizeof(float) * n);
        if (v->env[ENV_AMP].stage != ENV_IDLE) {
            if (first) control_voice(s, v);
            render_voice(s, v, acc, n, first);
        }
        for (int i = 0; i < n; i++) {
            int16_t q = (int16_t)(soft_limit(acc[i]) * 32767.0f);
            out[2 * (done + i)] = q;
            out[2 * (done + i) + 1] = q;
        }
        done += n;
        s->phase = (s->phase + n) % CR;
    }
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
