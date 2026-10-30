/* THE FACTORY BANKS (presets/banks/bank_*.py, 2026-10-30): every preset of a bank loaded as the wrapper loads a program
 * (each listed param in file order) into a fresh instance and played a phrase that suits its category: a bass line
 * (C2), a melody with legato steps and a held note (C4-C5), or two chords (C3-C4; a split adds a bass note under each);
 * a preset that runs the sequencer, the arpeggiator or a generator holds a key (or a chord) instead. After the plain
 * part, the same notes again with the mod wheel and aftertouch full up, so what they reach is played too. Per preset:
 * the peak (whole phrase, both channels), the loudness (the loudest 300 ms of RMS in the plain part, both channels)
 * and the CPU (microseconds per 128-frame block on this build: the ASan one in test/run.sh, the -O2 one in the bench).
 * Fails on a peak above -1 dBFS, silence, a non-finite sample (the engine's "heals") or a loudness more than 6 dB
 * from the bank's median. Writes <dir>/bank_<x>_demo.wav, every preset's phrase in program order, to listen to.
 * Bank B's presets are played their own way (further down). A bank's presets, their category and type are found by
 * desktop/factory_tags.h (written by presets/make_presets.py with presets.json). Both banks measure loudness with one
 * helper, bk_loudest_db(). Included from offline_check.c (its helpers: E, check, failures, SR, put16, put32,
 * write_wav); bank_checks_all() runs every bank. */
#include "../desktop/factory_tags.h"

void one_ish_render_float(void *inst, float *out_lr, int frames);   /* src/synths.h: render() as floats */

#define BK_MAX 512
#define BK_PHRASE_MS 3600
#define BK_PLAIN_MS 2100     /* the plain part (loudness is measured here); the wheel and aftertouch part follows */
#define BK_FRAMES (SR * BK_PHRASE_MS / 1000)

static double bk_energy(const float *lr, int i) {   /* frame i's L*L + R*R, a non-finite sample as 0 */
    float l = lr[2 * i], r = lr[2 * i + 1];
    return (isfinite(l) ? (double)l * l : 0) + (isfinite(r) ? (double)r * r : 0);
}

/* the loudness both banks use: the loudest `win` frames of RMS (both channels) among frames [0, frames) of the
 * interleaved stereo lr, in dB (a non-finite sample counts as 0); `hop`: the step between windows (1: every frame) */
static double bk_loudest_db(const float *lr, int frames, int win, int hop) {
    double best = 0, e2 = 0;
    for (int i = 0; i < frames; i++) {   /* a running sum over the window ending at frame i */
        e2 += bk_energy(lr, i);
        if (i >= win) e2 -= bk_energy(lr, i - win);
        int s = i - win + 1;   /* the window's first frame */
        if (s >= 0 && s % hop == 0 && e2 > best) best = e2;
    }
    return 10 * log10(best / (2.0 * win) + 1e-18);
}

static const char *bk_value(int p, const char *key) {   /* preset p's value for key (an option's index), or NULL */
    for (int i = 0; i < PRESETS[p].n; i++)
        if (!strcmp(PARAMS[PRESETS[p].values[i].param].key, key)) return PRESETS[p].values[i].value;
    return NULL;
}
static int bk_on(int p, const char *key) { const char *v = bk_value(p, key); return v && atoi(v) != 0; }

static int bk_self_playing(int p) {   /* holding a key plays a sequence or a pattern: hold, don't play a line */
    static const char *const keys[] = {"seq_on", "arp_on", "gen_euclid_on", "gen_tm_on", "phys_balls_on",
                                       "phys_parts_on", "rain_on", "mkv_on", "life_on", "orb_on", "pend_on"};
    for (int s = 0; s < 3; s++)
        for (unsigned k = 0; k < sizeof keys / sizeof *keys; k++) {
            char key[32];
            snprintf(key, sizeof key, "%s%s", s ? (s == 1 ? "s2_" : "s3_") : "", keys[k]);
            if (bk_on(p, key) && (!s || bk_on(p, s == 1 ? "s2_syn_on" : "s3_syn_on"))) return 1;
        }
    return 0;
}

