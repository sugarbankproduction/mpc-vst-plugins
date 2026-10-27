/* The rate converter on its own (desktop/resample.c), at many host rates: passband gain, distortion and noise
 * (a sine fitted to the output, the rest is error), images above the band, the reported delay against where an
 * impulse lands, and CPU per 512-sample block. Built for Windows by desktop/build.sh (resample_test.exe); plain C, so
 * it also builds and runs anywhere: cc -O2 resample_test.c resample.c -lm -lpthread */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "resample.h"

static int failures;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

#ifdef _WIN32
#include <windows.h>
static double now_s(void) {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / f.QuadPart;
}
#else
static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
#endif

enum { IN_LEN = 44100, MAX_OUT = 800000 };
static float in_lr[2 * IN_LEN], out_l[MAX_OUT], out_r[MAX_OUT];

/* run `in_lr` (IN_LEN engine frames) through a converter at fs, feeding 128 frames at a time as hostrate.c does */
static int convert(int fs, resampler_t *rs) {
    rs_reset(rs, fs);
    int made = 0, fed = 0, want = (int)((double)IN_LEN * fs / RS_RATE) - 2 * rs->delay - 8;
    while (made < want) {
        made += rs_out(rs, out_l + made, out_r + made, want - made, 0);
        if (made < want) {
            int n = IN_LEN - fed < 128 ? IN_LEN - fed : 128;
            if (n <= 0) break;
            rs_write(rs, in_lr + 2 * fed, n);
            fed += n;
        }
    }
    return made;
}

/* least squares a*cos + b*sin at frequency f (Hz at rate fs) over y[from..to): amplitude, and the rest's rms */
static void fit(const float *y, int from, int to, double f, int fs, double *amp, double *resid, double *delay) {
    double cc = 0, ss = 0, cs = 0, yc = 0, ys = 0, w = 2 * M_PI * f / fs;
    for (int i = from; i < to; i++) {
        double c = cos(w * i), s = sin(w * i);
        cc += c * c; ss += s * s; cs += c * s; yc += y[i] * c; ys += y[i] * s;
    }
    double det = cc * ss - cs * cs, a = (yc * ss - ys * cs) / det, b = (ys * cc - yc * cs) / det, e = 0;
    for (int i = from; i < to; i++) {
        double d = y[i] - a * cos(w * i) - b * sin(w * i);
        e += d * d;
    }
    *amp = sqrt(a * a + b * b);
    *resid = sqrt(e / (to - from));
    if (delay) *delay = atan2(-a, b) / w;   /* a sine that started at sample 0 shows as delayed by this */
}

/* the same with two sines at once (f1, f2), solved together so the strong one cannot leak into the weak one's
 * estimate over a finite stretch: the amplitude at f2 */
static double fit_second(const float *y, int from, int to, double f1, double f2, int fs) {
    double A[4][5] = {{0}}, w1 = 2 * M_PI * f1 / fs, w2 = 2 * M_PI * f2 / fs, x[4];
    for (int i = from; i < to; i++) {
        double b[4] = {cos(w1 * i), sin(w1 * i), cos(w2 * i), sin(w2 * i)};
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) A[r][c] += b[r] * b[c];
            A[r][4] += b[r] * y[i];
        }
    }
    for (int p = 0; p < 4; p++)   /* Gaussian elimination (the matrix is well conditioned: distinct frequencies) */
        for (int r = p + 1; r < 4; r++) {
            double m = A[r][p] / A[p][p];
            for (int c = p; c < 5; c++) A[r][c] -= m * A[p][c];
        }
    for (int r = 3; r >= 0; r--) {
        x[r] = A[r][4];
        for (int c = r + 1; c < 4; c++) x[r] -= A[r][c] * x[c];
        x[r] /= A[r][r];
    }
    return sqrt(x[2] * x[2] + x[3] * x[3]);
}

static void sine_in(double f, double amp) {
    for (int i = 0; i < IN_LEN; i++) in_lr[2 * i] = in_lr[2 * i + 1] = (float)(amp * sin(2 * M_PI * f * i / RS_RATE));
}

