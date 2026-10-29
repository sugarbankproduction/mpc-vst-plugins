/* The audio input's offline checks (the mixer's EXT channel; the desktop's input, desktop/hostrate.c): included by
 * offline_check.c after multi_check.c, so it uses that file's helpers (check, set, note, patch, rms_env, db ...).
 * The input reaches the engine through one_ish_set_input() before each render, as the desktop build gives it.
 *   routing   EXT into the SVF, the ladder, both or neither, as ROUTE says; the filter it goes through shapes it (a
 *             closed low-pass silences a 1 kHz input); heard only while a voice sounds (the amp envelope gates it)
 *   level 0   with level 0 the input adds exactly nothing (the same samples as with no input); with no input
 *             connected a level above 0 changes nothing either; level follows the knob
 *   matrix    the destination EXT LEVEL: a Constant slot at +50% plays as the knob at 50%; the sequencer's STEP MOD
 *             knob map reaches it
 *   bench     (offline_bench) the EXT channel's cost per voice */

void one_ish_set_input(void *inst, const float *in_lr, int frames);   /* src/engine.c */
void one_ish_render_float(void *inst, float *out_lr, int frames);

/* a sine of f Hz at amplitude a into the input (both channels), `frames` from sample *t on */
static void ext_sine(float *lr, int frames, double f, double a, long *t) {
    for (int i = 0; i < frames; i++, (*t)++) lr[2 * i] = lr[2 * i + 1] = (float)(a * sin(2 * M_PI * f * *t / SR));
}

/* render `frames` of the left channel (float, as the desktop's output) feeding a sine into the input when f > 0 */
static void ext_render(void *e, float *dst, int frames, double f, double a, long *t) {
    float in[256], out[256];
    for (int at = 0; at < frames; at += 128) {
        int n = frames - at < 128 ? frames - at : 128;
        if (f > 0) {
            ext_sine(in, n, f, a, t);
            one_ish_set_input(e, in, n);
        }
        one_ish_render_float(e, out, n);
        for (int i = 0; i < n; i++) dst[at + i] = out[2 * i];
    }
}

static double ext_rms(const float *x, int from, int to) {
    double s = 0;
    for (int i = from; i < to; i++) s += (double)x[i] * x[i];
    return sqrt(s / (to - from));
}

/* EXT alone into the filters: every oscillator at 0, filters open, the effects off, the amp envelope full on */
static void *ext_patch(int route, double level) {
    void *e = E->create(NULL);
    set(e, "o1_level", 0);
    set(e, "mfx_on", 0);
    set(e, "amp_a", 0);
    set(e, "amp_s", 100);
    set(e, "volume", 100);
    set(e, "ext_route", route);
    set(e, "ext_level", level);
    return e;
}

enum { EXT_N = 22050 };
static double ext_level_of(void *e, double f) {   /* rms of a held note with a sine at f into the input */
    static float x[EXT_N];
    long t = 0;
    note(e, 1, 60, 127);
    ext_render(e, x, EXT_N, f, 0.25, &t);
    note(e, 0, 60, 0);
    return ext_rms(x, EXT_N / 2, EXT_N);
}