typedef struct { int ms; uint8_t st, d1, d2; } bk_ev_t;
enum { BK_LINE, BK_MELODY, BK_CHORDS };

static int bk_note(bk_ev_t *ev, int n, int on_ms, int off_ms, int key) {
    ev[n++] = (bk_ev_t){on_ms, 0x90, (uint8_t)key, 100};
    ev[n++] = (bk_ev_t){off_ms, 0x80, (uint8_t)key, 0};
    return n;
}
static int bk_chord(bk_ev_t *ev, int n, int on_ms, int off_ms, const int *keys, int k) {
    for (int i = 0; i < k; i++) n = bk_note(ev, n, on_ms, off_ms, keys[i]);
    return n;
}
static int bk_expression(bk_ev_t *ev, int n, int on_ms, int off_ms) {   /* mod wheel and aftertouch full, then back */
    ev[n++] = (bk_ev_t){on_ms, 0xB0, 1, 127};
    ev[n++] = (bk_ev_t){on_ms, 0xD0, 127, 0};
    ev[n++] = (bk_ev_t){off_ms, 0xB0, 1, 0};
    ev[n++] = (bk_ev_t){off_ms, 0xD0, 0, 0};
    return n;
}

/* the phrase for a kind of sound, sorted by time; returns the event count */
static int bk_phrase(int kind, int split, int hold, bk_ev_t *ev) {
    int n = 0;
    if (hold) {
        static const int chord[] = {48, 55, 60, 63};
        if (kind == BK_CHORDS) n = bk_chord(ev, n, 0, 3000, chord, 4);
        else n = bk_note(ev, n, 0, 3000, kind == BK_LINE ? 36 : 60);
        if (split) n = bk_note(ev, n, 0, 3000, 36);
        n = bk_expression(ev, n, 2100, 3000);
    } else if (kind == BK_LINE) {
        static const int a[] = {36, 36, 48, 36, 43, 46, 36, 39}, b[] = {36, 43, 36, 48};
        for (int i = 0; i < 8; i++) n = bk_note(ev, n, 250 * i, 250 * i + 200, a[i]);
        for (int i = 0; i < 4; i++) n = bk_note(ev, n, 2100 + 230 * i, 2100 + 230 * i + 190, b[i]);
        n = bk_expression(ev, n, 2100, 3000);
    } else if (kind == BK_MELODY) {   /* legato steps (overlapping keys), a detached note, a held top note */
        static const int k[] = {60, 63, 67, 70, 72}, on[] = {0, 400, 800, 1200, 1600}, off[] = {450, 850, 1150, 1500, 2050};
        for (int i = 0; i < 5; i++) n = bk_note(ev, n, on[i], off[i], k[i]);
        n = bk_note(ev, n, 2150, 3100, 72);
        n = bk_expression(ev, n, 2250, 3100);
    } else {
        static const int c1[] = {48, 55, 58, 63}, c2[] = {44, 51, 56, 60}, c3[] = {46, 53, 58, 62};
        n = bk_chord(ev, n, 0, 900, c1, 4);
        n = bk_chord(ev, n, 1000, 1900, c2, 4);
        n = bk_chord(ev, n, 2100, 3000, c3, 4);
        if (split) {
            n = bk_note(ev, n, 0, 900, 36);
            n = bk_note(ev, n, 1000, 1900, 32);
            n = bk_note(ev, n, 2100, 3000, 34);
        }
        n = bk_expression(ev, n, 2100, 3000);
    }
    for (int i = 1; i < n; i++)   /* insertion sort by time, note-offs first at a tie */
        for (int j = i; j > 0 && (ev[j].ms < ev[j - 1].ms || (ev[j].ms == ev[j - 1].ms && ev[j].st == 0x80 &&
                                                               ev[j - 1].st != 0x80)); j--) {
            bk_ev_t t = ev[j]; ev[j] = ev[j - 1]; ev[j - 1] = t;
        }
    return n;
}

typedef struct { double peak_db, loud_db, us; int finite, heals; } bk_stats_t;

static double bk_db(double a) { return 20 * log10(a + 1e-9); }

