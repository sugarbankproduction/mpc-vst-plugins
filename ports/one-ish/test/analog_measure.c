/* (2026-11-02) The measuring kit (docs/ANALOG_RESEARCH.md section 5): numbers to judge the analog modelling by, and a
 * slow reference model to hold them against. `offline_check measure` prints it all; test/run.sh runs it with the O2
 * build. Included from offline_check.c after hq_check.c and analog_check.c (their helpers: patch, hq_render, hq_measure).
 * It changes no sound and the plugin never runs any of this.
 *
 * The filters under test are driven directly (src/ladder.h, src/svf.h, at 44.1 kHz, one control step = 16 samples, as
 * the voice does), so nothing else in the voice colours the numbers; aliasing and the null against 4x go through the
 * whole engine.
 *
 * THE REFERENCE MODEL: the published transistor ladder equations with perfectly matched parts (A. Huovilainen,
 * his non-linear digital ladder paper, DAFx 2004, eq. 10-17, and T. Stinchcombe's analysis; titles in the report),
 * in voltages scaled by the transistors' 2 Vt:
 *     y1' = wc (tanh(u - k y4) - tanh(y1))     yi' = wc (tanh(y(i-1)) - tanh(yi))     i = 2, 3, 4
 * solved by fourth-order Runge-Kutta in doubles, MZ_REF_OS times the sample rate per sample (16: about 700 kHz; the
 * convergence line prints how little 32 changes). Slow and only here. It is the truth of the EQUATIONS, not of any
 * instrument: real parts are not matched, and a capture of real hardware would be the next step (section 5).
 * The scale between the two is a choice, printed with the tables: the non-linear ladder's stages flatten at 1 / 2.5 of
 * its internal level, and its input comes in at 0.25, so an input x is u = 0.625 x and y4 = 1 comes out as 1.6. The
 * reference has no bass make-up (ours gives half of the loss back, ladder_set). A mismatch below is a finding to report,
 * not something to fix on sight.
 *
 * What it prints (section 5's table):
 *   self-oscillation  full resonance (k 4.4), 20 Hz - 15 kHz: cents against the cutoff, level, purity (harmonics against
 *                     the fundamental)
 *   onset             the k at which oscillation starts (theory: 4)
 *   level vs k        a 110 Hz saw at 1 kHz, k 0 .. 4: rms and fundamental against k 0, beside 1/(1+k)
 *   harmonics         a 200 Hz sine, -40 .. +12 dB, resonance 0 / 50 / 90%: gain, 2nd, 3rd and 5th
 *   compression       a sine at the cutoff, resonance 90%: output against input
 *   aliasing          alias_db through the engine, the driven non-linear ladder at high notes (hq_measure)
 *   null against 4x   a held saw at 1x and 4x, level and delay matched: the residual
 *   sweep             a slow resonant sweep over a saw: how often the resonance locks onto a harmonic (Parker et al.'s
 *                     "steppy" effect) and how far it strays from the cutoff
 *   cutoff steps      a fast resonant envelope with the cutoff set every 16 samples against every sample (report item 8:
 *                     below -70 dB, drop that idea) */

#define MZ_REF_OS 16     /* the reference's steps per sample */
#define MZ_FR 44100      /* samples per second, as a number for loops */
#define MZ_STEP 16       /* a control step */
enum { MZ_REF_U_NUM = 5, MZ_REF_U_DEN = 8 };   /* u = 0.625 x, see the top */
#define MZ_REF_OUT 1.6

/* ---- signals: a sine, or a band-limited saw from one table, at any time t (so the reference can ask between samples) ---- */
enum { MZ_SILENCE, MZ_SINE, MZ_SAW };
typedef struct { int kind; double hz, amp; } mz_sig_t;
enum { MZ_WT = 1 << 15 };
static float mz_saw_tab[MZ_WT + 1];
static double mz_saw_hz;
static void mz_saw_make(double hz) {   /* a -1 .. 1 saw's harmonics up to 20 kHz */
    if (mz_saw_hz == hz) return;
    mz_saw_hz = hz;
    int nh = (int)(20000.0 / hz);
    for (int i = 0; i <= MZ_WT; i++) {
        double s = 0, t = 2 * M_PI * i / MZ_WT;
        for (int h = 1; h <= nh; h++) s += sin(h * t) / h;
        mz_saw_tab[i] = (float)(s * 2 / M_PI);
    }
}
static double mz_sig(const mz_sig_t *s, double t) {
    if (s->kind == MZ_SINE) return s->amp * sin(2 * M_PI * s->hz * t);
    if (s->kind == MZ_SAW) {
        double ph = s->hz * t;
        ph = (ph - floor(ph)) * MZ_WT;
        int i = (int)ph;
        return s->amp * (mz_saw_tab[i] + (ph - i) * (mz_saw_tab[i + 1] - mz_saw_tab[i]));
    }
    return 0.0;
}

