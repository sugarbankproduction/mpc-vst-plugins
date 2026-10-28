/* ONE-ish golden render check: old sounds must play exactly as they did, sample for sample.
 *
 *   golden_check gen   <fixture>   with the engine BEFORE a change: writes every factory preset and 24 random sounds as
 *                                  saved states, each with a hash of its float output (one_ish_render_float) on a fixed
 *                                  phrase, once restored from its state and once (presets) loaded as a program
 *   golden_check check <fixture>   with the engine AFTER the change: restores each state, plays the same phrase and
 *                                  compares the hashes; each preset is loaded again from the (regenerated) program list
 *                                  by name, so a preset file that now also lists Synth 2 and 3 must still sound the same
 *
 * The phrase: single notes, a dyad and a chord (no larger than a unison patch's stacks fit, so a re-dividing unison
 * plays as a whole-stack one did), velocity, the sustain pedal, pitch bend, the mod wheel and a long release. Only the
 * engine API is used (mpc_engine()), so the same file builds against any version of the engine. Float output is
 * hashed bit for bit (FNV-1a 64): the test passes only when every sample is identical.
 * Build and run: test/run.sh (check against test/golden_states.txt). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "engine.h"
#include "randomize.h"

void one_ish_render_float(void *inst, float *out_lr, int frames);
void golden_reseed(void);   /* test/golden_rng.cc */

static const mpc_engine_t *E;
enum { NRANDOM = 24, STATE_TEXT = 1 << 16 };

static void midi3(void *e, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    E->midi(e, m, 3);
}

