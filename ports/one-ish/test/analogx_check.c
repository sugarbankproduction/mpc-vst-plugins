/* (2026-11-02) The ANALOG page's newer sounds (docs/ANALOG_RESEARCH.md items 2, 3, 5, 7): included from offline_check.c
 * after analog_measure.c (its helpers); `offline_check analogx` runs these alone, `analogxbench` the CPU.
 *   at 0          each control at 0 (or moved and back) gives a fresh instance's very samples; with every one up,
 *                 ANALOG AMOUNT 0% does too; a fresh state doesn't mention them; they are saved and restored
 *   NON-LINEAR SVF  0 is the linear element to rounding; the resonant peak compresses with level; SELF-OSC whistles in
 *                 tune at a steady level, only with both controls up; SVF DRIVE adds harmonics
 *   MIXER DRIVE   a sine's harmonics rise with it; printed: level and brightness
 *   FEEDBACK      level, brightness and pitch at each setting; it is per voice (two notes are the sum of each alone)
 *   extremes      everything up in every mode, pair and route, HQ too: finite and bounded; after the release, exact 0
 *   bench         the CPU of each, on against off */

static const char *const AX_KEYS[] = {"an_svf", "an_svf_drive", "an_svf_osc", "an_mix_drive", "an_feedback"};
enum { AX_NKEYS = sizeof AX_KEYS / sizeof AX_KEYS[0] };

/* a sound that reaches both filters and the things the controls touch */
static void *ax_rich(void) {
    void *e = patch(SAW, 1, 300, 80, 100);
    set(e, "voices", 7);
    set(e, "o2_level", 70); set(e, "o2_freq", 0.1); set(e, "o3_level", 50);
    route(e, "o1", 2); route(e, "o2", 2); route(e, "o3", 2);
    set(e, "flt_mix", 50); set(e, "flt_cut", 70); set(e, "flt_res", 40); set(e, "svf_cut", 65); set(e, "svf_res", 60);
    return e;
}
static uint64_t ax_hash(void *e, float *peak) {
    static float x[SR * 2];
    static const int NT[4] = {36, 48, 55, 64};
    uint64_t h = 1469598103934665603ull;
    float pk = 0;
    for (int k = 0; k < 4; k++) note(e, 1, NT[k], 110);
    hq_render(e, x, SR + SR / 2);
    for (int k = 0; k < 4; k++) note(e, 0, NT[k], 0);
    hq_render(e, x + SR + SR / 2, SR / 2);
    for (int i = 0; i < SR * 2; i++) {
        uint32_t u;
        memcpy(&u, &x[i], 4);
        h = (h ^ u) * 1099511628211ull;
        if (fabsf(x[i]) > pk) pk = fabsf(x[i]);
    }
    if (peak) *peak = pk;
    return h;
}