/* ---- the reference ---- */
typedef struct { double y[4], wc, k; int os; } mz_ref_t;
static inline void mz_ref_d(const mz_ref_t *r, const double *y, double u, double *d) {
    double t0 = tanh(y[0]), t1 = tanh(y[1]), t2 = tanh(y[2]), t3 = tanh(y[3]);
    d[0] = r->wc * (tanh(u - r->k * y[3]) - t0);
    d[1] = r->wc * (t0 - t1);
    d[2] = r->wc * (t1 - t2);
    d[3] = r->wc * (t2 - t3);
}
/* one output sample at time t (seconds): os Runge-Kutta steps. Returns y4 in the voice's output units */
static double mz_ref_tick(mz_ref_t *r, const mz_sig_t *s, double t) {
    double h = 1.0 / (MZ_FR * (double)r->os), u = (double)MZ_REF_U_NUM / MZ_REF_U_DEN;
    for (int n = 0; n < r->os; n++) {
        double tn = t + n * h, a[4], b[4], c[4], d[4], y[4];
        double u0 = u * mz_sig(s, tn), u1 = u * mz_sig(s, tn + 0.5 * h), u2 = u * mz_sig(s, tn + h);
        mz_ref_d(r, r->y, u0, a);
        for (int i = 0; i < 4; i++) y[i] = r->y[i] + 0.5 * h * a[i];
        mz_ref_d(r, y, u1, b);
        for (int i = 0; i < 4; i++) y[i] = r->y[i] + 0.5 * h * b[i];
        mz_ref_d(r, y, u1, c);
        for (int i = 0; i < 4; i++) y[i] = r->y[i] + h * c[i];
        mz_ref_d(r, y, u2, d);
        for (int i = 0; i < 4; i++) r->y[i] += h / 6 * (a[i] + 2 * b[i] + 2 * c[i] + d[i]);
    }
    return r->y[3] * MZ_REF_OUT;
}

/* ---- one filter under test, whichever it is ---- */
enum { MZ_LAD, MZ_NL, MZ_REF, MZ_SVF, MZ_SVFN, MZ_KINDS };   /* MZ_SVFN: NON-LINEAR SVF at 100% */
static const char *const MZ_NAME[MZ_KINDS] = {"linear ladder", "non-linear ladder", "reference", "SVF", "non-linear SVF"};
typedef struct {
    int kind;
    ladder_t l;
    svf2_t s;
    mz_ref_t r;
} mz_flt_t;

/* res: the knob, 0 .. 1 (the ladder's k = 4.4 res; the reference's the same) */
static void mz_set(mz_flt_t *f, double hz, double res) {
    if (f->kind == MZ_LAD || f->kind == MZ_NL) {
        ladder_set(&f->l, (float)hz, (float)res, 0.0f, LADDER_LP, LADDER_24DB, (float)MZ_FR);
        f->l.nl = f->kind == MZ_NL ? 1.0f : 0.0f;
    } else if (f->kind == MZ_REF) {
        f->r.wc = 2 * M_PI * hz;
        f->r.k = LADDER_K_MAX * res;
    } else {
        svf2_set(&f->s, (float)hz, 0.0f, (float)res, SVF_LP, SVF_PARALLEL, (float)MZ_FR);
        f->s.nl = SVF_NL_C;
    }
}
static void mz_init(mz_flt_t *f, int kind, double hz, double res) {
    memset(f, 0, sizeof *f);
    f->kind = kind;
    f->r.os = MZ_REF_OS;
    mz_set(f, hz, res);
}
static void mz_seed(mz_flt_t *f, double v) {   /* a start for an oscillation, as the voice's noise floor would give (faster) */
    f->l.s[0] = (float)v;
    f->s.a.ic1 = (float)v;
    f->r.y[0] = v;
}
static double mz_tick(mz_flt_t *f, const mz_sig_t *s, double t) {
    if (f->kind == MZ_REF) return mz_ref_tick(&f->r, s, t);
    float x = (float)mz_sig(s, t);
    if (f->kind == MZ_LAD) return ladder_next(&f->l, x);
    if (f->kind == MZ_NL) return ladder_next_nl(&f->l, x);
    if (f->kind == MZ_SVFN) return svf2_next_nl(&f->s, x, 1.0f);
    return svf2_next(&f->s, x);
}
static void mz_run(mz_flt_t *f, const mz_sig_t *s, float *out, int n) {
    for (int i = 0; i < n; i++) out[i] = (float)mz_tick(f, s, (double)i / MZ_FR);
}

