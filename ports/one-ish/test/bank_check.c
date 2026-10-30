/* THE FACTORY BANKS (presets/banks/bank_*.py, 2026-10-30): every preset of a bank loaded as the wrapper loads a program
 * (each listed param in file order) into a fresh instance and played a phrase that suits its category: a bass line
 * (C2), a melody with legato steps and a held note (C4-C5), or two chords (C3-C4; a split adds a bass note under each);
 * a preset that runs the sequencer, the arpeggiator or a generator holds a key (or a chord) instead. After the plain
 * part, the same notes again with the mod wheel and aftertouch full up, so what they reach is played too. Per preset:
 * the peak (whole phrase, both channels), the loudness (the loudest 300 ms of RMS in the plain part, both channels)
 * and the CPU (microseconds per 128-frame block on this build: the ASan one in test/run.sh, the -O2 one in the bench).
 * Fails on a peak above -1 dBFS, silence, a non-finite sample (the engine's "heals") or a loudness more than 6 dB
 * from the bank's median. Writes <dir>/bank_<x>_demo.wav, every preset's phrase in program order, to listen to.
 * The category of each preset comes from presets.json's tags (read at ports/one-ish/presets.json: the checks run
 * from the repo root). Included from offline_check.c (its helpers: E, check, failures, SR, put16, put32). */

void one_ish_render_float(void *inst, float *out_lr, int frames);   /* src/synths.h: render() as floats */

#define BK_MAX 512
#define BK_PHRASE_MS 3600
#define BK_PLAIN_MS 2100     /* the plain part (loudness is measured here); the wheel and aftertouch part follows */
#define BK_FRAMES (SR * BK_PHRASE_MS / 1000)

typedef struct { char name[64], type[16], cat[24]; } bk_tag_t;
static bk_tag_t bk_tags[BK_MAX];
static int bk_ntags;

static int bk_field(const char *line, const char *key, char *out, int n) {   /* "key": "value" on one line */
    const char *p = strstr(line, key);
    if (!p) return 0;
    p += strlen(key);
    int k = 0;
    while (*p && *p != '"' && k < n - 1) out[k++] = *p++;
    out[k] = 0;
    return k > 0;
}

/* presets.json has one preset per line ({"name": ..., "values": {...}, "tags": {...}}) */
static int bk_read_tags(void) {
    FILE *f = fopen("ports/one-ish/presets.json", "r");
    if (!f) return 0;
    static char line[1 << 20];
    bk_ntags = 0;
    while (fgets(line, sizeof line, f) && bk_ntags < BK_MAX) {
        bk_tag_t *t = &bk_tags[bk_ntags];
        if (!bk_field(line, "{\"name\": \"", t->name, sizeof t->name)) continue;
        const char *tg = strstr(line, "\"tags\": {");
        if (!tg) continue;
        bk_field(tg, "\"type\": \"", t->type, sizeof t->type);
        bk_field(tg, "\"category\": \"", t->cat, sizeof t->cat);
        bk_ntags++;
    }
    fclose(f);
    return 1;
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
    enum { WIN = SR * 3 / 10, HOP = SR / 20 };
    double loud = 0;
    for (int s = 0; s + WIN <= SR * BK_PLAIN_MS / 1000; s += HOP) {
        double e2 = 0;
        for (int i = s; i < s + WIN; i++) e2 += lr[2 * i] * lr[2 * i] + lr[2 * i + 1] * lr[2 * i + 1];
        e2 = sqrt(e2 / (2.0 * WIN));
        if (e2 > loud) loud = e2;
    }
    st.peak_db = bk_db(peak);
    st.loud_db = bk_db(loud);
    return st;
}

static int bk_cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int16_t bk_pcm(float x) { x = x > 1 ? 1 : x < -1 ? -1 : isfinite(x) ? x : 0; return (int16_t)lrintf(x * 32767.0f); }

/* every preset whose category is one of cats: the table, the checks, <dir of demo_path>/<wav_name> (NULL: no WAV).
 * bench: only the CPU summary (the -O2 build's bench run), no checks. */
static void bank_checks(const char *label, const char *const *cats, const char *demo_path, const char *wav_name,
                        int bench) {
    static float lr[2 * BK_FRAMES];
    static int16_t pcm[2 * BK_FRAMES];
    static bk_stats_t stats[BK_MAX];
    static int idx[BK_MAX];
    static double sorted[BK_MAX];
    char what[256];
    if (!bk_read_tags()) { check(0, "banks: ports/one-ish/presets.json can be read (run from the repo root)"); return; }
    int np = 0;
    for (int t = 0; t < bk_ntags; t++) {
        int in = 0;
        for (int c = 0; cats[c]; c++) in |= !strcmp(bk_tags[t].cat, cats[c]);
        if (!in) continue;
        int p = -1;
        for (int i = 0; i < NPRESETS; i++)
            if (!strncmp(PRESETS[i].name, bk_tags[t].name, 24)) { p = i; break; }
        if (p < 0) {
            snprintf(what, sizeof what, "%s: preset \"%.60s\" of presets.json is in the program list", label, bk_tags[t].name);
            check(0, what);
            continue;
        }
        idx[np++] = t * 1000 + p;   /* (tag, program) */
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
        const bk_tag_t *t = &bk_tags[idx[k] / 1000];
        int p = idx[k] % 1000;
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
           us_max, bk_tags[idx[worst] / 1000].name);
    if (bench) return;
    int bad_peak = 0, bad_loud = 0, bad_quiet = 0, bad_nan = 0;
    for (int k = 0; k < np; k++) {
        const char *nm = bk_tags[idx[k] / 1000].name;
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
static void bank_a_checks(const char *demo_path) { bank_checks("bank A", BANK_A_CATS, demo_path, "bank_a_demo.wav", 0); }
static void bank_a_bench(void) { bank_checks("bench: bank A", BANK_A_CATS, NULL, NULL, 1); }