/* preset p, played: its stats, its samples (interleaved stereo floats, BK_FRAMES) into lr */
static bk_stats_t bk_play(int p, int kind, int split, float *lr) {
    bk_stats_t st = {0, 0, 0, 1, 0};
    bk_ev_t ev[96];
    int nev = bk_phrase(kind, split, bk_self_playing(p), ev), next = 0;
    void *e = E->create(NULL);
    E->set_param(e, "lfo_bpm", "120");
    for (int i = 0; i < PRESETS[p].n; i++)
        E->set_param(e, PARAMS[PRESETS[p].values[i].param].key, PRESETS[p].values[i].value);
    struct timespec t0, t1;
    double ns = 0;
    int blocks = 0;
    for (int at = 0; at < BK_FRAMES; at += 128) {
        while (next < nev && (long)ev[next].ms * SR / 1000 <= at) {
            uint8_t m[3] = {ev[next].st, ev[next].d1, ev[next].d2};
            E->midi(e, m, ev[next].st == 0xD0 ? 2 : 3);
            next++;
        }
        int n = BK_FRAMES - at < 128 ? BK_FRAMES - at : 128;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        one_ish_render_float(e, lr + 2 * at, n);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
        blocks++;
    }
    char b[32];
    if (E->get_param(e, "heals", b, sizeof b) > 0) st.heals = atoi(b);
    E->destroy(e);
    st.us = ns / 1e3 / blocks;
    double peak = 0;
    for (int i = 0; i < 2 * BK_FRAMES; i++) {
        if (!isfinite(lr[i])) { st.finite = 0; continue; }
        if (fabsf(lr[i]) > peak) peak = fabsf(lr[i]);
    }
    st.peak_db = bk_db(peak);
    st.loud_db = bk_loudest_db(lr, SR * BK_PLAIN_MS / 1000, SR * 3 / 10, SR / 20);   /* 300 ms windows, 50 ms apart */
    return st;
}

static int bk_cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int16_t bk_pcm(float x) { x = x > 1 ? 1 : x < -1 ? -1 : isfinite(x) ? x : 0; return (int16_t)lrintf(x * 32767.0f); }

/* every preset of factory bank `bank` (desktop/factory_tags.h) whose category is one of cats: the table, the checks,
 * <dir of demo_path>/<wav_name> (NULL: no WAV). bench: only the CPU summary (the -O2 build's bench run), no checks. */