/* ---- measures ---- */
static double mz_db(double a) { return 20 * log10(a + 1e-12); }
static double mz_rms(const float *x, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / n);
}
/* the amplitude of the tone at hz (Hann window; far from anything else, so no leakage to speak of) */
static double mz_amp(const float *x, int n, double hz) {
    double re = 0, im = 0, sw = 0, w = 2 * M_PI * hz / MZ_FR;
    for (int i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        re += win * x[i] * cos(w * i);
        im += win * x[i] * sin(w * i);
        sw += win;
    }
    return 2 * sqrt(re * re + im * im) / sw;
}
/* the frequency: rising zero crossings, interpolated, from the first to the last (no DC filter: the mean comes off first) */
static double mz_freq(const float *x, int n) {
    double m = 0, first = -1, last = -1;
    int count = 0;
    for (int i = 0; i < n; i++) m += x[i];
    m /= n;
    for (int i = 1; i < n; i++) {
        double a = x[i - 1] - m, b = x[i] - m;
        if (a < 0 && b >= 0) {
            double at = i - 1 + a / (a - b);
            if (first < 0) first = at;
            last = at;
            count++;
        }
    }
    return count > 1 ? (count - 1) * (double)MZ_FR / (last - first) : 0.0;
}
/* the harmonics 2 .. 5 against the fundamental, in dB */
static double mz_thd(const float *x, int n, double f0) {
    double a0 = mz_amp(x, n, f0), s = 0;
    for (int h = 2; h <= 5 && h * f0 < 0.48 * MZ_FR; h++) {
        double a = mz_amp(x, n, h * f0);
        s += a * a;
    }
    return 10 * log10(s / (a0 * a0 + 1e-24) + 1e-14);
}

static float mz_x[MZ_FR * 8];

static void mz_self_osc(void) {
    static const double HZ[] = {20, 50, 110, 220, 440, 1000, 2000, 4000, 8000, 12000, 15000};
    printf("measure: SELF-OSCILLATION, resonance 100%% (k 4.4), no input: cents against the cutoff, rms level in dB, "
           "harmonics 2-5 in dB against the fundamental\n");
    printf("measure:   cutoff |  linear ladder           | non-linear ladder        | reference\n");
    for (unsigned i = 0; i < sizeof HZ / sizeof HZ[0]; i++) {
        int n = HZ[i] < 100 ? MZ_FR * 8 : MZ_FR * 2, tail = n / 2;
        printf("measure: %7.0f Hz", HZ[i]);
        for (int k = 0; k < 3; k++) {
            static mz_flt_t f;
            static const mz_sig_t none = {MZ_SILENCE, 0, 0};
            mz_init(&f, k, HZ[i], 1.0);
            mz_seed(&f, 0.1);
            mz_run(&f, &none, mz_x, n);
            const float *t = mz_x + n - tail;
            double hz = mz_freq(t, tail), rms = mz_rms(t, tail);
            printf(" | %+7.1f c %5.1f dB ", hz > 0 ? 1200 * log2(hz / HZ[i]) : 0.0, mz_db(rms));
            if (hz > 0 && 2 * hz < 0.48 * MZ_FR) printf("%6.1f", mz_thd(t, tail, hz));
            else printf("   n/a");   /* (no harmonic below the top) */
        }
        printf("\n");
    }
    /* is the reference's step small enough: 16 against 32 steps per sample */
    double h[2], a[2];
    for (int k = 0; k < 2; k++) {
        static mz_flt_t f;
        static const mz_sig_t none = {MZ_SILENCE, 0, 0};
        mz_init(&f, MZ_REF, 8000, 1.0);
        f.r.os = MZ_REF_OS << k;
        mz_seed(&f, 0.1);
        mz_run(&f, &none, mz_x, MZ_FR);
        h[k] = mz_freq(mz_x + MZ_FR / 2, MZ_FR / 2);
        a[k] = mz_rms(mz_x + MZ_FR / 2, MZ_FR / 2);
    }
    char what[200];
    snprintf(what, sizeof what, "measure: the reference has converged: at 8 kHz, %d against %d steps per sample differ by %.3f "
             "cents and %.3f dB", MZ_REF_OS, MZ_REF_OS * 2, 1200 * log2(h[1] / h[0]), mz_db(a[1]) - mz_db(a[0]));
    check(fabs(1200 * log2(h[1] / h[0])) < 0.5 && fabs(mz_db(a[1]) - mz_db(a[0])) < 0.05, what);
}

