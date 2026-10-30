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

/* ---- the vocoders (src/fx/vocoder.h): the input as the modulator, the voices (or a send) as the carrier ---- */

/* the vocoder alone on n samples: modulator mod (NULL: none), carrier car, into out (mono); from a fresh state */
static void voc_run(int bands, const fxs_params_t *q, const float *mod, const float *car, float *out, int n) {
    static voc_t v;
    memset(&v, 0, sizeof v);
    voc_init(&v, bands);
    float wl[128], wr[128];
    for (int at = 0; at < n; at += 128) {
        int m = n - at < 128 ? n - at : 128;
        voc_process(&v, q, mod ? mod + at : NULL, car + at, car + at, wl, wr, m);
        for (int i = 0; i < m; i++) out[at + i] = wl[i];
    }
}
static fxs_params_t voc_q(void) {
    fxs_params_t q;
    memset(&q, 0, sizeof q);
    q.tone = 1; q.sens = 0.5f; q.sibil = 0;
    return q;
}
static double band_energy_db(const float *x, int n, double lo, double hi) {   /* x[0..N) power between lo and hi Hz */
    (void)n;
    const double *p = spectrum(x);
    double e = 1e-30;
    for (int k = (int)(lo * N / SR); k <= (int)(hi * N / SR) && k < N / 2; k++) e += p[k];
    return 10 * log10(e);
}