static void ax_at_zero(void) {
    char what[300];
    void *f = ax_rich();
    uint64_t fresh = ax_hash(f, NULL);
    E->destroy(f);
    int moved = 1;
    for (int k = 0; k < AX_NKEYS; k++) {   /* moved and back, before the notes and with a sound playing */
        void *e = ax_rich();
        set(e, AX_KEYS[k], 1);
        set(e, AX_KEYS[k], 0);
        moved &= ax_hash(e, NULL) == fresh;
        E->destroy(e);
    }
    void *e = ax_rich();
    for (int k = 0; k < AX_NKEYS; k++) set(e, AX_KEYS[k], 100);
    set(e, "an_svf_osc", 1);
    set(e, "analog_amt", 0);
    int amt0 = ax_hash(e, NULL) == fresh;
    E->destroy(e);
    static char st[VST2_CHUNK_MAX + 1000];
    e = ax_rich();
    E->get_param(e, "state", st, sizeof st);
    int silent = 1;
    for (int k = 0; k < AX_NKEYS; k++) silent &= strstr(st, AX_KEYS[k]) == NULL;
    E->destroy(e);
    snprintf(what, sizeof what, "the ANALOG page's newer controls at 0: moved and back, a fresh instance's very samples: %s; "
             "all up with ANALOG AMOUNT 0%%, the same: %s; a state at 0 doesn't mention them: %s", moved ? "yes" : "NO",
             amt0 ? "yes" : "NO", silent ? "yes" : "NO");
    check(moved && amt0 && silent, what);
    /* saved and restored */
    e = ax_rich();
    for (int k = 0; k < AX_NKEYS; k++) set(e, AX_KEYS[k], 1);
    set(e, "an_svf", 70); set(e, "an_svf_drive", 30);
    E->get_param(e, "state", st, sizeof st);
    uint64_t was = ax_hash(e, NULL);
    E->destroy(e);
    e = E->create(NULL);
    E->set_param(e, "state", st);
    int kept = get(e, "an_svf") == 70 && get(e, "an_svf_drive") == 30 && get(e, "an_svf_osc") == 1 && ax_hash(e, NULL) == was;
    E->destroy(e);
    check(kept, "the ANALOG page's newer controls are saved with the sound: a restored state has them and plays the same samples");
}

/* Synth 1's SVF alone, with no key tracking or envelope on its cutoff */
static void *ax_svf_patch(double hz, double res) {
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "voices", 0);
    route(e, "o1", 1);
    set(e, "flt_mix", 0); set(e, "svf_cut", knob_for_hz(hz)); set(e, "svf_res", res); set(e, "svf_kbd", 0); set(e, "svf_env", 0);
    return e;
}