int main(void) {
    static resampler_t rs;
    static const int rates[] = {44100, 48000, 88200, 96000, 176400, 192000, 50000, 32000, 22050};
    static const double freqs[] = {20, 100, 1000, 5000, 10000, 15000, 18000, 19000, 20000};
    char what[200];
    rs_init();
    for (unsigned k = 0; k < sizeof rates / sizeof rates[0]; k++) {
        int fs = rates[k];
        double worst_gain = 0, worst_snr = 1e9, top = fs >= RS_RATE ? 20000 : 0.4 * fs;
        for (unsigned j = 0; j < sizeof freqs / sizeof freqs[0]; j++) {
            if (freqs[j] > top) continue;
            sine_in(freqs[j], 0.5);
            int n = convert(fs, &rs);
            double amp, res;
            fit(out_l, 2 * rs.delay + 64, n, freqs[j], fs, &amp, &res, NULL);
            double gain = 20 * log10(amp / 0.5), snr = 20 * log10(amp / sqrt(2) / (res > 1e-15 ? res : 1e-15));
            if (fabs(gain) > fabs(worst_gain)) worst_gain = gain;
            if (snr < worst_snr) worst_snr = snr;
        }
        snprintf(what, sizeof what, "%6d Hz: delay %3d samples, passband to %5.0f Hz: gain within %.4f dB, "
                 "signal to error %.1f dB", fs, rs_delay_for(fs), top, worst_gain, worst_snr);
        check(fabs(worst_gain) < 0.001 && worst_snr > 95, what);
        /* images: content near the top of the engine's band, and where its image lands above 22.05 kHz */
        if (fs > RS_RATE) {
            static const double hi[] = {20000, 21000, 22000};
            double worst20 = 0;
            for (int j = 0; j < 3; j++) {
                sine_in(hi[j], 0.5);
                int n = convert(fs, &rs);
                double img = RS_RATE - hi[j], amp = fit_second(out_l, 2 * rs.delay + 64, n, hi[j], img, fs);
                double db = 20 * log10(amp / 0.5 + 1e-20);
                if (j == 0) worst20 = db;
                printf("     %6d Hz: a %5.0f Hz tone's image at %5.0f Hz: %.1f dB\n", fs, hi[j], img, db);
            }
            snprintf(what, sizeof what, "%6d Hz: 20 kHz's image (24.1 kHz) %.1f dB", fs, worst20);
            check(worst20 < -100, what);
        }
        /* timing: a 100 Hz sine from engine frame 0 comes out delayed by exactly the reported latency */
        sine_in(100, 0.5);
        int n = convert(fs, &rs);
        double amp, res, late;
        fit(out_l, 2 * rs.delay + 64, n, 100, fs, &amp, &res, &late);
        snprintf(what, sizeof what, "%6d Hz: output delayed by %.4f host samples (reported latency %d)", fs, late, rs.delay);
        check(fabs(late - rs.delay) < 0.01, what);
    }
    /* CPU: one 512-sample host block, stereo, at 48 and 96 kHz */
    static const int bench[] = {48000, 96000};
    sine_in(1000, 0.5);
    for (int k = 0; k < 2; k++) {
        int fs = bench[k], blocks = 0, fed = 0;
        rs_reset(&rs, fs);
        double t0 = now_s();
        while (fed + 128 <= IN_LEN) {
            int made = 0;
            while (made < 512) {
                made += rs_out(&rs, out_l + made, out_r + made, 512 - made, 0);
                if (made < 512) { rs_write(&rs, in_lr + 2 * fed, 128); fed += 128; }
                if (fed + 128 > IN_LEN) break;
            }
            blocks++;
        }
        double us = (now_s() - t0) / blocks * 1e6, block_us = 512.0 / fs * 1e6;
        printf("     CPU %6d Hz: %.2f us per 512-sample block (%.2f%% of the block's %.0f us)\n", fs, us,
               100 * us / block_us, block_us);
    }
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