enum { VOC_N = SR * 2 };
static float voc_car[VOC_N];
/* the vocoder's gain (dB) between lo and hi Hz: x's energy there against the carrier's (the same last N samples) */
static double voc_gain_db(const float *x, double lo, double hi) {
    double out = band_energy_db(x, N, lo, hi);
    return out - band_energy_db(voc_car + VOC_N - N, N, lo, hi);
}
static void vocoders(void) {
    static float mod[VOC_N], out[VOC_N], out2[VOC_N];
    float *car = voc_car;
    char what[300];
    uint32_t rng = 12345;
    for (int i = 0; i < VOC_N; i++) car[i] = 0.5f * fx_white(&rng);   /* a carrier with every frequency in it */
    /* bands: a band-limited modulator (a 1 kHz, then a 300 Hz, then a 3 kHz sine) puts the carrier's energy there */
    static const double FM[3] = {1000, 300, 3000};
    double leak[2] = {-1e9, -1e9};   /* each bank's worst leak 2.5-5x away */
    for (int b = 0; b < 2; b++) {
        int bands = b ? 16 : 10, good = 0;
        char row[200] = "";
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < VOC_N; i++) mod[i] = (float)(0.3 * sin(2 * M_PI * FM[k] * i / SR));
            fxs_params_t q = voc_q();
            voc_run(bands, &q, mod, car, out, VOC_N);
            const float *x = out + VOC_N - N;   /* the vocoder's gain, region by region: out against the carrier */
            double at = voc_gain_db(x, FM[k] / 1.25, FM[k] * 1.25);
            double below = voc_gain_db(x, FM[k] / 5, FM[k] / 2.5), above = voc_gain_db(x, FM[k] * 2.5, FM[k] * 5);
            /* (at: -9 dB allows a tone halfway between two bands, at their crossover) */
            good += at - below > (b ? 20 : 16) && at - above > (b ? 20 : 16) && at > -9 && at < 6;
            leak[b] = fmax(leak[b], fmax(below - at, above - at));
            snprintf(row + strlen(row), sizeof row - strlen(row), " %.0f Hz %+.1f dB, %+.0f / %+.0f;", FM[k], at, below - at,
                     above - at);
        }
        snprintf(what, sizeof what, "vocoder %d bands: a sine modulator passes a noise carrier at its band, not 2.5-5x "
                 "below / above it:%s", bands, row);
        check(good == 3, what);
    }
    snprintf(what, sizeof what, "vocoder: 16 bands are finer than 10 (their worst leak 2.5-5x away %.0f dB, 10 bands' "
             "%.0f dB)", leak[1], leak[0]);
    check(leak[1] < leak[0] - 3, what);
    {   /* silence in, silence out */
        fxs_params_t q = voc_q();
        q.noise = 1; q.sibil = 1;
        voc_run(16, &q, NULL, car, out, VOC_N);
        double peak0 = 0, peak1 = 0, peak2 = 0;
        for (int i = 0; i < VOC_N; i++) peak0 = fmax(peak0, fabs(out[i]));
        for (int i = 0; i < VOC_N; i++) mod[i] = i < SR / 2 ? (float)(0.3 * sin(2 * M_PI * 500 * i / SR)) : 0.0f;
        static voc_t v;   /* the modulator stops: the bands die away, then the output is exactly 0 */
        memset(&v, 0, sizeof v);
        voc_init(&v, 16);
        float wl[128], wr[128];
        int zero_from = -1;
        for (int at = 0; at < VOC_N; at += 128) {
            voc_process(&v, &q, at < SR / 2 ? mod + at : NULL, car + at, car + at, wl, wr, 128);
            int z = 1;
            for (int i = 0; i < 128; i++) z &= wl[i] == 0.0f && wr[i] == 0.0f;
            if (z && zero_from < 0 && at >= SR / 2) zero_from = at;
            if (!z) zero_from = -1;
        }
        q.noise = 0;   /* a modulator with a silent carrier: nothing to play */
        static const float none[VOC_N];
        for (int i = 0; i < VOC_N; i++) mod[i] = (float)(0.3 * sin(2 * M_PI * 500 * i / SR));
        q.sibil = 0;
        voc_run(16, &q, mod, none, out, VOC_N);
        for (int i = 0; i < VOC_N; i++) peak1 = fmax(peak1, fabs(out[i]));
        q.noise = 1;   /* ... but NOISE gives it a carrier of its own */
        voc_run(16, &q, mod, none, out2, VOC_N);
        for (int i = 0; i < VOC_N; i++) peak2 = fmax(peak2, fabs(out2[i]));
        snprintf(what, sizeof what, "vocoder: no modulator (with NOISE and SIBILANCE up) peak %.2g; after the modulator "
                 "stops the output is exactly 0 from %.2f s; a silent carrier peak %.2g, with NOISE %.3f", peak0,
                 zero_from / (double)SR - 0.5, peak1, peak2);
        check(peak0 == 0 && zero_from > 0 && zero_from < SR * 3 / 2 && peak1 < 1e-9 && peak2 > 0.01, what);
    }
    {   /* SENSITIVITY: a -60 dBFS modulator is gated at 0% and heard at 100% */
        for (int i = 0; i < VOC_N; i++) mod[i] = (float)(0.001 * sin(2 * M_PI * 700 * i / SR));
        double rms[2];
        for (int k = 0; k < 2; k++) {
            fxs_params_t q = voc_q();
            q.sens = k;
            voc_run(10, &q, mod, car, out, VOC_N);
            double e = 0;
            for (int i = VOC_N / 2; i < VOC_N; i++) e += out[i] * out[i];
            rms[k] = sqrt(e / (VOC_N / 2));
        }
        snprintf(what, sizeof what, "vocoder: a -60 dBFS modulator at SENSITIVITY 0%% rms %.2g (gated), at 100%% rms %.2g",
                 rms[0], rms[1]);
        check(rms[0] < 1e-6 && rms[1] > 3e-5, what);
    }
    {   /* SIBILANCE: a 9 kHz modulator (above the bands) passes only with it up; TONE darkens the top */
        for (int i = 0; i < VOC_N; i++) mod[i] = (float)(0.3 * sin(2 * M_PI * 9000 * i / SR));
        static const float none[VOC_N];
        double s[2];
        for (int k = 0; k < 2; k++) {
            fxs_params_t q = voc_q();
            q.sibil = k;
            voc_run(16, &q, mod, none, out, VOC_N);
            s[k] = fmax(-200.0, band_energy_db(out + VOC_N - N, N, 8800, 9200));
        }
        for (int i = 0; i < VOC_N; i++) mod[i] = (float)(0.3 * sin(2 * M_PI * 5000 * i / SR));
        double t[2];
        for (int k = 0; k < 2; k++) {
            fxs_params_t q = voc_q();
            q.tone = k ? 1.0f : 0.3f;
            voc_run(16, &q, mod, car, out, VOC_N);
            t[k] = band_energy_db(out + VOC_N - N, N, 4000, 6250);
        }
        snprintf(what, sizeof what, "vocoder: a 9 kHz modulator with SIBILANCE 100%% is %.0f dB louder than at 0%%; a "
                 "5 kHz band at TONE 30%% is %.1f dB under TONE 100%%", s[1] - s[0], t[1] - t[0]);
        check(s[1] - s[0] > 40 && t[1] - t[0] > 12, what);
    }
    {   /* bounded at the extremes: full-scale square modulator, saw carrier, NOISE, SIBILANCE, SENSITIVITY at 100% */
        double worst = 0;
        int bad = 0;
        for (int i = 0; i < VOC_N; i++) {
            mod[i] = (i / 50) % 2 ? 1.0f : -1.0f;
            car[i] = 2.0f * (float)fmod(i * 110.0 / SR, 1.0) - 1.0f;
        }
        for (int b = 0; b < 2; b++)
            for (int t = 0; t < 2; t++) {
                fxs_params_t q = voc_q();
                q.noise = 1; q.sibil = 1; q.sens = 1; q.tone = t;
                voc_run(b ? 16 : 10, &q, mod, car, out, VOC_N);
                for (int i = 0; i < VOC_N; i++) {
                    if (!isfinite(out[i])) bad++;
                    worst = fmax(worst, fabs(out[i]));
                }
            }
        snprintf(what, sizeof what, "vocoder: full-scale modulator and carrier, NOISE, SIBILANCE and SENSITIVITY at 100%%: "
                 "finite (%d not), peak %.2f", bad, worst);
        check(!bad && worst < 12, what);
    }
    {   /* through the engine: the input plays the voices as a synth effect, and on a master send */
        static float a[4][EXT_N];
        double rms[4];
        for (int k = 0; k < 4; k++) {   /* 0, 1: synth effect without / with input; 2, 3: master slot 1, the same */
            void *e = E->create(NULL);
            set(e, "mfx_on", k >= 2);
            if (k < 2) {
                set(e, "modfx_on", 1);
                set(e, "modfx_type", SFX_VOC16);
                set(e, "modfx_mix", 100);
            } else {
                set(e, "dly_type", MFX_VOC10);
                set(e, "mfx_send1", 100);
                set(e, "dly_mix", 100);
            }
            for (int n = 0; n < 3; n++) note(e, 1, 48 + 7 * n, 100);
            long t = 0;
            ext_render(e, a[k], EXT_N, k % 2 ? 700 : 0, 0.3, &t);
            rms[k] = ext_rms(a[k], EXT_N / 2, EXT_N);
            E->destroy(e);
        }
        for (int i = 0; i < EXT_N; i++) a[3][i] -= a[2][i];   /* the master slot's return: with input minus without */
        double ret = ext_rms(a[3], EXT_N / 2, EXT_N);
        snprintf(what, sizeof what, "vocoder in the engine: as the synth effect (mix 100%%) rms %.2g with no input, %.4f "
                 "with one; on master slot 1 (FX1 10 Band Vocoder, the dry voices %.4f) its return rms %.4f with an input",
                 rms[0], rms[1], rms[2], ret);
        check(rms[0] == 0 && rms[1] > 0.01 && rms[2] > 0.01 && ret > 0.005, what);
    }
}