static void ax_svf_checks(void) {
    char what[400];
    /* c 0 is the linear element */
    {
        svf_t a, b;
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        float g = svf_g(800, SR);
        svf_coefs(&a, g, 0.3f, SVF_LP);
        svf_coefs(&b, g, 0.3f, SVF_LP);
        double d = 0;
        for (int i = 0; i < SR; i++) {
            float x = sinf(0.02f * i) + 0.3f * sinf(0.31f * i);
            d = fmax(d, fabs((double)svf_next(&a, x) - svf_tick_nl(&b, x, g, 0.0f)));
        }
        snprintf(what, sizeof what, "NON-LINEAR SVF at c 0 is the linear element to rounding (largest difference %.1e)", d);
        check(d < 2e-4, what);
    }
    /* compression: the peak's gain at the cutoff, quiet against loud */
    double g[2][2];
    for (int nl = 0; nl < 2; nl++)
        for (int lv = 0; lv < 2; lv++) {
            static mz_flt_t f;
            mz_sig_t s = {MZ_SINE, 1000, lv ? 1.0 : 0.01};
            mz_init(&f, nl ? MZ_SVFN : MZ_SVF, 1000, 0.95);
            mz_run(&f, &s, mz_x, SR / 2);
            g[nl][lv] = mz_db(mz_amp(mz_x + SR / 4, SR / 4, 1000)) - mz_db(s.amp);
        }
    snprintf(what, sizeof what, "NON-LINEAR SVF: the resonant peak's gain (resonance 95%%) %+.1f dB quiet, %+.1f dB at full "
             "scale; the linear SVF %+.1f / %+.1f", g[1][0], g[1][1], g[0][0], g[0][1]);
    check(g[1][0] > 12 && g[1][0] - g[1][1] > 8 && fabs(g[0][0] - g[0][1]) < 0.1, what);
    /* self-oscillation through the engine */
    static const double HZ[] = {50, 200, 1000, 4000, 8000};
    double lo = 1e9, hi = -1e9;
    for (unsigned i = 0; i < sizeof HZ / sizeof HZ[0]; i++) {
        void *e = ax_svf_patch(HZ[i], 100);
        set(e, "o1_level", 0); set(e, "an_svf", 100); set(e, "an_svf_osc", 1);
        note(e, 1, 60, 127);
        int n = HZ[i] < 100 ? SR * 5 : SR * 2;
        hq_render(e, mz_x, n);
        const float *t = mz_x + n - SR;
        double hz = mz_freq(t, SR), c = 1200 * log2(hz / HZ[i]), lv = mz_db(mz_rms(t, SR));
        lo = fmin(lo, lv); hi = fmax(hi, lv);
        snprintf(what, sizeof what, "SVF SELF-OSC at %5.0f Hz: %8.2f Hz (%+.1f cents), rms %.1f dB, harmonics 2-5 %.0f dB", HZ[i], hz, c,
                 lv, mz_thd(t, SR, hz));
        check(fabs(c) < (HZ[i] > 5000 ? 20 : 10) && lv > -20, what);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "SVF SELF-OSC holds its level across the cutoffs: %.1f to %.1f dB", lo, hi);
    check(hi - lo < 3, what);
    /* only with both up */
    double off[2];
    for (int k = 0; k < 2; k++) {
        void *e = ax_svf_patch(1000, 100);
        set(e, "o1_level", 0);
        set(e, k ? "an_svf_osc" : "an_svf", k ? 1 : 100);
        note(e, 1, 60, 127);
        hq_render(e, mz_x, SR);
        off[k] = mz_db(mz_rms(mz_x + SR / 2, SR / 2));
        E->destroy(e);
    }
    snprintf(what, sizeof what, "no whistle at 100%% resonance unless both are up: NON-LINEAR SVF alone %.0f dB, SELF-OSC alone %.0f dB",
             off[0], off[1]);
    check(off[0] < -70 && off[1] < -70, what);
    /* SVF DRIVE: a sine through the audio input: harmonics rise, the level stays about the same */
    double h3[3], lvl[3];
    for (int k = 0; k < 3; k++) {
        void *e = ax_svf_patch(20000, 0);
        long t = 0;
        set(e, "o1_level", 0); set(e, "ext_route", 1); set(e, "ext_level", 100); set(e, "volume", 70);
        set(e, "an_svf_drive", k * 50);
        note(e, 1, 45, 127);
        ext_render(e, mz_x, SR, 200, 0.7, &t);
        h3[k] = mz_db(mz_amp(mz_x + SR / 2, SR / 2, 600)) - mz_db(mz_amp(mz_x + SR / 2, SR / 2, 200));
        lvl[k] = mz_db(mz_rms(mz_x + SR / 2, SR / 2));
        E->destroy(e);
    }
    snprintf(what, sizeof what, "SVF DRIVE 0 / 50 / 100%% on a sine: the 3rd harmonic against it %.1f / %.1f / %.1f dB, level %.1f / "
             "%.1f / %.1f dB", h3[0], h3[1], h3[2], lvl[0], lvl[1], lvl[2]);
    check(h3[0] < -70 && h3[1] > -40 && h3[2] > h3[1] + 2 && fabs(lvl[2] - lvl[0]) < 6, what);
    /* the element at the very top with a huge input stays bounded, and comes back */
    {
        svf2_t p;
        memset(&p, 0, sizeof p);
        svf2_set(&p, 19800, 0, 1, SVF_LP, SVF_PARALLEL, SR);
        p.nl = SVF_NL_C;
        svf2_set_osc(&p, 1, 1, SVF_LP);
        double m = 0, after = 0;
        int bad = 0;
        for (int i = 0; i < SR * 3; i++) {
            float y = svf2_next_nl(&p, i < SR ? (i / 5 % 2 ? 8.0f : -8.0f) : 0.0f, 1.0f);
            bad |= !isfinite(y);
            m = fmax(m, fabs(y));
            if (i > SR * 2) after = fmax(after, fabs(y));
        }
        snprintf(what, sizeof what, "NON-LINEAR SVF at the top cutoff, past the edge, with +18 dB of square: finite, largest %.1f; "
                 "the whistle after it %.2f", m, after);
        check(!bad && m < 40 && after < 2, what);
    }
}