/* does an oscillation grow, or die, at this k? From a small start, in the quiet where the circuit is linear */
static int mz_grows(int kind, double hz, double k) {
    static mz_flt_t f;
    static const mz_sig_t none = {MZ_SILENCE, 0, 0};
    mz_init(&f, kind, hz, k / LADDER_K_MAX);
    mz_seed(&f, 1e-3);
    int n = MZ_FR / 2;
    mz_run(&f, &none, mz_x, n);
    return mz_rms(mz_x + 3 * n / 4, n / 4) > mz_rms(mz_x + n / 4, n / 4);
}
static void mz_onset(void) {
    static const double HZ[3] = {200, 1000, 8000};
    printf("measure: ONSET of oscillation: the k where it starts to grow (theory: 4)\n");
    for (int h = 0; h < 3; h++) {
        double on[3];
        for (int kd = 0; kd < 3; kd++) {
            double lo = 3.0, hi = LADDER_K_MAX;
            for (int it = 0; it < 7; it++) {
                double m = 0.5 * (lo + hi);
                if (mz_grows(kd, HZ[h], m)) hi = m; else lo = m;
            }
            on[kd] = 0.5 * (lo + hi);
        }
        printf("measure: %6.0f Hz: linear ladder k %.3f, non-linear ladder k %.3f, reference k %.3f\n", HZ[h], on[0], on[1], on[2]);
    }
}

static void mz_level_vs_k(double amp) {
    printf("measure: LEVEL AGAINST RESONANCE: a %.2f saw at 110 Hz, cutoff 1 kHz: rms / fundamental in dB against k 0 "
           "(k 0's own in brackets)\n", amp);
    printf("measure:     k | 1/(1+k) | linear ladder  | non-linear     | reference\n");
    double base[3][2] = {{0}}, ref_k4 = 0;
    mz_saw_make(110);
    for (int kk = 0; kk <= 4; kk++) {
        printf("measure:   %.1f | %6.1f ", (double)kk, mz_db(1.0 / (1 + kk)));
        for (int kd = 0; kd < 3; kd++) {
            static mz_flt_t f;
            mz_sig_t s = {MZ_SAW, 110, amp};
            mz_init(&f, kd, 1000, kk / LADDER_K_MAX);
            int n = MZ_FR;
            mz_run(&f, &s, mz_x, n);
            double r = mz_db(mz_rms(mz_x + n / 2, n / 2)), a = mz_db(mz_amp(mz_x + n / 2, n / 2, 110));
            if (kk == 0) { base[kd][0] = r; base[kd][1] = a; }
            if (kk == 4 && kd == MZ_REF) ref_k4 = a - base[kd][1];
            printf("| %+5.1f %+5.1f     ", r - base[kd][0], a - base[kd][1]);
        }
        printf("\n");
    }
    printf("measure:   (k 0 rms, fundamental: linear %.1f %.1f, non-linear %.1f %.1f, reference %.1f %.1f dB)\n",
           base[0][0], base[0][1], base[1][0], base[1][1], base[2][0], base[2][1]);
    if (amp < 0.1) {   /* in the quiet the reference must be the textbook's: the kit's own check */
        char what[160];
        snprintf(what, sizeof what, "measure: the reference, quiet, at k 4: the fundamental %.1f dB against 1/(1+k)'s %.1f dB",
                 ref_k4, mz_db(0.2));
        check(fabs(ref_k4 - mz_db(0.2)) < 0.5, what);
    }
}