/* (2026-10-30) SIBILANCE and SENSITIVITY as matrix destinations (the synth effect's and each master slot's, appended
 * at the very end of the destination list): named in every destination list; a Constant slot at +100% with the knob
 * at 0 plays as the knob at 100% (the same samples once the effect modulation's smoothing has settled), and the knob at
 * 0 alone sounds different (the vocoder's part: a master slot's dry voices taken off). CTRL 2 on Auto (SIBILANCE for the vocoders) moved by the matrix: finite, and it changes
 * the sound (before this, that path wrote outside the effect pass's dirty bits). The input: a 300 Hz tone and hiss,
 * so both the bands and the sibilance filter hear something (for SENSITIVITY, the gate, a quiet one). */
static void voc_dst_render(void *e, float *dst, int frames, double g) {   /* g: the input's gain */
    float in[256], out[256];
    uint32_t rng = 12345;
    long t = 0;
    for (int at = 0; at < frames; at += 128) {
        for (int i = 0; i < 128; i++, t++) {
            rng = rng * 1664525u + 1013904223u;
            in[2 * i] = in[2 * i + 1] = (float)(g * (0.2 * sin(2 * M_PI * 300 * t / SR) + 0.15 * ((rng >> 8) / 8388608.0 - 1.0)));
        }
        one_ish_set_input(e, in, 128);
        one_ish_render_float(e, out, 128);
        for (int i = 0; i < 128 && at + i < frames; i++) dst[at + i] = out[2 * i];
    }
}
static void *voc_dst_patch(int place, const char *knob, double v) {   /* place 0 the synth effect, 1 / 2 a master slot */
    void *e = E->create(NULL);
    set(e, "mfx_on", place > 0);
    if (place == 0) { set(e, "modfx_on", 1); set(e, "modfx_type", SFX_VOC16); set(e, "modfx_mix", 100); }
    else if (place == 1) { set(e, "dly_type", MFX_VOC16); set(e, "mfx_send1", 100); set(e, "mfx_send2", 0); set(e, "dly_mix", 100); }
    else { set(e, "rev_type", MFX_VOC16); set(e, "mfx_send2", 100); set(e, "mfx_send1", 0); set(e, "rev_mix", 100); }
    set(e, knob, v);
    for (int n = 0; n < 3; n++) note(e, 1, 48 + 7 * n, 100);
    return e;
}
static double voc_diff_db(const float *a, const float *b, int from, int to) {   /* the difference against a's level */
    double d = 0, s = 0;
    for (int i = from; i < to; i++) { d += (double)(a[i] - b[i]) * (a[i] - b[i]); s += (double)a[i] * a[i]; }
    return 10 * log10(d / (s + 1e-30) + 1e-30);
}
static void voc_matrix(void) {
    char what[300];
    static const char *const NAME[6] = {"Synth FX Sibilance", "Synth FX Sensitivity", "FX1 Sibilance", "FX1 Sensitivity",
                                        "FX2 Sibilance", "FX2 Sensitivity"};
    static const char *const KNOB[6] = {"modfx_sibil", "modfx_sens", "dly_sibil", "dly_sens", "rev_sibil", "rev_sens"};
    int lists = 0, named = 0;
    for (int i = 0; i < NPARAMS; i++)
        if (PARAMS[i].nopts == MD_COUNT) {
            lists++;
            int ok = 1;
            for (int k = 0; k < 6; k++) ok &= !strcmp(PARAMS[i].opts[md_of_fxk(MDF_VOC_SIBIL + k)], NAME[k]);
            named += ok;
        }
    snprintf(what, sizeof what, "vocoder destinations: the six at the end of the list (%d .. %d of %d), named in %d of %d "
             "destination lists", md_of_fxk(MDF_VOC_SIBIL), md_of_fxk(MDF_F2_SENS), MD_COUNT, named, lists);
    check(lists == 51 && named == lists && md_of_fxk(MDF_F2_SENS) == MD_COUNT - 1, what);
    enum { VN = SR };
    static float a[VN], b[VN], c[VN], d[VN];
    for (int k = 0; k < 6; k++) {
        int place = k / 2;
        double g = k % 2 ? 0.005 : 1.0;   /* SENSITIVITY: a quiet input (-50 dBFS or so), which the gate shuts at 0% */
        void *e = voc_dst_patch(place, KNOB[k], 100);
        voc_dst_render(e, a, VN, g);
        E->destroy(e);
        e = voc_dst_patch(place, KNOB[k], 0);
        slot_set(e, 2, MS_CONST, md_of_fxk(MDF_VOC_SIBIL + k), 100);
        voc_dst_render(e, b, VN, g);
        E->destroy(e);
        e = voc_dst_patch(place, KNOB[k], 0);
        voc_dst_render(e, c, VN, g);
        E->destroy(e);
        e = voc_dst_patch(place, KNOB[k], 100);   /* no input: the dry voices alone (a master slot's), taken off */
        voc_dst_render(e, d, VN, 0.0);
        E->destroy(e);
        for (int i = 0; i < VN; i++) { a[i] -= d[i]; b[i] -= d[i]; c[i] -= d[i]; }
        double same = voc_diff_db(a, b, VN / 2, VN), apart = voc_diff_db(a, c, VN / 2, VN), lvl = ext_rms(a, VN / 2, VN);
        snprintf(what, sizeof what, "vocoder destination %-20s: knob 0 + Constant +100%% against the knob at 100%% %.1f dB "
                 "apart; the knob at 0 alone %.1f dB (rms %.3g)", NAME[k], same, apart, lvl);
        check(same < -60 && apart > -40 && lvl > 1e-5, what);
    }
    {   /* CTRL 2 on Auto (the vocoders' SIBILANCE) moved by a Constant slot */
        for (int k = 0; k < 2; k++) {
            void *e = voc_dst_patch(0, "modfx_sibil", 0);
            if (k) slot_set(e, 2, MS_CONST, MD_FX_FIRST + MDF_CTRL2, 100);
            voc_dst_render(e, k ? b : a, VN, 1.0);
            E->destroy(e);
        }
        int bad = 0;
        for (int i = 0; i < VN; i++) bad += !isfinite(b[i]);
        double apart = voc_diff_db(a, b, VN / 2, VN);
        snprintf(what, sizeof what, "vocoder: CTRL 2 (Auto: SIBILANCE) moved by the matrix: finite (%d not), %.1f dB from "
                 "the unmoved sound", bad, apart);
        check(!bad && apart > -40, what);
    }
}

