/* (2026-11-01) The ANALOG page's new defaults and ANALOG AMOUNT. Included from offline_check.c after links_check.c (its
 * helpers: check, set, get, note, E (old defaults: the page all 0) and E_REAL (a new instance as a host has it)).
 *   amount 0      exactly the samples of the page at 0 (spreads, drift, NON-LINEAR LADDER and WARM-UP all off), with
 *                 every one of them up
 *   amount 100    the page as set: 200% with the page at X sounds like 100% with it at 2X; 100% differs from 0%
 *   states        a new instance's state says dv=2, leaves the new defaults out and restores them; an older one (dv=1
 *                 or none) reads what it leaves out of the page as 0; ANALOG AMOUNT is saved and restored, and LOAD
 *                 INTO SYNTH leaves it alone (shared)
 *   stats         a chord with the page at its defaults against all 0: level and brightness (analog_stats, printed)
 *   bench         the defaults' CPU per voice against ANALOG AMOUNT 0 (analog_bench, `offline_check analogbench`) */

static void *an_solo(const mpc_engine_t *eng) {   /* Synth 1 alone, a held two-saw sound into the ladder */
    void *e = eng->create(NULL);
    set(e, "s2_syn_on", 0);
    set(e, "s3_syn_on", 0);
    set(e, "o2_level", 70);
    set(e, "o2_freq", 0.1);
    set(e, "flt_cut", 70);
    set(e, "flt_res", 30);
    set(e, "amp_a", 0);
    set(e, "amp_s", 100);
    set(e, "voices", 7);
    return e;
}
static void an_chord(void *e, int on) {
    static const int NT[4] = {48, 55, 60, 64};
    for (int k = 0; k < 4; k++) note(e, on, NT[k], 100);
}
/* the hash of 3 s of a chord and its release, both channels */
static uint64_t an_hash(void *e) {
    int16_t buf[256];
    uint64_t h = 1469598103934665603ull;
    an_chord(e, 1);
    for (int b = 0; b < 900; b++) {
        if (b == 700) an_chord(e, 0);
        E->render(e, buf, 128);
        for (int i = 0; i < 256; i++) h = (h ^ (uint16_t)buf[i]) * 1099511628211ull;
    }
    return h;
}
enum { AN_FR = 2 * 44100 };
static float an_buf[3][AN_FR];
static void an_render(void *e, float *dst) {   /* 2 s of the chord, left channel */
    an_chord(e, 1);
    render_f(e, dst, AN_FR);
}
static double an_rms(const float *x, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / n);
}
static double an_bright(const float *x, int n) {   /* the share of the power in the first difference: brightness */
    double d = 0, s = 0;
    for (int i = 1; i < n; i++) { d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]); s += (double)x[i] * x[i]; }
    return s > 0 ? d / s : 0;
}

