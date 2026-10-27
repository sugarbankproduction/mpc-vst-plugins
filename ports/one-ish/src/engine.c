/* ONE-ish engine, Phase 2: one band-limited oscillator (saw or square), MIDI pitch, an amp envelope, one voice.
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
#define HEADROOM 0.5f      /* full-scale saw at full velocity and volume peaks at -6 dBFS */
#define STATE_MAX 8192     /* the wrapper's chunk size (wrapper/vst2_wrap.c) */

/* engine-side slots, matched to params.json by key (so the VST order there can grow freely) */
enum { P_O1_WAVE, P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R, P_VOLUME, P_COUNT };
static const char *const KEYS[P_COUNT] = {"o1_wave", "amp_a", "amp_d", "amp_s", "amp_r", "volume"};

typedef struct {
    float val[P_COUNT];   /* each value in its own units: option index, ms or % */
    osc_t osc;
    env_t amp;
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
        if (!strcmp(KEYS[i], key)) return i;
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

/* recompute what the audio path reads from the parameter values */
static void apply(one_t *s) {
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
    s->note = -1;
    s->gain_coef = env_coef(5.0f, SR);
    apply(s);
    return s;
}

static void destroy(void *inst) { free(inst); }

static void start_note(one_t *s, int note) {
    if (s->amp.stage == ENV_IDLE) s->osc.phase = 0.0f;   /* from silence every note starts the same way */
    s->note = note;
    osc_set_freq(&s->osc, 440.0f * powf(2.0f, (note - 69) / 12.0f), SR);
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
        used += snprintf(buf + used, n - used, "%s%s=%g", i ? ";" : "", KEYS[i], s->val[i]);
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

static void render(void *inst, int16_t *out, int frames) {
    one_t *s = inst;
    int wave = (int)s->val[P_O1_WAVE];
    float target = s->velocity * s->val[P_VOLUME] * 0.01f * HEADROOM * 32767.0f;
    if (s->amp.stage == ENV_IDLE) s->gain = target;   /* silent: nothing to smooth */
    for (int i = 0; i < frames; i++) {
        float y = 0.0f;
        if (s->amp.stage != ENV_IDLE) {
            s->gain += (target - s->gain) * s->gain_coef;
            y = osc_next(&s->osc, wave) * env_next(&s->amp) * s->gain;
        }
        int16_t v = (int16_t)clampf(y, -32768.0f, 32767.0f);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