/* a held low two-saw sound into the ladder, for the printed tables */
static void *ax_mix_patch(int res) {
    void *e = patch(SAW, 1, 300, 100, 100);
    set(e, "voices", 0); set(e, "o2_level", 70); set(e, "o2_freq", 0.1);
    set(e, "flt_cut", knob_for_hz(1500)); set(e, "flt_res", res); set(e, "flt_kbd", 0); set(e, "lad_kbd", 0); set(e, "volume", 40);
    return e;
}
static void ax_mix_checks(void) {
    char what[500];
    /* MIXER DRIVE: harmonics of a sine through the audio input, the ladder wide open */
    double h3[3], l3[3];
    for (int k = 0; k < 3; k++) {
        void *e = patch(SAW, 0, 1, 100, 10);
        long t = 0;
        set(e, "voices", 0); set(e, "o1_level", 0); set(e, "ext_route", 2); set(e, "ext_level", 100); set(e, "volume", 70);
        set(e, "an_mix_drive", k * 50);
        note(e, 1, 45, 127);
        ext_render(e, mz_x, SR, 200, 0.5, &t);
        h3[k] = mz_db(mz_amp(mz_x + SR / 2, SR / 2, 600)) - mz_db(mz_amp(mz_x + SR / 2, SR / 2, 200));
        l3[k] = mz_db(mz_rms(mz_x + SR / 2, SR / 2));
        E->destroy(e);
    }
    snprintf(what, sizeof what, "MIXER DRIVE 0 / 50 / 100%% on a sine: the 3rd harmonic against it %.1f / %.1f / %.1f dB, level %.1f / "
             "%.1f / %.1f dB", h3[0], h3[1], h3[2], l3[0], l3[1], l3[2]);
    check(h3[1] > h3[0] + 10 && h3[2] > h3[1] + 2 && fabs(l3[2] - l3[0]) < 8, what);
    /* level, brightness and the fundamental against the whole, printed: FEEDBACK alone and with MIXER DRIVE at 100% */
    static const int LV[5] = {0, 25, 50, 75, 100};
    for (int row = 0; row < 3; row++)
        for (int res = 0; res < 2; res++) {
            char line[300] = "";
            for (int k = 0; k < 5; k++) {
                void *e = ax_mix_patch(res * 60);
                set(e, row == 0 ? "an_mix_drive" : "an_feedback", LV[k]);
                if (row == 2) set(e, "an_mix_drive", 100);
                note(e, 1, 36, 110);
                hq_render(e, mz_x, SR);
                const float *t = mz_x + SR / 2;
                size_t u = strlen(line);
                snprintf(line + u, sizeof line - u, "  %3d%%: %5.1f dB, bright %.4f, fund %+.1f", LV[k], mz_db(mz_rms(t, SR / 2)),
                         an_bright(t, SR / 2), mz_db(mz_amp(t, SR / 2, 65.406)) - mz_db(mz_rms(t, SR / 2)));
                E->destroy(e);
            }
            printf("analog: %s, resonance %d%%:%s\n", row == 0 ? "MIXER DRIVE" : row == 1 ? "FEEDBACK" : "FEEDBACK with MIXER DRIVE 100%",
                   res * 60, line);
        }
    /* the note is still the note at 75%; with no source only MIXER DRIVE's gain makes the loop howl, and it stays bounded */
    double f75[2], howl[2], howl_d[2];
    for (int k = 0; k < 2; k++) {
        void *e = ax_mix_patch(k * 60);
        set(e, "an_feedback", 75);
        note(e, 1, 36, 110);
        hq_render(e, mz_x, SR);
        f75[k] = mz_db(mz_amp(mz_x + SR / 2, SR / 2, 65.406)) - mz_db(mz_rms(mz_x + SR / 2, SR / 2));
        E->destroy(e);
        for (int d = 0; d < 2; d++) {
            e = ax_mix_patch(k * 60);
            set(e, "o1_level", 0); set(e, "o2_level", 0); set(e, "an_feedback", 100); set(e, "an_mix_drive", d * 100);
            note(e, 1, 36, 110);
            hq_render(e, mz_x, SR * 3);
            (d ? howl_d : howl)[k] = mz_db(mz_rms(mz_x + SR * 2, SR));
            E->destroy(e);
        }
    }
    snprintf(what, sizeof what, "FEEDBACK 75%%: the note's fundamental against the whole, resonance 0 / 60%%: %+.1f / %+.1f dB. 100%% with "
             "no source: %.1f / %.1f dB; with MIXER DRIVE 100%%: %.1f / %.1f dB (a howl, bounded)", f75[0], f75[1], howl[0], howl[1],
             howl_d[0], howl_d[1]);
    check(f75[0] > -15 && howl[0] < -60 && howl_d[0] < 6 && howl_d[1] < 6, what);
    /* per voice: two notes are the sum of each alone */
    static float a[SR], b[SR], c[SR];
    for (int k = 0; k < 3; k++) {
        void *e = ax_mix_patch(30);
        set(e, "voices", 7); set(e, "volume", 15); set(e, "an_feedback", 80); set(e, "an_mix_drive", 50);
        if (k != 1) note(e, 1, 36, 110);
        if (k != 0) note(e, 1, 43, 110);
        hq_render(e, k == 0 ? a : k == 1 ? b : c, SR);
        E->destroy(e);
    }
    double dd = 0, ss = 0;
    for (int i = 0; i < SR; i++) { double d = (double)c[i] - a[i] - b[i]; dd += d * d; ss += (double)c[i] * c[i]; }
    snprintf(what, sizeof what, "FEEDBACK and MIXER DRIVE are per voice: two notes against the sum of each alone: %.1f dB", 10 * log10(dd / ss + 1e-30));
    check(dd < ss * 1e-3, what);
}

