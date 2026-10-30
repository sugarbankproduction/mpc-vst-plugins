/* (2026-10-30) HQ MODE and WARM-UP (the ANALOG page): included by offline_check.c (`offline_check hq` runs these alone).
 *
 * HQ MODE runs the voice's oscillators and filters at 2x or 4x the rate and decimates (src/hq.h). Measured here:
 *   - aliasing (the power that is not a harmonic of the note, against the whole, alias_db) of a hard-synced saw, of
 *     linear FM and of a triangle through the driven non-linear ladder, at high notes, Off against 2x and 4x
 *   - the note's own level and pitch are the same in every mode (the decimators are flat to 20 kHz)
 *   - Off is exactly as before: an instance that had HQ (and WARM-UP) on and off again gives a fresh one's samples
 *   - no NaN, infinity or denormal at extreme settings, 16 voices, 4x; the state keeps the four params
 * WARM-UP: at 100% with a 1-minute time and no DRIFT, a held note starts off pitch and wanders, and by the time it is
 * at the note again (within 0.5 cent) and still; the same on every run (fixed seed); turning it off and on starts it
 * again. Then the CPU per voice per block (8 and 16 voices) in each mode. */

void one_ish_render_float(void *inst, float *out_lr, int frames);

static void hq_render(void *e, float *x, int frames) {   /* the left channel, as floats from the engine */
    static float lr[256];
    for (int at = 0; at < frames; at += 128) {
        int n = frames - at < 128 ? frames - at : 128;
        one_ish_render_float(e, lr, n);
        for (int i = 0; i < n; i++) x[at + i] = lr[2 * i];
    }
}

static const char *const HQ_NAME[3] = {"Off", "2x", "4x"};

/* the patch for one measurement: kind 0 hard sync, 1 linear FM, 2 the driven ladder; hq 0/1/2 */
static void *hq_patch(int kind, int hq) {
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "voices", 0);   /* (Mono) */
    set(e, "flt_cut", 100);
    if (kind == 0) { set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "o2_sync", 1); set(e, "o2_freq", 7); }
    if (kind == 1) {   /* (a triangle modulator, 40%: the carrier never stops, so the sound stays harmonic) */
        set_shape(e, "o1", TRI);
        set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "fm_amt", 40);
    }
    if (kind == 2) {
        set_shape(e, "o1", TRI);
        set(e, "flt_drive", 100); set(e, "an_ladder", 100); set(e, "flt_res", 40); set(e, "flt_cut", 90);
    }
    set(e, "an_hq", hq);
    return e;
}

static void hq_measure(int kind, int note_n, int hq, double *alias, double *fund_db, double *hz) {
    static float x[N + 8192];
    void *e = hq_patch(kind, hq);
    note(e, 1, note_n, 127);
    hq_render(e, x, N + 8192);
    double f0 = 440 * pow(2, (note_n - 69) / 12.0);
    *alias = alias_db(x + 8192, f0);
    *fund_db = band_db(x + 8192, f0);
    *hz = peak_hz(x + 8192);
    E->destroy(e);
}

