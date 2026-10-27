/* ONE-ish offline checks beyond tools/test_port.sh, run through the engine API (mpc_engine()) on x86:
 *   aliasing  high saw and square notes: power that is not a harmonic of the note (aliasing folds back as
 *             inharmonic tones, the "whistle"), against a naive oscillator at the same pitch
 *   envelope  a released note reaches silence; full velocity and volume do not clip
 *   keys      repeated keys, mono fallback to a held key, All Notes Off / All Sound Off
 *   state     save/restore round trip; damaged or oversized states don't break the instance
 *   wav       writes test/out/phase2_demo.wav to listen to
 *   bench     (argv[1] == "bench") microseconds per 128-frame block on this machine; NOT the MPC's number
 * Build and run: win/run.sh bash ports/one-ish/test/run.sh */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params.h"
#include "engine.h"
#include "osc.h"

#define SR 44100
#define N 16384   /* FFT length: 2.7 Hz per bin */

static const mpc_engine_t *E;
static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static void set(void *s, const char *k, double v) {
    char b[32];
    snprintf(b, sizeof b, "%g", v);
    E->set_param(s, k, b);
}

static void note(void *s, int on, int n, int vel) {
    uint8_t m[3] = {(uint8_t)(on ? 0x90 : 0x80), (uint8_t)n, (uint8_t)vel};
    E->midi(s, m, 3);
}

/* render `frames` of the left channel as floats in blocks of up to 128 */
static void render_f(void *s, float *dst, int frames) {
    int16_t buf[256];
    for (int at = 0; at < frames; at += 128) {
        int n = frames - at < 128 ? frames - at : 128;
        E->render(s, buf, n);
        for (int i = 0; i < n; i++) dst[at + i] = buf[2 * i] / 32768.0f;
    }
}

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k + len / 2] = re[i + k] - xr; im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}

/* inharmonic power relative to the total, in dB, of N samples of a tone at f0 (Blackman-Harris window) */
static double alias_db(const float *x, double f0) {
    static double re[N], im[N];
    for (int i = 0; i < N; i++) {
        double t = 2 * M_PI * i / (N - 1);
        re[i] = x[i] * (0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t));
        im[i] = 0;
    }
    fft(re, im, N);
    double harm = 0, other = 0, bin = (double)SR / N;
    for (int k = 4; k < N / 2; k++) {   /* skip DC */
        double p = re[k] * re[k] + im[k] * im[k], f = k * bin, h = f / f0;
        if (fabs(h - round(h)) * f0 <= 6 * bin && round(h) >= 1) harm += p;
        else other += p;
    }
    return 10 * log10(other / (harm + other) + 1e-30);
}

static void naive(float *x, int n, double f0, int wave) {
    double t = 0, dt = f0 / SR;
    for (int i = 0; i < n; i++) {
        x[i] = (float)(wave == WAVE_SAW ? 2 * t - 1 : t < 0.5 ? 1 : -1) * 0.35f;
        t += dt;
        if (t >= 1) t -= 1;
    }
}

static void *patch(int wave, double a, double d, double s, double r) {
    void *e = E->create(NULL);
    set(e, "o1_wave", wave); set(e, "amp_a", a); set(e, "amp_d", d); set(e, "amp_s", s); set(e, "amp_r", r);
    set(e, "volume", 100);
    return e;
}

static void aliasing(void) {
    static float x[N + 4096];
    static const int notes[] = {84, 96, 103, 108};
    static const char *const names[] = {"saw", "square"};
    for (int w = 0; w < WAVE_COUNT; w++)
        for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; n++) {
            double f0 = 440 * pow(2, (notes[n] - 69) / 12.0);
            void *e = patch(w, 0, 1, 100, 10);
            note(e, 1, notes[n], 127);
            render_f(e, x, N + 4096);   /* the first 4096 frames are the attack: skipped */
            double ours = alias_db(x + 4096, f0);
            E->destroy(e);
            naive(x, N, f0, w);
            double ref = alias_db(x, f0);
            char what[160];
            snprintf(what, sizeof what, "%-6s note %3d (%5.0f Hz): aliasing %6.1f dB, naive oscillator %6.1f dB",
                     names[w], notes[n], f0, ours, ref);
            /* regression guard. First measured 2026-10-27: -25.8 dB (saw, note 108) to -34.4 dB, 15-18 dB below naive */
            check(ours < ref - 12 && ours < -24, what);
        }
}

static void envelope(void) {
    static float x[SR * 2];
    void *e = patch(WAVE_SQUARE, 0, 100, 100, 200);
    note(e, 1, 36, 127);
    render_f(e, x, SR / 2);
    float peak = 0;
    for (int i = 0; i < SR / 2; i++) peak = fmaxf(peak, fabsf(x[i]));
    char what[96];
    snprintf(what, sizeof what, "full velocity and volume peak at %.2f of full scale, no clipping", peak);
    check(peak > 0.3f && peak < 0.99f, what);
    note(e, 0, 36, 0);
    render_f(e, x, SR);
    int silent = 1;
    for (int i = SR - 1024; i < SR; i++) silent &= x[i] == 0;
    check(silent, "released note is silent 1 s after a 200 ms release");
    E->destroy(e);
}

