/* ONE-ish engine, Phase 3: three band-limited oscillators (saw or square; octave, semitone, fine, level; 2 and 3 can
 * hard-sync to 1), white noise, a mixer, an amp envelope, one voice.
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

#define SR 44100.0f
#define MAX_HELD 16
#define NOSC 3
#define HEADROOM 0.5f      /* one full-scale saw at full velocity and volume peaks at -6 dBFS */
#define STATE_MAX 8192     /* the wrapper's chunk size (wrapper/vst2_wrap.c) */

/* engine-side slots, matched to params.json by key (so the VST order there can grow freely). Per oscillator the
 * slots run WAVE, OCT, SEMI, FINE, LEVEL, SYNC from P_O1_WAVE + OSC_STRIDE * n (osc 1's SYNC slot is unused) */
enum {
    P_O1_WAVE, P_O1_OCT, P_O1_SEMI, P_O1_FINE, P_O1_LEVEL, P_O1_SYNC,
    P_O2_WAVE, P_O2_OCT, P_O2_SEMI, P_O2_FINE, P_O2_LEVEL, P_O2_SYNC,
    P_O3_WAVE, P_O3_OCT, P_O3_SEMI, P_O3_FINE, P_O3_LEVEL, P_O3_SYNC,
    P_NOISE_LEVEL, P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R, P_VOLUME, P_COUNT
};
enum { OSC_STRIDE = P_O2_WAVE - P_O1_WAVE, O_WAVE = 0, O_OCT, O_SEMI, O_FINE, O_LEVEL, O_SYNC };
static const char *const KEYS[P_COUNT] = {
    "o1_wave", "o1_oct", "o1_semi", "o1_fine", "o1_level", "",
    "o2_wave", "o2_oct", "o2_semi", "o2_fine", "o2_level", "o2_sync",
    "o3_wave", "o3_oct", "o3_semi", "o3_fine", "o3_level", "o3_sync",
    "noise_level", "amp_a", "amp_d", "amp_s", "amp_r", "volume"
};
#define OCT_CENTRE 2   /* option index of 8' (no octave shift) in the "oct" lists */

typedef struct {
    float val[P_COUNT];   /* each value in its own units: option index, semitones, cents, ms or % */
    osc_t osc[NOSC];
    float ratio[NOSC];    /* each oscillator's pitch against the played note */
    env_t amp;
    uint32_t noise;       /* xorshift state */
    float note_hz;
    float velocity;
    float gain;           /* smoothed towards velocity x volume, so neither jumps the level mid-note */
    float gain_coef;
    int note;             /* the sounding note, -1 before the first */
    int held[MAX_HELD];   /* keys down, oldest first: releasing the newest falls back to the one before (mono legato) */
    int nheld;
    char state_buf[STATE_MAX];   /* set_state's working copy (allocated with the instance, not per call) */
} one_t;