static void bank_checks(const char *label, const char *bank, const char *const *cats, const char *demo_path,
                        const char *wav_name, int bench) {
    static float lr[2 * BK_FRAMES];
    static int16_t pcm[2 * BK_FRAMES];
    static bk_stats_t stats[BK_MAX];
    static int idx[BK_MAX];
    static double sorted[BK_MAX];
    char what[256];
    check(NFACTORY_TAGS == NPRESETS, "banks: desktop/factory_tags.h lists every program (make_presets.py wrote both)");
    int np = 0;
    for (int p = 0; p < NPRESETS && p < NFACTORY_TAGS && np < BK_MAX; p++) {
        if (strcmp(FACTORY_TAG[p].bank, bank)) continue;
        int in = 0;
        for (int c = 0; cats[c]; c++) in |= !strcmp(FACTORY_TAG[p].cat, cats[c]);
        if (!in) {
            snprintf(what, sizeof what, "%s: preset \"%.60s\" has one of the bank's categories (%s)", label,
                     FACTORY_TAG[p].name, FACTORY_TAG[p].cat);
            check(0, what);
            continue;
        }
        idx[np++] = p;
    }
    snprintf(what, sizeof what, "%s: %d presets found", label, np);
    check(np > 0, what);
    if (!np) return;
    FILE *wav = NULL;
    if (wav_name && demo_path) {
        char path[512];
        const char *sl = strrchr(demo_path, '/');
        snprintf(path, sizeof path, "%.*s%s", sl ? (int)(sl - demo_path + 1) : 0, demo_path, wav_name);
        wav = fopen(path, "wb");
        if (!wav) { perror(path); failures++; }
        else {
            uint32_t bytes = (uint32_t)np * BK_FRAMES * 4;
            fwrite("RIFF", 1, 4, wav); put32(wav, 36 + bytes); fwrite("WAVEfmt ", 1, 8, wav);
            put32(wav, 16); put16(wav, 1); put16(wav, 2); put32(wav, SR); put32(wav, SR * 4); put16(wav, 4); put16(wav, 16);
            fwrite("data", 1, 4, wav); put32(wav, bytes);
        }
    }
    if (!bench) printf("%s: %-24s %-8s %-6s %8s %8s %8s\n", label, "preset", "category", "type", "peak dB", "loud dB",
                       "us/blk");
    for (int k = 0; k < np; k++) {
        const factory_tag_t *t = &FACTORY_TAG[idx[k]];
        int p = idx[k];
        int kind = !strcmp(t->cat, "Bass") ? BK_LINE : !strcmp(t->cat, "Lead") ? BK_MELODY : BK_CHORDS;
        stats[k] = bk_play(p, kind, !strcmp(t->type, "Split"), lr);
        sorted[k] = stats[k].loud_db;
        if (wav) {
            for (int i = 0; i < 2 * BK_FRAMES; i++) pcm[i] = bk_pcm(lr[i]);
            fwrite(pcm, 4, BK_FRAMES, wav);
        }
        if (!bench) printf("%s: %-24s %-8s %-6s %8.1f %8.1f %8.1f\n", label, t->name, t->cat, t->type, stats[k].peak_db,
                           stats[k].loud_db, stats[k].us);
    }
    if (wav) fclose(wav);
    qsort(sorted, np, sizeof *sorted, bk_cmp);
    double median = np % 2 ? sorted[np / 2] : 0.5 * (sorted[np / 2 - 1] + sorted[np / 2]);
    double us_max = 0, us_sum = 0, peak_max = -200;
    int worst = 0;
    for (int k = 0; k < np; k++) {
        us_sum += stats[k].us;
        if (stats[k].us > us_max) { us_max = stats[k].us; worst = k; }
        if (stats[k].peak_db > peak_max) peak_max = stats[k].peak_db;
    }
    printf("%s: %d presets, loudness median %.1f dB (range %.1f .. %.1f), loudest peak %.1f dBFS, CPU mean %.1f us, "
           "max %.1f us per 128-frame block (%s)\n", label, np, median, sorted[0], sorted[np - 1], peak_max, us_sum / np,
           us_max, FACTORY_TAG[idx[worst]].name);
    if (bench) return;
    int bad_peak = 0, bad_loud = 0, bad_quiet = 0, bad_nan = 0;
    for (int k = 0; k < np; k++) {
        const char *nm = FACTORY_TAG[idx[k]].name;
        if (stats[k].peak_db > -1.0) { printf("     %s: peak %.1f dBFS\n", nm, stats[k].peak_db); bad_peak++; }
        if (stats[k].loud_db < -50.0) { printf("     %s: silent (%.1f dB)\n", nm, stats[k].loud_db); bad_quiet++; }
        else if (fabs(stats[k].loud_db - median) > 6.0) {
            printf("     %s: loudness %.1f dB, %+.1f dB from the median\n", nm, stats[k].loud_db, stats[k].loud_db - median);
            bad_loud++;
        }
        if (!stats[k].finite || stats[k].heals) { printf("     %s: non-finite samples\n", nm); bad_nan++; }
    }
    snprintf(what, sizeof what, "%s: no preset peaks above -1 dBFS (%d do)", label, bad_peak);
    check(!bad_peak, what);
    snprintf(what, sizeof what, "%s: every preset sounds (%d silent)", label, bad_quiet);
    check(!bad_quiet, what);
    snprintf(what, sizeof what, "%s: every preset's samples are finite (%d not)", label, bad_nan);
    check(!bad_nan, what);
    snprintf(what, sizeof what, "%s: every preset within 6 dB of the bank's median loudness %.1f dB (%d not)", label,
             median, bad_loud);
    check(!bad_loud, what);
}

/* bank A (presets/banks/bank_a.py): basses, leads, keys, brass, plucks, organs */
static const char *const BANK_A_CATS[] = {"Bass", "Lead", "Keys", "Brass", "Pluck", "Organ", NULL};
static void bank_a_checks(const char *demo_path) { bank_checks("bank A", "A", BANK_A_CATS, demo_path, "bank_a_demo.wav", 0); }
static void bank_a_bench(void) { bank_checks("bench: bank A", "A", BANK_A_CATS, NULL, NULL, 1); }