/* is the last block after `ms` of rendering silent? */
static int silent_after(void *e, int ms) {
    static float x[SR * 2];
    int n = SR * ms / 1000;
    render_f(e, x, n);
    for (int i = n - 128; i < n; i++)
        if (x[i] != 0) return 0;
    return 1;
}

static void keys(void) {
    void *e = patch(WAVE_SAW, 0, 100, 100, 50);
    note(e, 1, 60, 100); note(e, 1, 60, 100); note(e, 0, 60, 0);
    check(silent_after(e, 500), "a key pressed twice and released once does not hang");
    note(e, 1, 48, 100); note(e, 1, 60, 100); note(e, 0, 60, 0);
    check(!silent_after(e, 500), "releasing the newer of two held keys keeps the older one sounding");
    note(e, 0, 48, 0);
    check(silent_after(e, 500), "releasing the last held key ends the note");
    note(e, 1, 60, 100);
    uint8_t cc123[3] = {0xb0, 123, 0}, cc120[3] = {0xb0, 120, 0};
    E->midi(e, cc123, 3);
    check(silent_after(e, 500), "All Notes Off (CC 123) releases a held note");
    note(e, 1, 60, 100);
    render_f(e, (float[128]){0}, 128);
    E->midi(e, cc120, 3);
    check(silent_after(e, 3), "All Sound Off (CC 120) is silent at once");
    E->destroy(e);
}

static double get(void *e, const char *k) {
    char b[64];
    return E->get_param(e, k, b, sizeof b) > 0 ? atof(b) : NAN;
}

static void state(void) {
    static char st[9000];
    void *a = patch(WAVE_SQUARE, 123, 456, 78, 901), *b = E->create(NULL);
    set(a, "volume", 42);
    int n = E->get_param(a, "state", st, sizeof st);
    E->set_param(b, "state", st);
    int same = n > 0;
    for (int i = 0; i < NPARAMS; i++) same &= get(a, PARAMS[i].key) == get(b, PARAMS[i].key);
    check(same, "saved state restores every value on a new instance");

    E->set_param(b, "state", "amp_s=nan;volume=inf;amp_a=-50;o1_wave=7;bogus;=3;amp_r=");
    check(get(b, "amp_s") == 78 && get(b, "volume") == 42, "a damaged state's NaN and infinite values are ignored");
    check(get(b, "amp_a") == 0 && get(b, "o1_wave") == 1 && get(b, "amp_r") == 1,
          "a damaged state's out-of-range values are clamped to the param's range");
    memset(st, 'x', sizeof st - 1);
    st[sizeof st - 1] = 0;
    memcpy(st, "volume=5;", 9);
    E->set_param(b, "state", st);
    check(get(b, "volume") == 42, "a state too long for the chunk is ignored, not half restored");
    E->destroy(a);
    E->destroy(b);
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

/* a short riff on each wave, then a run up to the top of the keyboard where aliasing would show */
static int16_t *play(void *e, int16_t *o, int n, int blocks) {   /* one note, `blocks` x 128 frames long */
    note(e, 1, n, 110);
    for (int b = 0; b < blocks; b++, o += 256) E->render(e, o, 128);
    note(e, 0, n, 0);
    return o;
}

static void demo_wav(const char *path) {
    enum { STEP = 86, RUN = 43, TAIL = 172 };   /* in 128-frame blocks: 0.25 s, 0.125 s, 0.5 s */
    static const int riff[] = {36, 48, 43, 46, 36, 48, 51, 50};
    static int16_t pcm[(WAVE_COUNT * 8 * STEP + 13 * RUN + TAIL) * 256];
    int16_t *o = pcm;
    for (int w = 0; w < WAVE_COUNT; w++) {
        void *e = patch(w, 5, 400, 40, 150);
        for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
        E->destroy(e);
    }
    void *e = patch(WAVE_SAW, 2, 200, 80, 60);
    for (int n = 72; n <= 108; n += 3) o = play(e, o, n, RUN);   /* 13 notes */
    for (int b = 0; b < TAIL; b++, o += 256) E->render(e, o, 128);
    E->destroy(e);

    uint32_t frames = (uint32_t)((o - pcm) / 2), bytes = frames * 4;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); failures++; return; }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SR); put32(f, SR * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes); fwrite(pcm, 4, frames, f);
    fclose(f);
    printf("wrote %s (%.1f s)\n", path, frames / (double)SR);
}

static void bench(void) {
    enum { BLOCKS = 200000 };
    int16_t buf[256];
    void *e = patch(WAVE_SAW, 5, 300, 70, 250);
    note(e, 1, 60, 100);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
    printf("bench: %.2f us per 128-frame block for one voice on this PC (MPC budget 2902 us, shared; device numbers need tools/bench.sh)\n", us);
    E->destroy(e);
}

int main(int argc, char **argv) {
    E = mpc_engine();
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
    aliasing();
    envelope();
    keys();
    state();
    demo_wav(argc > 1 ? argv[1] : "phase2_demo.wav");
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
