/* (2026-10-31) The Moog One preset importer's converter (desktop/m1p.c, its tables desktop/m1p_map.h), offline:
 * included by offline_check.c, using its helpers (check, get). The files are our own synthetic ones in test/m1p/
 * (made-up values in the format's layout; no Moog preset is in the project).
 *   basic       each kind of conversion lands where it should in the engine (seconds, note-number cutoffs, Hz with
 *               the LFO range picked, octaves, taps, codes, flags, labels, Synth 2 and 3, the master effects), the
 *               matrix rows (source, controller as VIA, transform, a row that can't map reported), the preset's
 *               name and tags, a "null", a " #raw", an escaped key; unknown settings listed in the report
 *   failures    an empty file, a file of other text, a binary one, one where nothing maps: each says why
 *   level       a sound made as loud as it goes is turned down to a peak under -1 dBFS; a normal one isn't
 *   cpu         what one file costs to convert and check on this PC (the window thread's time, not the audio's) */
#include "../desktop/m1p.h"

#define M1P_DIR "ports/one-ish/test/m1p/"

static char *m1p_file(const char *name, size_t *len) {
    char path[256];
    snprintf(path, sizeof path, M1P_DIR "%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("     (can't open %s)\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    *len = b ? fread(b, 1, n, f) : 0;
    fclose(f);
    if (b) b[*len] = 0;
    return b;
}

/* the state a file makes, loaded into a fresh engine to read back (NULL: it failed) */
static void *m1p_engine(const char *name, m1p_t *m, m1p_level_t *lv) {
    size_t len = 0;
    char *t = m1p_file(name, &len);
    if (!t) return NULL;
    int st = m1p_convert(t, len, m);
    free(t);
    if (st != M1P_OK) return NULL;
    char *state = malloc(VST2_CHUNK_MAX);
    int ok = state && m1p_make_state(m->assign, state, VST2_CHUNK_MAX, lv);
    void *e = ok ? E_REAL->create(NULL) : NULL;
    if (e) E_REAL->set_param(e, "state", state);
    free(state);
    return e;
}

static void m1p_near(void *e, const char *k, double want, double tol) {
    char what[160];
    double got = get(e, k);
    snprintf(what, sizeof what, "m1p: %s = %.3f (want %.3f)", k, got, want);
    check(fabs(got - want) <= tol, what);
}
static void m1p_opt(void *e, const char *k, const char *label) {   /* an option param shows that label */
    char what[160];
    int i = -1;
    for (int p = 0; p < NPARAMS && i < 0; p++)
        if (!strcmp(PARAMS[p].key, k)) i = p;
    int o = -1;
    for (int k2 = 0; i >= 0 && k2 < PARAMS[i].nopts; k2++)
        if (!strcmp(PARAMS[i].opts[k2], label)) o = k2;
    double got = get(e, k);
    snprintf(what, sizeof what, "m1p: %s is %s (option %d, got %.0f)", k, label, o, got);
    check(o >= 0 && fabs(got - o) < 0.01, what);
}
static double m1p_cut(double note) { return 100.0 / 3 * log10(440 * pow(2, (note - 69) / 12) / 20); }

static void m1p_basic(void) {
    m1p_t m;
    m1p_level_t lv;
    void *e = m1p_engine("basic.txt", &m, &lv);
    check(e != NULL, "m1p: the synthetic basic file converts");
    if (!e) { m1p_free(&m); return; }
    /* oscillators, mixer */
    m1p_opt(e, "o1_wave", "Tri");
    m1p_opt(e, "o1_oct", "16'");
    m1p_near(e, "o1_freq", 3.5, 0.01);
    m1p_near(e, "o1_angle", 25, 0.01);
    m1p_near(e, "o1_pw", 23, 0.01);
    m1p_near(e, "o1_level", 80, 0.01);
    m1p_opt(e, "o1_svf", "On");
    m1p_opt(e, "o1_lad", "Off");
    m1p_opt(e, "o2_wave", "Saw");
    m1p_opt(e, "o2_oct", "4'");
    m1p_opt(e, "o2_sync", "On");
    m1p_opt(e, "o2_sync_src", "Osc 1");
    m1p_near(e, "o2_level", 40, 0.01);
    m1p_near(e, "noise_level", 0, 0.01);   /* (" #raw" dropped) */
    /* filters: a note number as a cutoff, a tap as a slope, an escaped key, the SVF's mode from its flags */
    m1p_near(e, "flt_cut", m1p_cut(93), 0.02);
    m1p_near(e, "flt_res", 30, 0.01);
    m1p_opt(e, "flt_mode", "HP");
    m1p_opt(e, "flt_slope", "12 dB");
    m1p_near(e, "svf_cut", m1p_cut(69), 0.02);
    m1p_near(e, "svf_env", 50, 0.01);
    m1p_opt(e, "svf_mode", "BP");
    /* envelopes in seconds onto the 1 ms .. 10 s knobs; e2 is the amp envelope */
    m1p_near(e, "fenv_a", 25, 0.01);
    m1p_near(e, "fenv_s", 25, 0.01);
    m1p_near(e, "amp_a", 0, 0.01);
    m1p_near(e, "amp_d", 75, 0.01);
    m1p_near(e, "amp_s", 100, 0.01);
    m1p_near(e, "amp_r", 50, 0.01);
    /* LFOs: Hz, with the range that holds it */
    m1p_opt(e, "lfo1_range", "Low");
    m1p_near(e, "lfo1_rate", 100.0 / 3 * log10(500), 0.02);
    m1p_opt(e, "lfo1_wave", "S&H");
    m1p_opt(e, "lfo2_range", "Medium");
    m1p_near(e, "lfo2_rate", 100.0 / 3 * log10(400), 0.02);
    /* glide, unison, voices, keys, the Synth */
    m1p_opt(e, "glide", "On");
    m1p_near(e, "glide_time", 50, 0.01);
    m1p_opt(e, "glide_type", "EXP");
    m1p_opt(e, "unison", "On");
    m1p_opt(e, "uni_count", "6");
    m1p_opt(e, "voices", "8");
    m1p_near(e, "key_lo", 36, 0.01);
    m1p_near(e, "key_hi", 96, 0.01);
    m1p_near(e, "pan", 50, 0.01);
    m1p_near(e, "bend_up", 7, 0.01);
    m1p_near(e, "bend_down", -7, 0.01);
    m1p_opt(e, "modfx_on", "On");
    m1p_opt(e, "modfx_type", "Tape Delay");
    /* the matrix: the controller as the source; then a source with the controller as VIA and a transform; the third
     * row's destination has no equivalent; every other row emptied (our defaults' rows 13-16 too) */
    m1p_opt(e, "mod1_src", "Mod Wheel");
    m1p_opt(e, "mod1_dst", "Pitch LFO1 Amt");
    m1p_near(e, "mod1_amt", 25, 0.01);
    m1p_opt(e, "mod1_via", "None");
    m1p_opt(e, "mod2_src", "LFO 2");
    m1p_opt(e, "mod2_via", "Velocity");
    m1p_near(e, "mod2_via_amt", 50, 0.01);
    m1p_opt(e, "mod2_dst", "Ladder Cutoff");
    m1p_near(e, "mod2_amt", -20, 0.01);
    m1p_opt(e, "mod2_xf", "Squared");
    m1p_near(e, "mod2_xf_amt", 40, 0.01);
    m1p_opt(e, "mod3_src", "Off");
    m1p_opt(e, "mod13_src", "Off");
    check(m.matrix == 2, "m1p: two matrix rows converted");
    check(strstr(m.report, "matrix row 3") && strstr(m.report, "its destination has no equivalent"),
          "m1p: the matrix row that can't map is in the report, with why");
    /* Synths 2 and 3, the master effects */
    m1p_opt(e, "s2_syn_on", "On");
    m1p_near(e, "s2_o1_level", 50, 0.01);
    m1p_near(e, "s2_flt_cut", m1p_cut(110), 0.02);
    m1p_opt(e, "s3_syn_on", "Off");
    m1p_opt(e, "rev_on", "On");
    m1p_opt(e, "rev_type", "Hall");
    m1p_opt(e, "dly_on", "Off");   /* type "none" */
    char *st = malloc(VST2_CHUNK_MAX);
    check(st && E_REAL->get_param(e, "state", st, VST2_CHUNK_MAX) > 0 && strstr(st, ";syn_name=TESTSYN"),
          "m1p: Synth 1's name from the file");
    free(st);
    /* the preset's labels, the counts, the report */
    check(!strcmp(m.name, "Test Brassy"), "m1p: the preset's own name");
    check(!strcmp(m.category, "Brass") && !strcmp(m.mood, "Bright") && !strcmp(m.type, "Single"),
          "m1p: its category, mood and type, as our tags spell them");
    check(strstr(m.notes, "made up for the tests") != NULL, "m1p: its comment goes into the notes");
    check(m.empty == 1, "m1p: a null value is counted as empty, not as a mistake");
    check(m.unknown == 5, "m1p: 5 unknown (version, tempo, two sequencer settings, a matrix row)");
    check(strstr(m.report, "/p/part/t/1/p/seq/made_up_key = 3") && strstr(m.report, "Synth 1: seq (2)"),
          "m1p: the unknown settings are listed, and summed up by part");
    check(strstr(m.report, "/p/part/t/1/p/fb/p = 93.0  ->  ") != NULL, "m1p: the report lists what mapped");
    check(m.mapped > 50 && m.guessed > 0 && m.guessed < m.mapped, "m1p: most of it mapped, some of it a guess");
    printf("     basic: %d settings, %d mapped (%d guesses, %d matrix rows), %d unknown, level peak %.1f dBFS\n",
           m.settings, m.mapped, m.guessed, m.matrix, m.unknown, lv.peak_db);
    check(lv.finite && !lv.silent && lv.trim_db == 0 && lv.peak_db < -1, "m1p: the basic sound plays, not too loud");
    E_REAL->destroy(e);
    m1p_free(&m);
}

static void m1p_failures(void) {
    m1p_t m;
    size_t len = 0;
    char *t;
    check(m1p_convert("", 0, &m) == M1P_EMPTY, "m1p: an empty file says it is empty");
    m1p_free(&m);
    check(m1p_convert(" \r\n\t\n", 5, &m) == M1P_EMPTY, "m1p: a file of blank lines is empty");
    m1p_free(&m);
    if ((t = m1p_file("malformed.txt", &len))) {
        check(m1p_convert(t, len, &m) == M1P_NOT_M1P && m.unreadable == 3, "m1p: other text isn't a preset (3 lines "
                                                                            "it can't read)");
        check(!m1p_looks_like(t), "m1p: ... and doesn't look like one");
        m1p_free(&m);
        free(t);
    }
    static const char bin[] = "/p/x 1\n\x01\x02\0\x03 junk";
    check(m1p_convert(bin, sizeof bin - 1, &m) == M1P_BINARY, "m1p: a file with zero bytes is not text");
    m1p_free(&m);
    if ((t = m1p_file("unknown_only.txt", &len))) {
        check(m1p_looks_like(t), "m1p: a file of /key value lines looks like a preset");
        check(m1p_convert(t, len, &m) == M1P_NOTHING_MAPPED && m.unknown == 4, "m1p: nothing known: says so, 4 listed");
        m1p_free(&m);
        free(t);
    }
    char *big = malloc(M1P_TEXT_MAX + 2);
    if (big) {
        memset(big, 'a', M1P_TEXT_MAX + 1);
        check(m1p_convert(big, M1P_TEXT_MAX + 1, &m) == M1P_TOO_BIG, "m1p: a file over 4 MB is too big");
        m1p_free(&m);
        free(big);
    }
    /* a value that makes no sense for its control: counted, the rest still converts */
    static const char odd[] = "/p/part/t/1/p/o1/m \"Q\"\n/p/part/t/1/p/o1/d banana\n/p/part/t/1/p/o2/d 0.5\n";
    check(m1p_convert(odd, sizeof odd - 1, &m) == M1P_OK && m.unreadable == 2 && m.mapped == 1,
          "m1p: values that make no sense are counted and skipped");
    m1p_free(&m);
}

static void m1p_level(void) {
    m1p_t m;
    m1p_level_t lv;
    void *e = m1p_engine("loud.txt", &m, &lv);
    check(e != NULL, "m1p: the loud file converts");
    if (!e) { m1p_free(&m); return; }
    printf("     loud: turned down %.1f dB, peak now %.1f dBFS, Volume %.1f\n", lv.trim_db, lv.peak_db, get(e, "volume"));
    check(lv.trim_db > 0 && lv.peak_db < -1 && lv.finite, "m1p: a sound over -1 dBFS is turned down below it");
    check(get(e, "volume") < 100, "m1p: ... by its Volume");
    E_REAL->destroy(e);
    m1p_free(&m);
}

static void m1p_cpu(void) {
    m1p_t m;
    m1p_level_t lv;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    void *e = m1p_engine("basic.txt", &m, &lv);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("m1p: one file converted and level-checked in %.1f ms (the window's thread; nothing on the audio thread)\n",
           (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
    if (e) E_REAL->destroy(e);
    m1p_free(&m);
}

static void m1p_checks(void) {
    m1p_basic();
    m1p_failures();
    m1p_level();
}