static void analog_checks(void) {
    char what[400];
    /* ANALOG AMOUNT 0: every analog thing up, yet the very samples of the page all 0 */
    void *a = an_solo(E), *b = an_solo(E);
    static const char *const UP[] = {"drift", "an_cut", "an_env", "an_lvl", "an_tune", "an_res", "an_ladder"};
    for (unsigned k = 0; k < sizeof UP / sizeof UP[0]; k++) set(a, UP[k], 80);
    set(a, "an_warm", 1);
    set(a, "analog_amt", 0);
    int same0 = an_hash(a) == an_hash(b);
    E->destroy(a);
    E->destroy(b);
    /* a new instance at ANALOG AMOUNT 0 (the new page's defaults): the old all-0 sound too */
    a = an_solo(E_REAL);
    set(a, "spread", 0);   /* (2026-11-02: SPREAD is not the ANALOG page's; new sounds start at 50%) */
    b = an_solo(E);
    set(a, "analog_amt", 0);
    int same_new0 = an_hash(a) == an_hash(b);
    E->destroy(a);
    E->destroy(b);
    snprintf(what, sizeof what, "ANALOG AMOUNT 0%%: with every spread, drift and the NON-LINEAR LADDER at 80%% and WARM-UP "
             "on, the very samples of the page at 0: %s; a new instance (the page's defaults) at 0%%: the same: %s",
             same0 ? "yes" : "NO", same_new0 ? "yes" : "NO");
    check(same0 && same_new0, what);

    /* 100%: the page as set (differs from 0%); 200% with the page at X is about 100% with it at 2X */
    void *p100 = an_solo(E_REAL), *p0 = an_solo(E_REAL), *x2 = an_solo(E_REAL), *a200 = an_solo(E_REAL);
    set(p0, "analog_amt", 0);
    static const char *const SP[] = {"drift", "an_cut", "an_env", "an_lvl", "an_tune", "an_res"};
    for (unsigned k = 0; k < sizeof SP / sizeof SP[0]; k++) {
        set(x2, SP[k], 2 * get(p100, SP[k]));
    }
    set(a200, "analog_amt", 200);
    an_render(p100, an_buf[0]);
    an_render(p0, an_buf[1]);
    an_render(x2, an_buf[2]);
    static float b200[AN_FR];
    an_render(a200, b200);
    double d10 = 0, d2 = 0, r = an_rms(an_buf[0], AN_FR);
    for (int i = 0; i < AN_FR; i++) {
        d10 += (double)(an_buf[0][i] - an_buf[1][i]) * (an_buf[0][i] - an_buf[1][i]);
        d2 += (double)(an_buf[2][i] - b200[i]) * (an_buf[2][i] - b200[i]);
    }
    d10 = sqrt(d10 / AN_FR);
    d2 = sqrt(d2 / AN_FR);
    snprintf(what, sizeof what, "ANALOG AMOUNT 100%% (the defaults) differs from 0%% (the difference %+.1f dB against the "
             "sound: the voices' phases drift apart); 200%% sounds as 100%% with the page doubled (difference %+.1f dB)",
             20 * log10(d10 / r + 1e-12), 20 * log10(d2 / r + 1e-12));
    check(d10 / r > 0.01 && d2 / r < 0.003, what);
    E->destroy(p100); E->destroy(p0); E->destroy(x2); E->destroy(a200);

    /* states */
    static char st[65536], cmd[66000];
    void *e = E_REAL->create(NULL);
    E->get_param(e, "state", st, sizeof st);
    void *f = E->create(NULL);
    E->set_param(f, "state", st);
    int round = !strncmp(st, "v=1;dv=2", 8) && !strstr(st, ";drift=") && !strstr(st, "an_cut=") &&
                !strstr(st, "analog_amt") && get(f, "drift") == 25 && get(f, "an_cut") == 25 && get(f, "s3_an_env") == 30
                && get(f, "an_res") == 20 && get(f, "analog_amt") == 100;
    set(e, "analog_amt", 50);
    E->get_param(e, "state", st, sizeof st);
    E->set_param(f, "state", st);
    int amt = strstr(st, ";analog_amt=50") && !strstr(st, "s2_analog_amt") && !strstr(st, "~analog_amt") &&
              get(f, "analog_amt") == 50;
    E->set_param(f, "state", "v=1;dv=1;flt_cut=50");
    int old1 = get(f, "drift") == 0 && get(f, "an_cut") == 0 && get(f, "s2_an_tune") == 0 && get(f, "s3_an_lvl") == 0 &&
               get(f, "analog_amt") == 100 && get(f, "flt_cut") == 50 && get(f, "s2_syn_on") == 1;
    E->set_param(f, "state", "v=1;drift=40;an_tune=10");
    int old0 = get(f, "drift") == 40 && get(f, "an_tune") == 10 && get(f, "an_cut") == 0 && get(f, "s2_an_env") == 0 &&
               get(f, "s2_drift") == 0;   /* (no dv: Synth 2 at the old defaults) */
    E->set_param(f, "state", "v=1;dv=2;analog_amt=150");   /* LOAD INTO SYNTH 2: a dv=1 sound, ANALOG AMOUNT stays */
    E->set_param(f, "state", "synthload2\nv=1;dv=1;flt_cut=40;analog_amt=10");
    int into = get(f, "s2_drift") == 0 && get(f, "s2_an_cut") == 0 && get(f, "drift") == 25 && get(f, "analog_amt") == 150;
    snprintf(what, sizeof what, "a new instance's state \"%.12s...\" leaves the ANALOG page's new defaults out and restores "
             "them: %s; ANALOG AMOUNT 50%% saved once and restored: %s; a dv=1 state reads the page as 0 (AMOUNT 100%%): "
             "%s; an older one keeps what it lists: %s; LOAD INTO SYNTH 2 of a dv=1 sound: its page at 0, AMOUNT kept: %s",
             st, round ? "yes" : "NO", amt ? "yes" : "NO", old1 ? "yes" : "NO", old0 ? "yes" : "NO", into ? "yes" : "NO");
    check(round && amt && old1 && old0 && into, what);
    (void)cmd;
    E->destroy(e);
    E->destroy(f);
}