static void ext_input(void) {
    char what[200];
    /* routing: flt_mix 0 hears the SVF alone, 100 the ladder alone */
    static const char *const RT[] = {"None", "SVF", "Ladder", "Both"};
    for (int r = 0; r < 4; r++) {
        double heard[2];
        for (int m = 0; m < 2; m++) {
            void *e = ext_patch(r, 100);
            set(e, "flt_mix", m ? 100 : 0);
            heard[m] = ext_level_of(e, 1000);
            E->destroy(e);
        }
        int svf = r == 1 || r == 3, lad = r == 2 || r == 3;
        snprintf(what, sizeof what, "ext: route %-6s: through the SVF rms %.5f, through the ladder rms %.5f", RT[r], heard[0],
                 heard[1]);
        check((svf ? heard[0] > 0.05 : heard[0] < 1e-4) && (lad ? heard[1] > 0.05 : heard[1] < 1e-4), what);
    }
    {   /* the filter shapes it: a closed low-pass takes a 1 kHz input away */
        void *e = ext_patch(2, 100);
        double open = ext_level_of(e, 1000);
        set(e, "flt_cut", 0);   /* 20 Hz */
        double shut = ext_level_of(e, 1000);
        E->destroy(e);
        e = ext_patch(1, 100);
        set(e, "flt_mix", 0);
        set(e, "svf_mode", 2);   /* HP */
        set(e, "svf_cut", 100);
        double hp = ext_level_of(e, 1000);
        E->destroy(e);
        snprintf(what, sizeof what, "ext: a 1 kHz input through the ladder at 20 kHz rms %.4f, at 20 Hz %.1f dB; through an "
                 "SVF high-pass at 20 kHz %.1f dB", open, db(shut, open), db(hp, open));
        check(open > 0.05 && db(shut, open) < -40 && db(hp, open) < -20, what);
    }
    {   /* only while a voice sounds; the level follows the knob */
        static float x[EXT_N];
        long t = 0;
        void *e = ext_patch(3, 100);
        ext_render(e, x, EXT_N, 1000, 0.25, &t);
        double idle = ext_rms(x, 0, EXT_N);
        double full = ext_level_of(e, 1000);
        set(e, "ext_level", 50);
        double half = ext_level_of(e, 1000);
        E->destroy(e);
        snprintf(what, sizeof what, "ext: no note held rms %.6f; level 50%% is %.2f dB under 100%%", idle, db(half, full));
        check(idle < 1e-6 && fabs(db(half, full) + 6.02) < 0.3, what);
    }
    {   /* level 0 adds exactly nothing; no input changes nothing at any level */
        static float a[3][EXT_N];
        for (int k = 0; k < 3; k++) {
            void *e = E->create(NULL);
            set(e, "ext_level", k == 2 ? 80 : 0);
            set(e, "ext_route", 3);
            long t = 0;
            for (int n = 0; n < 3; n++) note(e, 1, 48 + 7 * n, 100);
            ext_render(e, a[k], EXT_N / 2, k == 1 ? 440 : 0, 0.5, &t);
            for (int n = 0; n < 3; n++) note(e, 0, 48 + 7 * n, 0);
            ext_render(e, a[k] + EXT_N / 2, EXT_N / 2, k == 1 ? 440 : 0, 0.5, &t);
            E->destroy(e);
        }
        int same1 = !memcmp(a[0], a[1], sizeof a[0]), same2 = !memcmp(a[0], a[2], sizeof a[0]);
        snprintf(what, sizeof what, "ext: level 0 with an input plays the same samples as no input (%s); level 80%% with "
                 "no input connected too (%s)", same1 ? "yes" : "no", same2 ? "yes" : "no");
        check(same1 && same2 && ext_rms(a[0], 0, EXT_N) > 1e-3, what);
    }
    {   /* the matrix destination EXT LEVEL: Constant at +50% = the knob at 50% */
        void *e = ext_patch(2, 50);
        double knob = ext_level_of(e, 500);
        set(e, "ext_level", 0);
        set(e, "mod2_src", MS_CONST);
        set(e, "mod2_dst", MD_EXT_LEVEL);
        set(e, "mod2_amt", 50);
        double mod = ext_level_of(e, 500);
        E->destroy(e);
        char name[64] = "";
        int k = pindex("mod2_dst");
        if (k >= 0 && PARAMS[k].nopts == MD_COUNT) snprintf(name, sizeof name, "%s", PARAMS[k].opts[MD_EXT_LEVEL]);
        snprintf(what, sizeof what, "ext: matrix \"%s\" from Constant at +50%% %.2f dB from the knob at 50%%", name,
                 db(mod, knob));
        check(!strcmp(name, "Ext Level") && fabs(db(mod, knob)) < 0.1 && knob > 0.02, what);
    }
}

static void ext_bench(void) {   /* the EXT channel per voice: 8 voices of the default sound, with and without it */
    static float x[128 * 2];
    double us[2];
    for (int on = 0; on < 2; on++) {
        void *e = E->create(NULL);
        set(e, "ext_level", on ? 60 : 0);
        set(e, "ext_route", 3);
        set(e, "voices", 7);   /* 8 */
        for (int n = 0; n < 8; n++) note(e, 1, 48 + 3 * n, 100);
        float in[256];
        long t = 0;
        enum { BL = 4000 };
        struct timespec t0, t1;
        for (int b = 0; b < 200; b++) { ext_sine(in, 128, 300, 0.3, &t); one_ish_set_input(e, in, 128); one_ish_render_float(e, x, 128); }
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BL; b++) {
            ext_sine(in, 128, 300, 0.3, &t);   /* (the same input work both ways) */
            one_ish_set_input(e, in, 128);
            one_ish_render_float(e, x, 128);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        us[on] = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) * 1e-3) / BL;
        E->destroy(e);
    }
    printf("bench: 8 voices, EXT off %6.2f / on (both filters) %6.2f us per 128-frame block: the EXT channel %+.3f us per "
           "voice\n", us[0], us[1], (us[1] - us[0]) / 8);
}
