/* ONE-ish factory bank checks (2026-10-30): each bank's presets loaded as the wrapper loads a program (params.h's
 * PRESETS, each listed value in file order), played, measured and written to a WAV to listen to. A bank's presets are
 * found by desktop/factory_tags.h (written by presets/make_presets.py with presets.json). One function per bank,
 * kept apart so banks made at the same time merge as a plain union.
 * Build and run: test/run.sh (bank_check <dir>: <dir>/bank_<x>_demo.wav). The CPU figures are this PC's, not the MPC's. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params.h"
#include "engine.h"
#include "../desktop/factory_tags.h"

#define SR 44100
#define BLK 128
void one_ish_render_float(void *inst, float *out_lr, int frames);   /* src/engine.c: the samples before int16 */

static const mpc_engine_t *E;
static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static void write_wav(const char *path, const int16_t *pcm, uint32_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL cannot write %s\n", path); failures++; return; }
    uint32_t bytes = frames * 4, v32;
    uint16_t v16;
    fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16; fwrite(&v32, 4, 1, f); v16 = 1; fwrite(&v16, 2, 1, f); v16 = 2; fwrite(&v16, 2, 1, f);
    v32 = SR; fwrite(&v32, 4, 1, f); v32 = SR * 4; fwrite(&v32, 4, 1, f); v16 = 4; fwrite(&v16, 2, 1, f);
    v16 = 16; fwrite(&v16, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
}

/* ======================================================================== bank B */
/* Each preset of bank B played by its kind: a phrase then a chord (most), eighth-note hits (Drum), or nothing at all
 * for 4 s (Generative: it must play by itself; if it doesn't, a held C major chord, and the report says so). Measured:
 * the peak (fails over -1 dBFS), the loudness (the loudest 400 ms RMS; fails more than 6 dB from the bank's median),
 * silence, NaN / infinity, and the render time per 128-frame block. bank_b_demo.wav: every preset in program order. */
enum { BB_SECS = 4, BB_FRAMES = BB_SECS * SR, BB_WIN = SR * 4 / 10, BB_MAX = 160 };
typedef struct { double peak, loud, us, us_max; int notes, nonfinite, held; } bb_res_t;

static void bb_midi(void *e, int st, int n, int v) {
    uint8_t m[3] = {(uint8_t)st, (uint8_t)n, (uint8_t)v};
    E->midi(e, m, 3);
}
static int bb_count_notes(void *e) {   /* the note-ons the Synths' generators made since the last call */
    static const char *const KEY[3] = {"gen_log", "s2_gen_log", "s3_gen_log"};
    static char b[8192];
    int n = 0;
    for (int c = 0; c < 3; c++) {
        if (E->get_param(e, KEY[c], b, sizeof b) <= 0) continue;
        for (char *p = b; *p;) {
            long long t;
            int nt, v, used;
            if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
            n += v > 0;
            p += used;
        }
    }
    return n;
}
/* the events of the kind at block b: 0 phrase + chord, 1 drum hits, 2 nothing (generative), 3 a held chord */
static void bb_events(void *e, int kind, int b) {
    int t0 = b * BLK, t1 = t0 + BLK;
#define AT(sec) ((int)((sec) * SR) >= t0 && (int)((sec) * SR) < t1)
    if (kind == 0) {
        static const int line[4] = {48, 55, 60, 67}, chord[4] = {48, 60, 64, 67};
        for (int k = 0; k < 4; k++) {
            if (AT(0.3 * k)) bb_midi(e, 0x90, line[k], 100);
            if (AT(0.3 * k + 0.28)) bb_midi(e, 0x80, line[k], 0);
            if (AT(1.25)) bb_midi(e, 0x90, chord[k], 100);
            if (AT(2.9)) bb_midi(e, 0x80, chord[k], 0);
        }
    } else if (kind == 1) {
        for (int k = 0; k < 12; k++) {
            if (AT(0.25 * k)) bb_midi(e, 0x90, 48, k % 2 ? 80 : 115);
            if (AT(0.25 * k + 0.1)) bb_midi(e, 0x80, 48, 0);
        }
    } else if (kind == 3) {
        static const int chord[3] = {48, 52, 55};
        for (int k = 0; k < 3; k++) {
            if (AT(0.0)) bb_midi(e, 0x90, chord[k], 100);
            if (AT(3.4)) bb_midi(e, 0x80, chord[k], 0);
        }
    }
#undef AT
}
static void *bb_load(int p) {
    void *e = E->create(NULL);
    E->set_param(e, "lfo_bpm", "120");
    for (int i = 0; i < PRESETS[p].n; i++) E->set_param(e, PARAMS[PRESETS[p].values[i].param].key, PRESETS[p].values[i].value);
    return e;
}
static bb_res_t bb_play(int p, int kind, int16_t *pcm) {
    static float f[2 * BB_FRAMES], sq[BB_FRAMES];
    bb_res_t r = {0};
    void *e = bb_load(p);
    bb_count_notes(e);
    for (int b = 0; b < BB_FRAMES / BLK; b++) {
        bb_events(e, kind, b);
        double t = now_us();
        one_ish_render_float(e, f + 2 * b * BLK, BLK);
        t = now_us() - t;
        r.us += t;
        if (t > r.us_max) r.us_max = t;
        r.notes += bb_count_notes(e);
    }
    r.us /= BB_FRAMES / BLK;
    E->destroy(e);
    for (int i = 0; i < BB_FRAMES; i++) {
        float l = f[2 * i], rr = f[2 * i + 1];
        if (!isfinite(l) || !isfinite(rr)) { r.nonfinite++; l = rr = 0; }
        if (fabsf(l) > r.peak) r.peak = fabsf(l);
        if (fabsf(rr) > r.peak) r.peak = fabsf(rr);
        sq[i] = 0.5f * (l * l + rr * rr);
        if (pcm) {
            float cl = l > 1 ? 1 : l < -1 ? -1 : l, cr = rr > 1 ? 1 : rr < -1 ? -1 : rr;
            pcm[2 * i] = (int16_t)lrintf(cl * 32767.0f);
            pcm[2 * i + 1] = (int16_t)lrintf(cr * 32767.0f);
        }
    }
    double s = 0, best = 0;   /* the loudest 400 ms */
    for (int i = 0; i < BB_FRAMES; i++) {
        s += sq[i];
        if (i >= BB_WIN) s -= sq[i - BB_WIN];
        if (i >= BB_WIN - 1 && s > best) best = s;
    }
    r.loud = 10 * log10(best / BB_WIN + 1e-20);
    return r;
}
static int bb_cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void bank_b_checks(const char *dir) {
    static int idx[BB_MAX];
    static bb_res_t res[BB_MAX];
    static double louds[BB_MAX];
    int n = 0;
    for (int p = 0; p < NPRESETS && p < NFACTORY_TAGS; p++)
        if (!strcmp(FACTORY_TAG[p].bank, "B") && n < BB_MAX) idx[n++] = p;
    char what[400];
    snprintf(what, sizeof what, "bank B: %d presets (128 wanted), names matching the program list", n);
    int names = 1;
    for (int k = 0; k < n; k++) names &= !strcmp(PRESETS[idx[k]].name, FACTORY_TAG[idx[k]].name);
    check(n == 128 && names, what);
    int16_t *pcm = malloc((size_t)n * BB_FRAMES * 2 * sizeof *pcm);
    double us_all = 0, us_top = 0;
    const char *top = "";
    int own = 0, held = 0, gens = 0;
    for (int k = 0; k < n; k++) {
        int p = idx[k];
        const char *cat = FACTORY_TAG[p].cat;
        int gen = !strcmp(cat, "Generative"), kind = gen ? 2 : !strcmp(cat, "Drum") ? 1 : 0;
        res[k] = bb_play(p, kind, pcm ? pcm + (size_t)k * BB_FRAMES * 2 : NULL);
        if (gen) {
            gens++;
            if (res[k].notes > 0) own++;
            else {
                res[k] = bb_play(p, 3, pcm ? pcm + (size_t)k * BB_FRAMES * 2 : NULL);
                res[k].held = 1;
                held += res[k].notes > 0;
            }
        }
        louds[k] = res[k].loud;
        us_all += res[k].us;
        if (res[k].us > us_top) { us_top = res[k].us; top = PRESETS[p].name; }
    }
    double sorted[BB_MAX];
    memcpy(sorted, louds, n * sizeof *sorted);
    qsort(sorted, n, sizeof *sorted, bb_cmp);
    double med = n ? sorted[n / 2] : 0;
    printf("     bank B: preset (category): peak dBFS, loudness dBFS (loudest 400 ms), CPU us per 128-frame block "
           "(mean / worst), generator notes\n");
    int clip = 0, quiet = 0, bad = 0, off = 0;
    for (int k = 0; k < n; k++) {
        int p = idx[k];
        bb_res_t *r = &res[k];
        double pk = 20 * log10(r->peak + 1e-20);
        int is_gen = !strcmp(FACTORY_TAG[p].cat, "Generative");
        snprintf(what, sizeof what, "%3d %-24s (%s): peak %6.1f, loud %6.1f (%+5.1f), CPU %5.1f / %5.1f us", p + 1,
                 PRESETS[p].name, FACTORY_TAG[p].cat, pk, r->loud, r->loud - med, r->us, r->us_max);
        if (is_gen)
            snprintf(what + strlen(what), sizeof what - strlen(what), ", %d notes %s", r->notes,
                     r->held ? "with a held chord" : "on its own");
        int ok = pk <= -1.0 && r->loud > -60 && !r->nonfinite && fabs(r->loud - med) <= 6.0 && (!is_gen || r->notes > 0);
        clip += pk > -1.0;
        quiet += r->loud <= -60;
        bad += r->nonfinite > 0;
        off += fabs(r->loud - med) > 6.0;
        check(ok, what);
    }
    snprintf(what, sizeof what, "bank B: no preset clips (%d over -1 dBFS), none silent (%d), no NaN or infinity (%d), "
             "all within 6 dB of the median loudness %.1f dBFS (%d outside)", clip, quiet, bad, med, off);
    check(!clip && !quiet && !bad && !off, what);
    snprintf(what, sizeof what, "bank B: the %d generative presets make notes: %d on their own, %d with a held chord", gens,
             own, held);
    check(own + held == gens, what);
    printf("     bank B CPU on this PC: %.1f us per 128-frame block on average (%.1f%% of the 2902 us a block lasts), the "
           "most %.1f us (%s)\n", n ? us_all / n : 0, n ? 100 * us_all / n / 2902.0 : 0, us_top, top);
    if (pcm && dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/bank_b_demo.wav", dir);
        write_wav(path, pcm, (uint32_t)((size_t)n * BB_FRAMES));
        printf("     wrote %s (%d presets, %d s each)\n", path, n, BB_SECS);
    }
    free(pcm);
}
/* ======================================================================== end of bank B */

int main(int argc, char **argv) {
    E = mpc_engine();
    const char *dir = argc > 1 ? argv[1] : ".";
    bank_b_checks(dir);
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