static void voc_bench(void) {
    static float mod[SR], car[SR], out[SR];
    uint32_t rng = 99;
    for (int i = 0; i < SR; i++) { mod[i] = 0.3f * fx_white(&rng); car[i] = 0.3f * fx_white(&rng); }
    for (int b = 0; b < 2; b++) {
        fxs_params_t q = voc_q();
        q.sibil = 0.3f; q.noise = 0.2f;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < 10; r++) voc_run(b ? 16 : 10, &q, mod, car, out, SR);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) * 1e-3) / (10.0 * SR / 128);
        printf("bench: synth FX %d band vocoder %6.2f us per 128-frame block\n", b ? 16 : 10, us);
    }
}

/* a speech-like modulator, synthetic: a glottal buzz (120 Hz with a little vibrato and a falling pitch per syllable)
 * through three formant resonators gliding between vowels, with a hiss burst for each "s"; 0.3 peak */
static void voc_speech(float *x, int n) {
    static const float F[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410},
                                  {300, 870, 2240}};   /* a e i o u */
    static const int seq[] = {0, 2, 3, 1, 4, 0, 3, 2};   /* syllables: their vowels */
    const int syl = SR * 3 / 8, nsyl = (int)(sizeof seq / sizeof seq[0]);
    double ph = 0, st[3][2] = {{0}};
    uint32_t rng = 777;
    for (int i = 0; i < n; i++) {
        int k = (i / syl) % nsyl, pos = i % syl;
        double u = (double)pos / syl, f0 = 130 - 25 * u + 3 * sin(2 * M_PI * 5.5 * i / SR);
        ph += f0 / SR;
        double pulse = 0;
        if (ph >= 1) { ph -= 1; pulse = 1; }   /* a pulse per period: rich in harmonics */
        double env = u < 0.1 ? u / 0.1 : u > 0.75 ? fmax(0, (0.95 - u) / 0.2) : 1;   /* the syllable's shape */
        double v = 0;
        const float *a = F[seq[k]], *b = F[seq[(k + 1) % nsyl]];
        for (int f = 0; f < 3; f++) {   /* two-pole resonators, gliding toward the next vowel */
            double fr = a[f] + (b[f] - a[f]) * u * u, bw = 80 + 40 * f, r = exp(-M_PI * bw / SR);
            double c1 = 2 * r * cos(2 * M_PI * fr / SR), c2 = -r * r;
            double y = pulse * (1 - r) * 4 + c1 * st[f][0] + c2 * st[f][1];
            st[f][1] = st[f][0];
            st[f][0] = y;
            v += y / (f + 1);
        }
        double hiss = 0;   /* an "s" at the end of every other syllable */
        if (k % 2 == 1 && u > 0.8) hiss = 0.25 * fx_white(&rng) * sin(M_PI * (u - 0.8) / 0.2);
        x[i] = (float)(0.3 * fmax(-1, fmin(1, v * env * 1.5)) + hiss * 0.3);
    }
}