/* ======================================================================== bank B */
/* Each preset of bank B (presets/banks/bank_b.py) played by its kind: a phrase then a chord (most), eighth-note hits
 * (Drum), or nothing at all for 4 s (Generative: it must play by itself; if it doesn't, a held C major chord, and the
 * report says so). Measured: the peak (fails over -1 dBFS), the loudness (bk_loudest_db over 400 ms; fails more than
 * 6 dB from the bank's median), silence, NaN / infinity, and the render time per 128-frame block.
 * bank_b_demo.wav (next to demo_path): every preset in program order. */
enum { BB_SECS = 4, BB_FRAMES = BB_SECS * SR, BB_WIN = SR * 4 / 10, BB_MAX = 160, BB_BLK = 128 };
typedef struct { double peak, loud, us, us_max; int notes, nonfinite, held; } bb_res_t;

static double bb_now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
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
    int t0 = b * BB_BLK, t1 = t0 + BB_BLK;
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
    static float f[2 * BB_FRAMES];
    bb_res_t r = {0};
    void *e = bb_load(p);
    bb_count_notes(e);
    for (int b = 0; b < BB_FRAMES / BB_BLK; b++) {
        bb_events(e, kind, b);
        double t = bb_now_us();
        one_ish_render_float(e, f + 2 * b * BB_BLK, BB_BLK);
        t = bb_now_us() - t;
        r.us += t;
        if (t > r.us_max) r.us_max = t;
        r.notes += bb_count_notes(e);
    }
    r.us /= BB_FRAMES / BB_BLK;
    E->destroy(e);
    for (int i = 0; i < BB_FRAMES; i++) {
        float l = f[2 * i], rr = f[2 * i + 1];
        if (!isfinite(l) || !isfinite(rr)) { r.nonfinite++; l = rr = 0; }
        if (fabsf(l) > r.peak) r.peak = fabsf(l);
        if (fabsf(rr) > r.peak) r.peak = fabsf(rr);
        if (pcm) {
            pcm[2 * i] = bk_pcm(l);
            pcm[2 * i + 1] = bk_pcm(rr);
        }
    }
    r.loud = bk_loudest_db(f, BB_FRAMES, BB_WIN, 1);   /* the loudest 400 ms */
    return r;
}

/* bench: only the CPU summary (the -O2 build's bench run), no checks and no WAV */
static void bank_b_run(const char *demo_path, int bench) {
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
    if (!bench) check(n == 128 && names, what);
    int16_t *pcm = bench ? NULL : malloc((size_t)n * BB_FRAMES * 2 * sizeof *pcm);
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
    if (bench) {
        printf("bench: bank B: %d presets, CPU mean %.1f us, max %.1f us per 128-frame block (%s)\n", n,
               n ? us_all / n : 0, us_top, top);
        return;
    }
    double sorted[BB_MAX];
    memcpy(sorted, louds, n * sizeof *sorted);
    qsort(sorted, n, sizeof *sorted, bk_cmp);
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
    if (pcm && demo_path) {
        char path[512];
        const char *sl = strrchr(demo_path, '/');
        snprintf(path, sizeof path, "%.*sbank_b_demo.wav", sl ? (int)(sl - demo_path + 1) : 0, demo_path);
        write_wav(path, pcm, (uint32_t)((size_t)n * BB_FRAMES));
        printf("     wrote %s (%d presets, %d s each)\n", path, n, BB_SECS);
    }
    free(pcm);
}
static void bank_b_checks(const char *demo_path) { bank_b_run(demo_path, 0); }
static void bank_b_bench(void) { bank_b_run(NULL, 1); }
/* ======================================================================== end of bank B */

/* every factory bank (offline_check runs it; offline_check banks <dir>/demo.wav: the banks alone) */
static void bank_checks_all(const char *demo_path) {
    bank_a_checks(demo_path);
    bank_b_checks(demo_path);
}
static void bank_bench_all(void) {
    bank_a_bench();
    bank_b_bench();
}