static uint64_t hash_blocks(void *e, int blocks, uint64_t h) {
    float buf[256];
    for (int b = 0; b < blocks; b++) {
        one_ish_render_float(e, buf, 128);
        const unsigned char *p = (const unsigned char *)buf;
        for (size_t i = 0; i < sizeof buf; i++) h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

static int getf(void *e, const char *k, float *v) {
    char b[64];
    if (E->get_param(e, k, b, sizeof b) <= 0) return 0;
    *v = (float)atof(b);
    return 1;
}

/* the largest chord the phrase may play: whole unison stacks only */
static int chord_cap(void *e) {
    float uni = 0, cnt = 0, vo = 7;
    getf(e, "unison", &uni);
    getf(e, "uni_count", &cnt);
    getf(e, "voices", &vo);
    int voices = vo < 0.5f ? 1 : (int)vo + 1, stack = (int)cnt + 2;
    if (uni < 0.5f || voices == 1) return 3;
    if (stack > voices) stack = voices;
    int n = voices / stack;
    return n < 1 ? 1 : n > 3 ? 3 : n;
}

static uint64_t phrase(void *e) {
    uint64_t h = 1469598103934665603ull;
    int cap = chord_cap(e);
    static const int chord[3] = {60, 64, 67};
    midi3(e, 0x90, 48, 100);
    h = hash_blocks(e, 60, h);
    midi3(e, 0x80, 48, 0);
    h = hash_blocks(e, 30, h);
    for (int k = 0; k < cap; k++) midi3(e, 0x90, chord[k], 60 + 20 * k);
    h = hash_blocks(e, 80, h);
    midi3(e, 0xe0, 0, 80);          /* bend up */
    midi3(e, 0xb0, 1, 90);          /* mod wheel */
    h = hash_blocks(e, 40, h);
    midi3(e, 0xb0, 64, 127);        /* sustain down, keys up: held on */
    for (int k = 0; k < cap; k++) midi3(e, 0x80, chord[k], 0);
    midi3(e, 0xe0, 0, 64);
    h = hash_blocks(e, 40, h);
    midi3(e, 0xb0, 64, 0);
    midi3(e, 0x90, 72, 127);
    h = hash_blocks(e, 20, h);
    midi3(e, 0x90, 74, 30);         /* legato into another key */
    midi3(e, 0x80, 72, 0);
    h = hash_blocks(e, 40, h);
    midi3(e, 0x80, 74, 0);
    midi3(e, 0xb0, 1, 0);
    return hash_blocks(e, 260, h);  /* the release and the effect tails */
}

static void *fresh(void) {
    void *e = E->create(NULL);
    golden_reseed();
    E->set_param(e, "lfo_bpm", "120");
    return e;
}

static uint64_t from_state(const char *st) {
    void *e = fresh();
    E->set_param(e, "state", st);
    uint64_t h = phrase(e);
    E->destroy(e);
    return h;
}

static int preset_index(const char *name) {
    for (int i = 0; i < NPRESETS; i++)
        if (!strcmp(PRESETS[i].name, name)) return i;
    return -1;
}

static uint64_t from_program(int idx) {   /* as the wrapper's set_program: each listed param in file order */
    void *e = fresh();
    for (int i = 0; i < PRESETS[idx].n; i++)
        E->set_param(e, PARAMS[PRESETS[idx].values[i].param].key, PRESETS[idx].values[i].value);
    uint64_t h = phrase(e);
    E->destroy(e);
    return h;
}

static void norm_to_str(const param_t *p, float n, char *buf, int len) {   /* as wrapper/vst2_wrap.c */
    n = n < 0 ? 0 : n > 1 ? 1 : n;
    if (p->nopts) snprintf(buf, len, "%d", (int)lroundf(n * (p->nopts - 1)));
    else if (p->int_display) snprintf(buf, len, "%ld", lroundf(p->min + (p->max - p->min) * n));
    else snprintf(buf, len, "%g", p->min + (p->max - p->min) * n);
}

static int gen(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    static char st[STATE_TEXT];
    for (int p = 0; p < NPRESETS; p++) {
        void *e = fresh();
        for (int i = 0; i < PRESETS[p].n; i++)
            E->set_param(e, PARAMS[PRESETS[p].values[i].param].key, PRESETS[p].values[i].value);
        if (E->get_param(e, "state", st, sizeof st) <= 0) { fprintf(stderr, "no state for %s\n", PRESETS[p].name); return 1; }
        E->destroy(e);
        fprintf(f, "P\t%s\t%016llx\t%016llx\t%s\n", PRESETS[p].name, (unsigned long long)from_state(st),
                (unsigned long long)from_program(p), st);
    }
    uint32_t seed = 0x5eed1234u;
    static float cur[NPARAMS], out[NPARAMS];
    for (int r = 0; r < NRANDOM; r++) {
        for (int i = 0; i < NPARAMS; i++) cur[i] = PARAMS[i].def;
        rnd_generate(cur, out, 0, &seed);
        void *e = fresh();
        char b[64];
        for (int i = 0; i < NPARAMS; i++) {
            if (PARAMS[i].string_display) continue;
            norm_to_str(&PARAMS[i], out[i], b, sizeof b);
            E->set_param(e, PARAMS[i].key, b);
        }
        if (E->get_param(e, "state", st, sizeof st) <= 0) { fprintf(stderr, "no state for random %d\n", r); return 1; }
        E->destroy(e);
        fprintf(f, "R\trandom %d\t%016llx\t-\t%s\n", r, (unsigned long long)from_state(st), st);
    }
    fclose(f);
    printf("golden: wrote %d presets and %d random sounds to %s\n", NPRESETS, NRANDOM, path);
    return 0;
}

static int check(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    static char line[STATE_TEXT + 256];
    int n = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        char *kind = strtok(line, "\t"), *name = strtok(NULL, "\t"), *hs = strtok(NULL, "\t"), *hp = strtok(NULL, "\t");
        char *st = strtok(NULL, "\n");
        if (!kind || !name || !hs || !hp || !st) continue;
        unsigned long long want = strtoull(hs, NULL, 16), got = from_state(st);
        n++;
        if (got != want) { printf("FAIL golden: \"%s\" from its old state sounds different\n", name); bad++; }
        if (kind[0] == 'P') {
            int idx = preset_index(name);
            if (idx < 0) { printf("FAIL golden: preset \"%s\" is gone\n", name); bad++; continue; }
            unsigned long long wp = strtoull(hp, NULL, 16), gp = from_program(idx);
            if (gp != wp) { printf("FAIL golden: preset \"%s\" loaded as a program sounds different\n", name); bad++; }
        }
    }
    fclose(f);
    printf("%s golden: %d old sounds (factory presets as states and as programs, random sounds) give the same samples\n",
           bad || !n ? "FAIL" : "ok  ", n);
    return bad || !n;
}

int main(int argc, char **argv) {
    E = mpc_engine();
    if (argc == 3 && !strcmp(argv[1], "gen")) return gen(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "check")) return check(argv[2]);
    fprintf(stderr, "usage: golden_check gen|check <fixture>\n");
    return 2;
}