/* test/out/vocoder_demo.wav: the speech-like modulator alone, then playing a held chord through the 16 band vocoder
 * (the synth effect, mix 100%), then the 10 band one, then the 16 band with NOISE and more SIBILANCE */
static void voc_wav(const char *demo_path) {
    enum { LEN = SR * 3 };
    static float mod[LEN], buf[256];
    static int16_t pcm[2 * 4 * (LEN + SR / 2 + 128)];
    char dir[256], path[320];
    snprintf(dir, sizeof dir, "%s", demo_path);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    voc_speech(mod, LEN);
    int16_t *o = pcm;
    for (int i = 0; i < LEN + SR / 2; i++, o += 2) o[0] = o[1] = (int16_t)(i < LEN ? mod[i] * 32767 : 0);
    for (int pass = 0; pass < 3; pass++) {
        void *e = E->create(NULL);
        set(e, "o2_level", 80); set(e, "o2_freq", 7); set(e, "noise_level", 0);
        set(e, "voices", 7); set(e, "amp_a", 5); set(e, "amp_s", 100); set(e, "amp_r", 40);
        set(e, "modfx_on", 1); set(e, "modfx_type", pass == 1 ? SFX_VOC10 : SFX_VOC16); set(e, "modfx_mix", 100);
        set(e, "modfx_gain", 70);
        if (pass == 2) { set(e, "modfx_noise", 40); set(e, "modfx_sibil", 70); }
        set(e, "mfx_send2", 20);
        static const int chord[4] = {45, 52, 57, 60};
        for (int k = 0; k < 4; k++) note(e, 1, chord[k], 100);
        for (int at = 0; at < LEN + SR / 2; at += 128) {
            float in[256];
            for (int i = 0; i < 128; i++) in[2 * i] = in[2 * i + 1] = at + i < LEN ? mod[at + i] : 0.0f;
            one_ish_set_input(e, in, 128);
            if (at + 128 > LEN) for (int k = 0; k < 4; k++) note(e, 0, chord[k], 0);
            one_ish_render_float(e, buf, 128);
            for (int i = 0; i < 128; i++, o += 2) {
                o[0] = (int16_t)(buf[2 * i] * 32767);
                o[1] = (int16_t)(buf[2 * i + 1] * 32767);
            }
        }
        E->destroy(e);
    }
    snprintf(path, sizeof path, "%svocoder_demo.wav", dir);
    uint32_t frames = (uint32_t)((o - pcm) / 2);
    write_wav(path, pcm, frames);
    printf("wrote %s (%.1f s: a synthetic voice alone, then playing a chord through the 16 and 10 band vocoders, then "
           "16 bands with noise and sibilance)\n", path, frames / (double)SR);
}