/* every sound up in every mode, pair and route; then the release ends in exact silence */
static void ax_extremes(void) {
    char what[300];
    double worst = 0;
    int bad = 0, loud = 0, nonzero = 0, runs = 0;
    for (int hq = 0; hq < 3; hq += 2)
        for (int mode = 0; mode < 4; mode++)
            for (int pair = 0; pair < 2; pair++)
                for (int route_ = 0; route_ < 2; route_++)
                    for (int fm = 0; fm < 2; fm++) {
                        void *e = patch(SAW, 0, 1, 100, 10);
                        set(e, "voices", 7); set(e, "o2_level", 100); set(e, "o3_level", 100);
                        set(e, "o2_freq", 0.3); route(e, "o1", 2); route(e, "o2", 2); route(e, "o3", 2);
                        set(e, "flt_route", route_); set(e, "flt_mix", 50); set(e, "flt_res", 100); set(e, "flt_cut", 60);
                        set(e, "flt_drive", 100); set(e, "svf_res", 100); set(e, "svf_cut", 50); set(e, "svf_mode", mode);
                        set(e, "svf_pair", pair); set(e, "svf_space", pair ? 100 : 0); set(e, "lad_fm", fm * 100);
                        set(e, "svf_fm", fm * 100); set(e, "an_ladder", 100); set(e, "an_hq", hq);
                        for (int k = 0; k < AX_NKEYS; k++) set(e, AX_KEYS[k], 100);
                        set(e, "an_svf_osc", 1);
                        set(e, "analog_amt", 200);
                        for (int k = 0; k < 8; k++) note(e, 1, 30 + 7 * k, 127);
                        hq_render(e, mz_x, SR / 2);
                        for (int i = 0; i < SR / 2; i++) {
                            bad |= !isfinite(mz_x[i]);
                            worst = fmax(worst, fabs(mz_x[i]));
                        }
                        for (int k = 0; k < 8; k++) note(e, 0, 30 + 7 * k, 0);
                        hq_render(e, mz_x, SR / 2);   /* (10 ms of release, and the master stage's own tail) */
                        hq_render(e, mz_x, SR / 2);
                        for (int i = 0; i < SR / 2; i++) nonzero += mz_x[i] != 0.0f;
                        loud += worst > 8;
                        runs++;
                        E->destroy(e);
                    }
    snprintf(what, sizeof what, "the newer controls all up, ANALOG AMOUNT 200%%, every SVF mode, pair and route, FM on and off, "
             "HQ Off and 4x (%d runs): finite (%s), largest sample %.2f, and %d samples not exactly 0 after the release", runs,
             bad ? "NO" : "yes", worst, nonzero);
    check(!bad && !loud && !nonzero, what);
}