static void mz_harmonics_table(const int *kinds, int nk) {
    static const double LV[] = {-40, -20, -12, -6, 0, 6, 12}, RES[3] = {0, 0.5, 0.9};
    printf("measure: HARMONICS: a 200 Hz sine, cutoff 1 kHz: its gain in dB, then the 2nd, 3rd and 5th in dB against it\n");
    for (int r = 0; r < 3; r++)
        for (int nkd = 0; nkd < nk; nkd++) {
            int kd = kinds[nkd];
            printf("measure:   %-18s res %2.0f%%:", MZ_NAME[kd], RES[r] * 100);
            for (unsigned l = 0; l < sizeof LV / sizeof LV[0]; l++) {
                static mz_flt_t f;
                mz_sig_t s = {MZ_SINE, 200, pow(10, LV[l] / 20)};
                mz_init(&f, kd, 1000, RES[r]);
                int n = MZ_FR / 2;
                mz_run(&f, &s, mz_x, n);
                const float *t = mz_x + n / 2;
                double a1 = mz_amp(t, n / 2, 200);
                printf("  %+3.0fdB:%+5.1f/%4.0f/%4.0f/%4.0f", LV[l], mz_db(a1) - LV[l], mz_db(mz_amp(t, n / 2, 400)) - mz_db(a1),
                       mz_db(mz_amp(t, n / 2, 600)) - mz_db(a1), mz_db(mz_amp(t, n / 2, 1000)) - mz_db(a1));
            }
            printf("\n");
        }
}

static void mz_compression(const int *kinds, int nk) {
    static const double LV[] = {-40, -30, -20, -12, -6, 0, 6, 12};
    printf("measure: COMPRESSION: a sine at the cutoff (1 kHz), resonance 90%%: gain in dB at each input level\n");
    printf("measure:   %-18s", "input dB");
    for (unsigned l = 0; l < sizeof LV / sizeof LV[0]; l++) printf(" %+6.0f", LV[l]);
    printf("\n");
    for (int nkd = 0; nkd < nk; nkd++) {
        int kd = kinds[nkd];
        printf("measure:   %-18s", MZ_NAME[kd]);
        for (unsigned l = 0; l < sizeof LV / sizeof LV[0]; l++) {
            static mz_flt_t f;
            mz_sig_t s = {MZ_SINE, 1000, pow(10, LV[l] / 20)};
            mz_init(&f, kd, 1000, 0.9);
            int n = MZ_FR / 2;
            mz_run(&f, &s, mz_x, n);
            printf(" %+6.1f", mz_db(mz_amp(mz_x + n / 2, n / 2, 1000)) - LV[l]);
        }
        printf("\n");
    }
}

/* (the voice's other side of it: test/analogx_check.c, through the engine) */
static void mz_svf_self_osc(void) {
    static const double HZ[] = {50, 200, 1000, 2000, 4000, 8000, 12000, 16000};
    printf("measure: NON-LINEAR SVF, SELF-OSC at 100%%: cents against the cutoff, rms level, harmonics 2-5 in dB\n");
    for (unsigned i = 0; i < sizeof HZ / sizeof HZ[0]; i++) {
        static mz_flt_t f;
        static const mz_sig_t none = {MZ_SILENCE, 0, 0};
        int n = HZ[i] < 100 ? MZ_FR * 6 : MZ_FR * 2;
        mz_init(&f, MZ_SVFN, HZ[i], 1.0);
        svf2_set_osc(&f.s, 1.0f, 1.0f, SVF_LP);
        mz_seed(&f, 0.01);
        mz_run(&f, &none, mz_x, n);
        const float *t = mz_x + n - MZ_FR;
        double hz = mz_freq(t, MZ_FR);
        printf("measure:   %6.0f Hz: %+6.1f cents, %5.1f dB, %6.1f\n", HZ[i], 1200 * log2(hz / HZ[i]), mz_db(mz_rms(t, MZ_FR)),
               2 * hz < 0.48 * MZ_FR ? mz_thd(t, MZ_FR, hz) : 0.0);
    }
}

static void mz_aliasing(void) {
    static const int NOTES[3] = {91, 96, 100};
    printf("measure: ALIASING (alias_db, the whole engine): a triangle into the ladder, drive 100%%, non-linear 100%%\n");
    for (int n = 0; n < 3; n++) {
        double a[3], f, hz;
        for (int q = 0; q < 3; q++) hq_measure(2, NOTES[n], q, &a[q], &f, &hz);
        printf("measure:   note %3d: Off %6.1f dB, 2x %6.1f dB, 4x %6.1f dB\n", NOTES[n], a[0], a[1], a[2]);
    }
}