static int slot(const char *key) {
    for (int i = 0; i < P_COUNT; i++)
        if (KEYS[i][0] && !strcmp(KEYS[i], key)) return i;
    return -1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

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

static void set_pitches(one_t *s) {
    for (int n = 0; n < NOSC; n++) osc_set_freq(&s->osc[n], s->note_hz * s->ratio[n], SR);
}

/* recompute what the audio path reads from the parameter values */
static void apply(one_t *s) {
    for (int n = 0; n < NOSC; n++) {
        const float *o = &s->val[P_O1_WAVE + OSC_STRIDE * n];
        s->ratio[n] = powf(2.0f, (o[O_OCT] - OCT_CENTRE) + o[O_SEMI] / 12.0f + o[O_FINE] / 1200.0f);
    }
    if (s->note >= 0) set_pitches(s);
    env_set_times(&s->amp, s->val[P_AMP_A], s->val[P_AMP_D], s->val[P_AMP_S] * 0.01f, s->val[P_AMP_R], SR);
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
    for (int n = 0; n < NOSC; n++) osc_reset(&s->osc[n]);
    s->noise = 0x9e3779b9u;
    s->note = -1;
    s->gain_coef = env_coef(5.0f, SR);
    apply(s);
    return s;
}

static void destroy(void *inst) { free(inst); }

static void start_note(one_t *s, int note) {
    if (s->amp.stage == ENV_IDLE)   /* from silence every note starts the same way */
        for (int n = 0; n < NOSC; n++) osc_reset(&s->osc[n]);
    s->note = note;
    s->note_hz = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    set_pitches(s);
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
    s->velocity = vel / 127.0f;
    start_note(s, note);
    env_gate_on(&s->amp);
}

static void note_off(one_t *s, int note) {
    int was_top = s->nheld > 0 && s->held[s->nheld - 1] == note;
    forget(s, note);
    if (s->nheld == 0) env_gate_off(&s->amp);
    else if (was_top) start_note(s, s->held[s->nheld - 1]);   /* back to the key still held, no retrigger */
}

static void all_off(one_t *s, int hard) {
    s->nheld = 0;
    env_gate_off(&s->amp);
    if (hard) { s->amp.level = 0.0f; s->amp.stage = ENV_IDLE; }   /* All Sound Off: silence now */
}

static void midi(void *inst, const uint8_t *m, int len) {
    one_t *s = inst;
    if (len < 3) return;
    int st = m[0] & 0xf0;
    if (st == 0x90 && m[2]) note_on(s, m[1] & 0x7f, m[2]);
    else if (st == 0x80 || st == 0x90) note_off(s, m[1] & 0x7f);
    else if (st == 0xb0 && (m[1] == 120 || m[1] == 123)) all_off(s, m[1] == 120);
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
    else store(s, key, val);
    apply(s);
}

static int get_param(void *inst, const char *key, char *buf, int n) {
    one_t *s = inst;
    if (!strcmp(key, "state")) return get_state(s, buf, n);
    int k = slot(key);
    return k < 0 ? 0 : snprintf(buf, n, "%g", s->val[k]);
}

static inline float white(uint32_t *x) {   /* xorshift32: -1..1 */
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return (int32_t)*x * (1.0f / 2147483648.0f);
}

/* linear up to half scale (one oscillator at full velocity is untouched), then bends smoothly towards full scale,
 * like an analog mixer driven hard, instead of wrapping or clipping */
static inline float soft_limit(float x) {
    float a = fabsf(x);
    if (a <= 0.5f) return x;
    float u = (a - 0.5f) * 2.0f;
    return copysignf(0.5f + 0.5f * u / (1.0f + u), x);
}

static void render(void *inst, int16_t *out, int frames) {
    one_t *s = inst;
    const float *v = s->val;
    int w1 = (int)v[P_O1_WAVE], w2 = (int)v[P_O2_WAVE], w3 = (int)v[P_O3_WAVE];
    int sync2 = v[P_O2_SYNC] > 0.5f, sync3 = v[P_O3_SYNC] > 0.5f;
    float l1 = v[P_O1_LEVEL] * 0.01f, l2 = v[P_O2_LEVEL] * 0.01f, l3 = v[P_O3_LEVEL] * 0.01f;
    float ln = v[P_NOISE_LEVEL] * 0.01f;
    float target = s->velocity * v[P_VOLUME] * 0.01f * HEADROOM;
    if (s->amp.stage == ENV_IDLE) s->gain = target;   /* silent: nothing to smooth */
    for (int i = 0; i < frames; i++) {
        float y = 0.0f;
        if (s->amp.stage != ENV_IDLE) {
            /* every oscillator runs even at level 0, so a synced one always follows its master */
            float a = osc_next(&s->osc[0], w1, -1.0f), wrap = s->osc[0].wrap_d;
            float b = osc_next(&s->osc[1], w2, sync2 ? wrap : -1.0f);
            float c = osc_next(&s->osc[2], w3, sync3 ? wrap : -1.0f);
            float mix = a * l1 + b * l2 + c * l3 + white(&s->noise) * ln;
            s->gain += (target - s->gain) * s->gain_coef;
            y = soft_limit(mix * env_next(&s->amp) * s->gain);
        }
        int16_t q = (int16_t)(y * 32767.0f);
        out[2 * i] = q;
        out[2 * i + 1] = q;
    }
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