static void hq_aliasing(void) {
    static const char *const KIND[3] = {"hard sync (osc 2 +7 st, saw)", "linear FM 40% (osc 1 triangle > osc 2 saw)",
                                        "triangle into the ladder, drive 100%, non-linear 100%"};
    static const int NOTES[3][3] = {{84, 96, 103}, {84, 91, 96}, {91, 96, 100}};
    char what[300];
    for (int k = 0; k < 3; k++)
        for (int n = 0; n < 3; n++) {
            double a[3], f[3], hz[3];
            for (int q = 0; q < 3; q++) hq_measure(k, NOTES[k][n], q, &a[q], &f[q], &hz[q]);
            snprintf(what, sizeof what, "HQ %s, note %d: aliasing Off %.1f dB, 2x %.1f dB, 4x %.1f dB; the note's level "
                     "%+.2f / %+.2f dB and pitch %+.2f / %+.2f cents from Off", KIND[k], NOTES[k][n], a[0], a[1], a[2],
                     f[1] - f[0], f[2] - f[0], cents(hz[1], hz[0]), cents(hz[2], hz[0]));
            /* guards from the first measurement (2026-10-30): 2x at least 8 dB under Off, 4x at least as low as 2x. The
             * driven non-linear ladder comes out up to about 1 dB quieter oversampled: its state-evaluated stages are
             * closer to the circuit at the higher rate (they compress a little more) */
            double tol = k == 2 ? 1.5 : 0.5;
            check(a[1] < a[0] - 8 && a[2] < a[1] + 1 && fabs(f[1] - f[0]) < tol && fabs(f[2] - f[0]) < tol &&
                  fabs(cents(hz[1], hz[0])) < 1 && fabs(cents(hz[2], hz[0])) < 1, what);
        }
}

/* Off is the old engine: an instance that had HQ and WARM-UP on (and played with them) and off again plays a fresh
 * instance's samples; and HQ on gives other samples (so the check above can't pass by HQ doing nothing) */
static void hq_off_exact(void) {
    static float a[SR], b[SR];
    void *x = hq_patch(0, 0), *y = hq_patch(0, 2);
    set(y, "an_warm", 1);
    for (int k = 0; k < 2; k++) {   /* the same history, y with HQ 4x and WARM-UP on */
        void *e = k ? y : x;
        set(e, "drift", 30);
        note(e, 1, 60, 100);
        hq_render(e, b, SR / 4);
        note(e, 0, 60, 0);
        hq_render(e, b, SR);   /* (the release ends: the voice is idle again) */
    }
    set(y, "an_hq", 0); set(y, "an_warm", 0);
    int differ = 0;
    {
        void *z = hq_patch(0, 1), *o = hq_patch(0, 0);   /* fresh, 2x and Off */
        note(z, 1, 72, 100); note(o, 1, 72, 100);
        hq_render(z, a, SR / 4); hq_render(o, b, SR / 4);
        for (int i = 0; i < SR / 4; i++) differ += a[i] != b[i];
        E->destroy(z); E->destroy(o);
    }
    int same = 1;
    for (int k = 0; k < 4; k++) { note(x, 1, 60 + 5 * k, 90); note(y, 1, 60 + 5 * k, 90); }
    hq_render(x, a, SR); hq_render(y, b, SR);
    for (int i = 0; i < SR; i++) same &= a[i] == b[i];
    char what[200];
    snprintf(what, sizeof what, "HQ MODE and WARM-UP Off (after both were on): a fresh instance's samples (%s); HQ 2x "
             "differs (%d of %d samples)", same ? "yes" : "no", differ, SR / 4);
    check(same && differ > SR / 8, what);
    E->destroy(x); E->destroy(y);
}

/* extremes: 16 voices of everything at once at 4x; finite, within the limiter, no denormals as they die away */
static void hq_extremes(void) {
    static float x[SR * 3];
    int bad = 0, sub = 0;
    float peak = 0;
    for (int q = 1; q < 3; q++) {
        void *e = hq_patch(0, q);
        set(e, "voices", 15); set(e, "fm_amt", 100); set(e, "o3_level", 100); set(e, "o3_sync", 1); set(e, "o1_level", 100);
        set(e, "flt_res", 100); set(e, "flt_drive", 100); set(e, "an_ladder", 100); set(e, "lad_fm", 100);
        set(e, "svf_fm", 100); set(e, "o1_svf", 1); set(e, "svf_res", 100); set(e, "an_warm", 1); set(e, "an_warm_amt", 100);
        set(e, "drift", 100);
        for (int k = 0; k < 16; k++) note(e, 1, 112 + k, 127);
        hq_render(e, x, SR);
        for (int k = 0; k < 16; k++) note(e, 0, 112 + k, 0);
        hq_render(e, x + SR, SR * 2);
        for (int i = 0; i < SR * 3; i++) {
            bad += !isfinite(x[i]);
            sub += x[i] != 0.0f && fpclassify(x[i]) == FP_SUBNORMAL;
            if (fabsf(x[i]) > peak) peak = fabsf(x[i]);
        }
        E->destroy(e);
    }
    char what[200];
    snprintf(what, sizeof what, "HQ 2x and 4x at extremes (16 voices, top notes, FM, sync, filter FM, full resonance and "
             "drive, warm-up): %d non-finite, %d denormal samples, peak %.2f", bad, sub, peak);
    check(bad == 0 && sub == 0 && peak <= 1.0f, what);
}