/* the residual of y against x in dB, after the best gain and delay (searched by whole FFT, phase-shifted: 0 .. 8 samples) */
static double mz_null_db(const float *x, const float *y, int n) {
    static double xr[N], xi[N], yr[N], yi[N];
    (void)n;
    for (int i = 0; i < N; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (N - 1));
        xr[i] = x[i] * w; xi[i] = 0;
        yr[i] = y[i] * w; yi[i] = 0;
    }
    fft(xr, xi, N);
    fft(yr, yi, N);
    double best = 1e9, pxx = 0;
    for (int k = 1; k < N / 2; k++) pxx += xr[k] * xr[k] + xi[k] * xi[k];
    for (double d = 0; d <= 8.0; d += 0.05) {
        double sxy = 0, syy = 0, err = 0;
        for (int k = 1; k < N / 2; k++) {
            double a = 2 * M_PI * k * d / N, cr = cos(a), ci = sin(a);   /* y moved earlier by d */
            double re = yr[k] * cr - yi[k] * ci, im = yr[k] * ci + yi[k] * cr;
            sxy += xr[k] * re + xi[k] * im;
            syy += re * re + im * im;
        }
        double g = sxy / (syy + 1e-30);
        for (int k = 1; k < N / 2; k++) {
            double a = 2 * M_PI * k * d / N, cr = cos(a), ci = sin(a);
            double re = g * (yr[k] * cr - yi[k] * ci) - xr[k], im = g * (yr[k] * ci + yi[k] * cr) - xi[k];
            err += re * re + im * im;
        }
        if (err < best) best = err;
    }
    return 10 * log10(best / pxx + 1e-30);
}
static void mz_null_4x(void) {
    static float a[N + 8192], b[N + 8192];
    static const struct { const char *name; int note, nl, res, drive; } C[] = {
        {"saw, note 36, linear ladder, res 50%", 36, 0, 50, 0},
        {"saw, note 36, non-linear ladder, res 50%", 36, 100, 50, 0},
        {"saw, note 72, linear ladder, res 50%", 72, 0, 50, 0},
        {"saw, note 72, non-linear ladder, res 50%", 72, 100, 50, 0},
        {"saw, note 72, non-linear ladder, res 90%, drive 100%", 72, 100, 90, 100},
    };
    printf("measure: NULL AGAINST 4x (the whole engine, level and delay matched: how far 1x is from converged)\n");
    for (unsigned k = 0; k < sizeof C / sizeof C[0]; k++) {
        for (int q = 0; q < 2; q++) {
            void *e = patch(SAW, 0, 1, 100, 10);
            set(e, "voices", 0);
            set(e, "flt_cut", 60); set(e, "flt_res", C[k].res); set(e, "flt_drive", C[k].drive); set(e, "an_ladder", C[k].nl);
            set(e, "an_hq", q ? 2 : 0);
            note(e, 1, C[k].note, 127);
            hq_render(e, q ? b : a, N + 8192);
            E->destroy(e);
        }
        printf("measure:   %-54s %6.1f dB\n", C[k].name, mz_null_db(a + 8192, b + 8192, N));
    }
}