/* level and brightness of a chord: the page's defaults against all 0, and at ANALOG AMOUNT 200% (printed) */
static void analog_stats(void) {
    static const char *const NAME[3] = {"all 0 (digital)", "defaults (100%)", "defaults at 200%"};
    double r[3], br[3], pk[3];
    for (int k = 0; k < 3; k++) {
        void *e = an_solo(E_REAL);
        set(e, "analog_amt", k == 0 ? 0 : k == 1 ? 100 : 200);
        an_render(e, an_buf[k]);
        r[k] = an_rms(an_buf[k] + 4410, AN_FR - 4410);
        br[k] = an_bright(an_buf[k] + 4410, AN_FR - 4410);
        pk[k] = 0;
        for (int i = 0; i < AN_FR; i++) pk[k] = fabs(an_buf[k][i]) > pk[k] ? fabs(an_buf[k][i]) : pk[k];
        E->destroy(e);
    }
    for (int k = 0; k < 3; k++)
        printf("stats ANALOG %-17s a 4-note saw chord, 2 s: RMS %.4f (%+.2f dB), peak %.3f, brightness %.4f (%+.1f%%)\n",
               NAME[k], r[k], 20 * log10(r[k] / r[0]), pk[k], br[k], 100 * (br[k] / br[0] - 1));
    /* one note at a time (no chord beating): C3, then the same key 8 times over (each a new voice: each card heard) */
    for (int k = 0; k < 3; k++) {
        void *e = an_solo(E_REAL);
        set(e, "analog_amt", k == 0 ? 0 : k == 1 ? 100 : 200);
        set(e, "amp_r", 0);
        double lo = 1e9, hi = 0, sum = 0, blo = 1e9, bhi = 0;
        for (int n = 0; n < 8; n++) {
            note(e, 1, 48, 100);
            render_f(e, an_buf[0], 22050);
            note(e, 0, 48, 0);
            render_f(e, an_buf[1], 4410);
            double rr = an_rms(an_buf[0] + 4410, 22050 - 4410), bb = an_bright(an_buf[0] + 4410, 22050 - 4410);
            lo = rr < lo ? rr : lo; hi = rr > hi ? rr : hi; sum += rr;
            blo = bb < blo ? bb : blo; bhi = bb > bhi ? bb : bhi;
        }
        printf("stats ANALOG %-17s C3 played 8 times (8 voices): RMS %.4f on average, spread %.2f dB; brightness "
               "%.4f..%.4f (spread %.1f%%)\n", NAME[k], sum / 8, 20 * log10(hi / lo), blo, bhi, 100 * (bhi / blo - 1));
        E->destroy(e);
    }
}

/* the defaults' CPU: 8 voices of a busy patch at ANALOG AMOUNT 0, 100 and 200% (O2 build: offline_check analogbench) */
static void analog_bench(void) {
    for (int q = 0; q < 3; q++) {
        void *e = an_solo(E_REAL);
        set(e, "analog_amt", q * 100);
        set(e, "o3_level", 60);
        for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
        int16_t buf[256];
        for (int i = 0; i < 200; i++) E->render(e, buf, 128);
        double best = 1e9;
        for (int rep = 0; rep < 7; rep++) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int i = 0; i < 2000; i++) E->render(e, buf, 128);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 2000;
            if (us < best) best = us;
        }
        printf("bench ANALOG AMOUNT %3d%% (the page's defaults), 8 voices: %.1f us per 128-frame block, %.2f us per voice\n",
               q * 100, best, best / 8);
        E->destroy(e);
    }
}