static void hq_state(void) {
    static char st[VST2_CHUNK_MAX + 1000];
    static float a[SR], b[SR];
    void *x = hq_patch(2, 1);
    set(x, "an_warm", 1); set(x, "an_warm_amt", 70); set(x, "an_warm_time", 3);
    int n = E->get_param(x, "state", st, sizeof st);
    void *y = E->create(NULL);
    E->set_param(y, "state", st);
    E->destroy(x);
    x = E->create(NULL);
    E->set_param(x, "state", st);   /* (both fresh: both warm-up clocks start now) */
    int same = n > 0 && get(y, "an_hq") == 1 && get(y, "an_warm") == 1 && get(y, "an_warm_amt") == 70 &&
               get(y, "an_warm_time") == 3;
    note(x, 1, 96, 120); note(y, 1, 96, 120);
    hq_render(x, a, SR); hq_render(y, b, SR);
    for (int i = 0; i < SR; i++) same &= a[i] == b[i];
    check(same, "HQ MODE and WARM-UP are saved with the sound: a restored state has them and plays the same samples");
    E->destroy(x); E->destroy(y);
}

/* the pitch of a held A5 (osc 1, a saw) at times t (s): cents from 880 Hz */
static void warm_pitches(void *e, const double *t, int n, double *c) {
    static float x[N];
    double now = 0;
    for (int k = 0; k < n; k++) {
        int skip = (int)((t[k] - now) * SR);
        while (skip > 0) { int m = skip > N ? N : skip; hq_render(e, x, m); skip -= m; }
        hq_render(e, x, N);
        now = t[k] + (double)N / SR;
        c[k] = cents(peak_hz(x), 880);
    }
}

static void *warm_patch(int on) {
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "voices", 0);
    set(e, "an_warm_time", 1);
    set(e, "an_warm_amt", 100);
    set(e, "an_warm", on);
    return e;
}