/* a slow sweep of a self-oscillating filter over a quiet saw: frame by frame, the strongest partial near the cutoff */
static void mz_sweep(void) {
    enum { FR = 4096, HOP = 2048 };
    static double re[FR], im[FR];
    int n = MZ_FR * 4;
    double lo = 200, hi = 4000;
    mz_saw_make(110);
    printf("measure: SWEEP: 200 Hz - 4 kHz in 4 s, resonance 100%%, a 110 Hz saw at 0.1: the frames whose resonance sits on "
           "a harmonic (free running would give about 5%%), and its rms distance from the cutoff\n");
    for (int kd = 0; kd < 3; kd++) {
        static mz_flt_t f;
        mz_sig_t s = {MZ_SAW, 110, 0.1};
        mz_init(&f, kd, lo, 1.0);
        for (int i = 0; i < n; i++) {
            if (i % MZ_STEP == 0) mz_set(&f, lo * pow(hi / lo, (double)i / n), 1.0);
            mz_x[i] = (float)mz_tick(&f, &s, (double)i / MZ_FR);
        }
        int frames = 0, locked = 0;
        double d2 = 0;
        for (int at = MZ_FR / 2; at + FR <= n; at += HOP) {
            for (int i = 0; i < FR; i++) {
                re[i] = mz_x[at + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (FR - 1)));
                im[i] = 0;
            }
            fft(re, im, FR);
            double target = lo * pow(hi / lo, (at + FR / 2.0) / n), bin = (double)MZ_FR / FR;
            int k0 = (int)(target / sqrt(2) / bin), k1 = (int)(target * sqrt(2) / bin), best = k0;
            double bp = -1;
            for (int k = k0; k <= k1; k++) {
                double p = re[k] * re[k] + im[k] * im[k];
                if (p > bp) { bp = p; best = k; }
            }
            double a = log(re[best - 1] * re[best - 1] + im[best - 1] * im[best - 1] + 1e-30), b = log(bp + 1e-30),
                   c = log(re[best + 1] * re[best + 1] + im[best + 1] * im[best + 1] + 1e-30);
            double hz = (best + 0.5 * (a - c) / (a - 2 * b + c)) * bin, h = hz / 110;
            if (fabs(h - round(h)) * 110 < 3) locked++;
            d2 += pow(1200 * log2(hz / target), 2);
            frames++;
        }
        printf("measure:   %-18s %3.0f%% of frames on a harmonic, %5.1f cents rms from the cutoff\n", MZ_NAME[kd],
               100.0 * locked / frames, sqrt(d2 / frames));
    }
}

/* a resonant envelope: the cutoff falling from 8 kHz to 200 Hz (time constant `ms`) over a saw, the cutoff set every
 * `step` samples */
static void mz_env_run(mz_flt_t *f, float *out, int n, double ms, int step, double res) {
    mz_sig_t s = {MZ_SAW, 110, 0.5};
    for (int i = 0; i < n; i++) {
        if (i % step == 0) {   /* (the value at the middle of the step: stepping alone is measured, not a delay) */
            double e = exp(-(i + 0.5 * (step - 1)) / (ms * 0.001 * MZ_FR));
            mz_set(f, 200 * pow(8000.0 / 200, e), res);
        }
        out[i] = (float)mz_tick(f, &s, (double)i / MZ_FR);
    }
}
static void mz_cutoff_steps(void) {
    static const double MS[3] = {1, 5, 25};
    static const int KD[3] = {MZ_LAD, MZ_NL, MZ_SVF};
    static float a[MZ_FR / 4], b[MZ_FR / 4];
    mz_saw_make(110);
    printf("measure: CUTOFF STEPS: a resonant envelope (8 kHz to 200 Hz, resonance 90%%) with the cutoff set every 16 samples "
           "against every sample: the residual in dB (report item 8 is dropped below -70 dB)\n");
    for (int k = 0; k < 3; k++) {
        printf("measure:   %-18s", MZ_NAME[KD[k]]);
        for (int m = 0; m < 3; m++) {
            static mz_flt_t f;
            int n = MZ_FR / 4;
            mz_init(&f, KD[k], 8000, 0.9);
            mz_env_run(&f, a, n, MS[m], MZ_STEP, 0.9);
            mz_init(&f, KD[k], 8000, 0.9);
            mz_env_run(&f, b, n, MS[m], 1, 0.9);
            double d = 0, s = 0;
            for (int i = 0; i < n; i++) { d += (double)(a[i] - b[i]) * (a[i] - b[i]); s += (double)b[i] * b[i]; }
            printf("  %2.0f ms: %6.1f dB", MS[m], 10 * log10(d / s + 1e-30));
        }
        printf("\n");
    }
}

static void analog_measure(void) {
    static const int ALL[] = {MZ_LAD, MZ_NL, MZ_REF, MZ_SVF, MZ_SVFN};
    printf("measure: the scale against the reference: input x is u = 0.625 x, y4 = 1 comes out as 1.6, no bass make-up in "
           "the reference. All offline, no ears.\n");
    mz_self_osc();
    mz_onset();
    mz_level_vs_k(0.05);
    mz_level_vs_k(1.0);
    mz_harmonics_table(ALL, 5);
    mz_compression(ALL, 5);
    mz_svf_self_osc();
    mz_aliasing();
    mz_null_4x();
    mz_sweep();
    mz_cutoff_steps();
    printf("measure: CPU: offline_check analogbench and hqbench print each setting's cost\n");
}