static void analogx_checks(void) {
    ax_at_zero();
    ax_svf_checks();
    ax_mix_checks();
    ax_extremes();
}

/* the busy patch of the bench: 8 voices, 3 oscillators, into `svf` both filters in series */
static double ax_time(void (*setup)(void *)) {
    void *e = patch(SAW, 5, 300, 70, 250);
    int16_t buf[256];
    set(e, "voices", 7); set(e, "o2_level", 80); set(e, "o3_level", 60);
    route(e, "o1", 2); set(e, "flt_route", 1); set(e, "lad_env", 40); set(e, "flt_res", 50); set(e, "svf_res", 50);
    if (setup) setup(e);
    for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
    for (int i = 0; i < 200; i++) E->render(e, buf, 128);
    double best = 1e9;
    for (int rep = 0; rep < 9; rep++) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < 1500; i++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 1500;
        if (us < best) best = us;
    }
    E->destroy(e);
    return best;
}
static void ax_b_svf(void *e) { set(e, "an_svf", 100); }
static void ax_b_drive(void *e) { set(e, "an_svf", 100); set(e, "an_svf_drive", 100); }
static void ax_b_pair(void *e) { set(e, "an_svf", 100); set(e, "svf_pair", 0); }
static void ax_b_off_pair(void *e) { set(e, "svf_pair", 0); }
static void ax_b_mixd(void *e) { set(e, "an_mix_drive", 100); }
static void ax_b_fb(void *e) { set(e, "an_feedback", 100); }
static void analogx_bench(void) {
    static const struct { const char *name; void (*fn)(void *); int base; } B[] = {
        {"all off", NULL, 0},
        {"NON-LINEAR SVF 100%", ax_b_svf, 0},
        {"NON-LINEAR SVF + SVF DRIVE 100%", ax_b_drive, 0},
        {"SVF pair in series, all off", ax_b_off_pair, 0},
        {"SVF pair in series, NON-LINEAR SVF 100%", ax_b_pair, 3},
        {"MIXER DRIVE 100%", ax_b_mixd, 0},
        {"FEEDBACK 100%", ax_b_fb, 0},
    };
    double t[sizeof B / sizeof B[0]];
    for (unsigned k = 0; k < sizeof B / sizeof B[0]; k++) t[k] = ax_time(NULL);   /* (warm the caches, and a first noise figure) */
    double noise = 0;
    for (unsigned k = 1; k < sizeof B / sizeof B[0]; k++) noise = fmax(noise, fabs(t[k] - t[0]));
    printf("bench analog: the same run five times differs by up to %.2f us a voice\n", noise / 8);
    for (unsigned k = 0; k < sizeof B / sizeof B[0]; k++) {
        t[k] = ax_time(B[k].fn);
        printf("bench analog: 8 voices, %-44s %7.1f us per 128-frame block, %6.2f per voice (%+.2f)\n", B[k].name, t[k], t[k] / 8,
               (t[k] - t[B[k].base]) / 8);
    }
}