static void warm_checks(void) {
    char what[400];
    enum { NT = 12 };
    static const double T[NT] = {0.2, 0.6, 1.0, 1.4, 1.8, 2.2, 30, 60, 90, 90.4, 90.8, 91.2};
    double ref[NT], c[NT], c2[NT];
    void *r = warm_patch(0), *w = warm_patch(1), *w2 = warm_patch(1);
    note(r, 1, 81, 100); note(w, 1, 81, 100); note(w2, 1, 81, 100);
    warm_pitches(r, T, NT, ref);
    warm_pitches(w, T, NT, c);
    warm_pitches(w2, T, NT, c2);
    double early_lo = 1e9, early_hi = -1e9, late_lo = 1e9, late_hi = -1e9, early = 0;
    for (int k = 0; k < 6; k++) {
        early_lo = fmin(early_lo, c[k] - ref[k]); early_hi = fmax(early_hi, c[k] - ref[k]);
        early += fabs(c[k] - ref[k]) / 6;
    }
    for (int k = 8; k < NT; k++) { late_lo = fmin(late_lo, c[k] - ref[k]); late_hi = fmax(late_hi, c[k] - ref[k]); }
    int same = 1;
    for (int k = 0; k < NT; k++) same &= c[k] == c2[k];
    snprintf(what, sizeof what, "WARM-UP 100%%, 1 minute, DRIFT 0: a held A5 sits %.1f cents off on average over its first "
             "2 s and wanders %.1f cents there; at 30 s %+.1f, 60 s %+.1f; after 90 s within %+.2f .. %+.2f cents and still "
             "(%.2f); off: %+.2f cents; the same on a second run (%s)",
             early, early_hi - early_lo, c[6] - ref[6], c[7] - ref[7], late_lo, late_hi, late_hi - late_lo, ref[0],
             same ? "yes" : "no");
    check(early > 4 && early_hi - early_lo > 1.0 && fabs(c[7] - ref[7]) < 0.2 * early + 0.5 &&
          fabs(late_lo) < 0.5 && fabs(late_hi) < 0.5 && late_hi - late_lo < 0.3 && same, what);
    /* off and on again: cold again */
    set(w, "an_warm", 0);
    hq_render(w, (float[128]){0}, 128);
    set(w, "an_warm", 1);
    double again[6], av = 0;
    warm_pitches(w, T, 6, again);
    for (int k = 0; k < 6; k++) av += fabs(again[k] - ref[NT - 1]) / 6;
    snprintf(what, sizeof what, "WARM-UP off and on again starts a new warm-up: %.1f cents off on average over its "
             "first 2 s (the first time: %.1f)", av, early);
    check(av > 0.5 * early && av > 3, what);
    E->destroy(r); E->destroy(w); E->destroy(w2);
    /* the cutoffs: the ladder self-oscillating (no oscillator) at 1 kHz starts off and settles */
    void *f = warm_patch(1), *g = warm_patch(0);
    for (int k = 0; k < 2; k++) {
        void *e = k ? g : f;
        set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(1000));
        note(e, 1, 60, 100);
    }
    static float x[N];
    hq_render(f, x, N); double f0 = cents(peak_hz(x), 1000);
    hq_render(g, x, N); double g0 = cents(peak_hz(x), 1000);
    snprintf(what, sizeof what, "WARM-UP moves the cutoffs too: a self-oscillating ladder sits %+.0f cents from where it "
             "is with warm-up off", f0 - g0);
    check(fabs(f0 - g0) > 3, what);
    E->destroy(f); E->destroy(g);
}

static void hq_checks(void) {
    hq_off_exact();
    hq_aliasing();
    hq_extremes();
    hq_state();
    warm_checks();
}

/* CPU per block: 8 and 16 voices of a busy patch (three saws, sync, the ladder; and with the non-linear ladder) */
static void hq_bench(void) {
    for (int nl = 0; nl < 2; nl++)
        for (int nv = 8; nv <= 16; nv += 8)
            for (int q = 0; q < 4; q++) {   /* q 3: Off with WARM-UP on */
                void *e = hq_patch(0, q == 3 ? 0 : q);
                set(e, "voices", nv - 1);
                set(e, "o1_level", 80); set(e, "o3_level", 60); set(e, "flt_cut", 70); set(e, "flt_res", 40);
                set(e, "an_ladder", nl * 100);
                if (q == 3) set(e, "an_warm", 1);
                for (int k = 0; k < nv; k++) note(e, 1, 48 + 3 * k, 100);
                int16_t buf[256];
                for (int i = 0; i < 200; i++) E->render(e, buf, 128);
                double best = 1e9;
                for (int r = 0; r < 5; r++) {
                    struct timespec t0, t1;
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                    for (int i = 0; i < 2000; i++) E->render(e, buf, 128);
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 2000;
                    if (us < best) best = us;
                }
                printf("bench HQ %-3s%s, %2d voices%s: %.1f us per 128-frame block, %.2f us per voice\n",
                       q == 3 ? "Off" : HQ_NAME[q], q == 3 ? " + WARM-UP" : "", nv, nl ? ", non-linear ladder" : "",
                       best, best / nv);
                E->destroy(e);
            }
}
