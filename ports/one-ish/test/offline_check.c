/* ONE-ish offline checks beyond tools/test_port.sh, run through the engine API (mpc_engine()) on x86:
 *   aliasing  high saw and square notes: power that is not a harmonic of the note (aliasing folds back as
 *             inharmonic tones, the "whistle"), against a naive oscillator at the same pitch
 *   sync      the same for oscillator 2 hard-synced to oscillator 1, against a naive hard sync
 *   tuning    octave, semitone and fine move each oscillator's pitch by that much
 *   noise/mix noise level; all sources at full level are limited, not clipped
 *   filter    self-oscillation is a clean, steady sine near the cutoff; the ladder alone stays stable in every mode
 *             and slope at full resonance and drive; LP/HP slopes measure 24/18/12/6 dB; the cutoff knob reads in Hz
 *   svf       LP/BP/HP/notch shapes, the resonance peak, stability at full resonance
 *   routing   parallel/series, the filter mix and each source's SVF/ladder buttons send the sound where they say
 *   mod       envelope timing and knob text, filter envelope, keyboard tracking, LFO vibrato free and tempo-synced,
 *             pitch envelope, a soft note after a loud one starts at its own level
 *   matrix    amounts and via scaling add up; mod wheel vibrato (the default slot 1); velocity to cutoff; aftertouch
 *             to a level; pitch bend
 *   envelope  a released note reaches silence; full velocity and volume do not clip
 *   keys      repeated keys, mono fallback to a held key, All Notes Off / All Sound Off
 *   state     save/restore round trip; damaged or oversized states don't break the instance
 *   wav       writes test/out/demo.wav to listen to
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
#include "ladder.h"
#include "svf.h"
#include "lfo.h"
#include "modmatrix.h"

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

/* power spectrum of N samples (Blackman-Harris window): pw[0..N/2) */
static const double *spectrum(const float *x) {
    static double re[N], im[N];
    for (int i = 0; i < N; i++) {
        double t = 2 * M_PI * i / (N - 1);
        re[i] = x[i] * (0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t));
        im[i] = 0;
    }
    fft(re, im, N);
    for (int k = 0; k < N / 2; k++) re[k] = re[k] * re[k] + im[k] * im[k];
    return re;
}

/* inharmonic power relative to the total, in dB, of N samples of a tone at f0 */
static double alias_db(const float *x, double f0) {
    const double *pw = spectrum(x);
    double harm = 0, other = 0, bin = (double)SR / N;
    for (int k = 4; k < N / 2; k++) {   /* skip DC */
        double f = k * bin, h = f / f0;
        if (fabs(h - round(h)) * f0 <= 6 * bin && round(h) >= 1) harm += pw[k];
        else other += pw[k];
    }
    return 10 * log10(other / (harm + other) + 1e-30);
}

/* frequency of the strongest partial (a saw's or square's fundamental), interpolated between bins */
static double peak_hz(const float *x) {
    const double *pw = spectrum(x);
    int best = 4;
    for (int k = 4; k < N / 2 - 1; k++)
        if (pw[k] > pw[best]) best = k;
    double a = log(pw[best - 1] + 1e-30), b = log(pw[best] + 1e-30), c = log(pw[best + 1] + 1e-30);
    return (best + 0.5 * (a - c) / (a - 2 * b + c)) * SR / N;
}

static void naive(float *x, int n, double f0, int wave) {
    double t = 0, dt = f0 / SR;
    for (int i = 0; i < n; i++) {
        x[i] = (float)(wave == WAVE_SAW ? 2 * t - 1 : t < 0.5 ? 1 : -1) * 0.35f;
        t += dt;
        if (t >= 1) t -= 1;
    }
}

static double knob_ms(double ms) { return ms <= 1 ? 0 : 25 * log10(ms); }   /* envelope knobs: 1 ms .. 10 s */

/* amp envelope in ms (attack, decay, release) and % (sustain) */
static void *patch(int wave, double a, double d, double s, double r) {
    void *e = E->create(NULL);
    set(e, "o1_wave", wave); set(e, "amp_a", knob_ms(a)); set(e, "amp_d", knob_ms(d)); set(e, "amp_s", s);
    set(e, "amp_r", knob_ms(r));
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

/* naive hard sync: the slave restarts on the sample the master wraps */
static void naive_sync(float *x, int n, double f0, double ratio) {
    double tm = 0, ts = 0;
    for (int i = 0; i < n; i++) {
        x[i] = (float)(2 * ts - 1) * 0.35f;
        tm += f0 / SR;
        ts += f0 * ratio / SR;
        if (ts >= 1) ts -= 1;
        if (tm >= 1) { tm -= 1; ts = 0; }
    }
}

/* a synced oscillator repeats at the master's pitch, so its aliasing is what isn't a harmonic of the master */
static void sync_aliasing(void) {
    static float x[N + 4096];
    static const int notes[] = {60, 72, 84, 96};
    double ratio = pow(2, 7.0 / 12);
    for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; n++) {
        double f0 = 440 * pow(2, (notes[n] - 69) / 12.0);
        void *e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "o2_sync", 1); set(e, "o2_semi", 7);
        note(e, 1, notes[n], 127);
        render_f(e, x, N + 4096);
        double ours = alias_db(x + 4096, f0);
        E->destroy(e);
        naive_sync(x, N, f0, ratio);
        double ref = alias_db(x, f0);
        char what[160];
        snprintf(what, sizeof what, "sync   note %3d (%5.0f Hz), osc 2 +7 st: aliasing %6.1f dB, naive sync %6.1f dB",
                 notes[n], f0, ours, ref);
        /* regression guard. First measured 2026-10-27: -26.6 dB (note 96) to -36.3 dB, 15-16 dB below naive sync */
        check(ours < ref - 12 && ours < -24, what);
    }
}

static double cents(double hz, double want) { return 1200 * log2(hz / want); }

/* each oscillator's octave, semitone and fine settings move its pitch by exactly that much */
static void tuning(void) {
    static float x[N + 4096];
    static const struct { const char *osc, *key; double val, ratio; } t[] = {
        {"o1", "o1_oct", 1, 0.5}, {"o1", "o1_oct", 4, 4.0}, {"o1", "o1_semi", 7, 1.4983070768766815},
        {"o1", "o1_semi", -12, 0.5}, {"o1", "o1_fine", 50, 1.0293022366434921}, {"o2", "o2_oct", 3, 2.0},
        {"o3", "o3_fine", -25, 0.98566319864018},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        void *e = patch(WAVE_SAW, 0, 1, 100, 10);
        char lvl[16];
        snprintf(lvl, sizeof lvl, "%s_level", t[i].osc);
        set(e, "o1_level", 0); set(e, lvl, 100); set(e, t[i].key, t[i].val);
        note(e, 1, 69, 127);
        render_f(e, x, N + 4096);
        double want = 440 * t[i].ratio, got = peak_hz(x + 4096), off = cents(got, want);
        char what[128];
        snprintf(what, sizeof what, "%s = %g plays %.1f Hz (want %.1f, %+.2f cents)", t[i].key, t[i].val, got, want, off);
        check(fabs(off) < 2, what);
        E->destroy(e);
    }
}

static double knob_for_hz(double hz) { return 100 * log(hz / 20) / log(1000); }   /* engine.c's cut_hz, inverted */

/* full resonance, nothing playing into it: the filter rings as a sine at its cutoff, steady and clean */
static void self_oscillation(void) {
    static float x[SR * 2];
    static const double hz[] = {110, 440, 1000, 3000, 8000};
    for (unsigned i = 0; i < sizeof hz / sizeof hz[0]; i++) {
        void *e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(hz[i]));
        note(e, 1, 60, 127);
        render_f(e, x, SR * 2);
        const float *tail = x + SR * 2 - N;
        double f = peak_hz(tail), off = cents(f, hz[i]);
        const double *pw = spectrum(tail);
        double fund = 0, all = 0, bin = (double)SR / N;
        for (int k = 4; k < N / 2; k++) {
            all += pw[k];
            if (fabs(k * bin - f) <= 6 * bin) fund += pw[k];
        }
        double rest_db = 10 * log10((all - fund) / all + 1e-30), r1 = 0, r2 = 0;
        for (int k = 0; k < SR / 4; k++) {   /* the last two quarter-seconds: is the level steady? */
            r1 += tail[N - SR / 2 + k] * tail[N - SR / 2 + k];
            r2 += tail[N - SR / 4 + k] * tail[N - SR / 4 + k];
        }
        double rms = sqrt(r2 / (SR / 4)), drift = fabs(10 * log10(r2 / r1));
        char what[200];
        snprintf(what, sizeof what, "self-oscillates at %5.0f Hz: %6.1f Hz (%+5.0f cents), rms %.3f, %.2f dB drift, "
                 "everything but the sine %5.1f dB", hz[i], f, off, rms, drift, rest_db);
        check(fabs(off) < 100 && rms > 0.03 && drift < 0.5 && rest_db < -25, what);
        E->destroy(e);
    }
}

/* the filter alone, pushed as hard as it goes: every mode and slope, full resonance and drive, a loud saw while the
 * cutoff sweeps the whole range and back. Unstable would mean huge, growing or NaN output. */
static void filter_stability(void) {
    float worst = 0;
    int bad = 0;
    for (int mode = 0; mode < 2; mode++)
        for (int slope = 0; slope < 4; slope++) {
            ladder_t l;
            memset(&l, 0, sizeof l);
            double t = 0;
            for (int b = 0; b < 1400; b++) {   /* about 4 s in 128-sample blocks */
                double pos = b < 700 ? b / 700.0 : (1400 - b) / 700.0;
                ladder_set(&l, 20 * pow(1000, pos), 1.0f, 1.0f, mode, slope, SR);
                for (int i = 0; i < 128; i++) {
                    float y = ladder_next(&l, (float)(2 * t - 1) * 3.0f);
                    t += 55.0 / SR;
                    if (t >= 1) t -= 1;
                    if (!isfinite(y)) bad++;
                    else if (fabsf(y) > worst) worst = fabsf(y);
                }
            }
        }
    char what[128];
    snprintf(what, sizeof what, "filter stable in every mode and slope at full resonance and drive (largest output %.2f)", worst);
    check(!bad && worst < 8, what);
}

/* gain of the filter alone for a quiet sine (the linear range), in dB */
static double filter_gain_db(int mode, int slope, double fc, double f) {
    ladder_t l;
    memset(&l, 0, sizeof l);
    ladder_set(&l, (float)fc, 0, 0, mode, slope, SR);
    double in = 0, out = 0;
    for (int i = 0; i < SR; i++) {
        float s = 0.1f * sinf((float)(2 * M_PI * f * i / SR));
        float y = ladder_next(&l, s);
        if (i >= SR / 2) { in += s * s; out += y * y; }
    }
    return 10 * log10(out / in);
}

/* slopes: n poles lose 12.3 dB each two octaves past the cutoff (a one-pole is 1/sqrt(1 + 16) there) */
static void filter_slopes(void) {
    static const char *const names[] = {"24 dB", "18 dB", "12 dB", "6 dB"};
    for (int mode = 0; mode < 2; mode++)
        for (int slope = 0; slope < 4; slope++) {
            int poles = 4 - slope;
            double pass = filter_gain_db(mode, slope, 1000, mode == LADDER_LP ? 250 : 4000);
            double stop = filter_gain_db(mode, slope, 1000, mode == LADDER_LP ? 4000 : 250);
            double want = -12.3 * poles;
            char what[160];
            snprintf(what, sizeof what, "%s %-5s: two octaves into the stopband %6.1f dB below the passband (want %.1f)",
                     mode == LADDER_LP ? "LP" : "HP", names[slope], pass - stop, -want);
            check(fabs((stop - pass) - want) < 3, what);
        }
}

/* gain of the SVF alone for a quiet sine, in dB */
static double svf_gain_db(int mode, double reso, double fc, double f) {
    svf_t sv;
    memset(&sv, 0, sizeof sv);
    svf_set(&sv, (float)fc, (float)reso, mode, SR);
    double in = 0, out = 0;
    for (int i = 0; i < SR; i++) {
        float x = 0.1f * sinf((float)(2 * M_PI * f * i / SR));
        float y = svf_next(&sv, x);
        if (i >= SR / 2) { in += x * x; out += y * y; }
    }
    return 10 * log10(out / in + 1e-30);
}

static void svf_shapes(void) {
    char what[200];
    double lp_pass = svf_gain_db(SVF_LP, 0, 1000, 250), lp_stop = svf_gain_db(SVF_LP, 0, 1000, 4000);
    snprintf(what, sizeof what, "SVF LP: %.1f dB at a quarter of the cutoff, %.1f dB two octaves above (12 dB/oct)", lp_pass, lp_stop);
    check(lp_pass > -1 && fabs(lp_stop - lp_pass + 24.1) < 2, what);
    double hp_pass = svf_gain_db(SVF_HP, 0, 1000, 4000), hp_stop = svf_gain_db(SVF_HP, 0, 1000, 250);
    snprintf(what, sizeof what, "SVF HP: %.1f dB two octaves above the cutoff, %.1f dB two below", hp_pass, hp_stop);
    check(hp_pass > -1 && fabs(hp_stop - hp_pass + 24.1) < 2, what);
    double bp_mid = svf_gain_db(SVF_BP, 0, 1000, 1000), bp_lo = svf_gain_db(SVF_BP, 0, 1000, 125),
           bp_hi = svf_gain_db(SVF_BP, 0, 1000, 8000);
    snprintf(what, sizeof what, "SVF BP: %.1f dB at the cutoff, %.1f / %.1f dB three octaves below / above", bp_mid, bp_lo, bp_hi);
    check(fabs(bp_mid) < 1 && bp_lo < bp_mid - 10 && bp_hi < bp_mid - 10, what);
    double n_mid = svf_gain_db(SVF_NOTCH, 0.5, 1000, 1000), n_lo = svf_gain_db(SVF_NOTCH, 0.5, 1000, 125),
           n_hi = svf_gain_db(SVF_NOTCH, 0.5, 1000, 8000);
    snprintf(what, sizeof what, "SVF notch: %.1f dB at the cutoff, %.1f / %.1f dB three octaves below / above", n_mid, n_lo, n_hi);
    check(n_mid < -30 && n_lo > -1 && n_hi > -1, what);
    double peak = svf_gain_db(SVF_LP, 1, 1000, 1000);
    snprintf(what, sizeof what, "SVF LP at full resonance: %+.1f dB at the cutoff (want about +20)", peak);
    check(peak > 17 && peak < 22, what);

    float worst = 0;
    int bad = 0;
    for (int mode = 0; mode < 4; mode++) {
        svf_t sv;
        memset(&sv, 0, sizeof sv);
        double t = 0;
        for (int b = 0; b < 1400; b++) {
            double pos = b < 700 ? b / 700.0 : (1400 - b) / 700.0;
            svf_set(&sv, (float)(20 * pow(1000, pos)), 1.0f, mode, SR);
            for (int i = 0; i < 128; i++) {
                float y = svf_next(&sv, (float)(2 * t - 1));
                t += 55.0 / SR;
                if (t >= 1) t -= 1;
                if (!isfinite(y)) bad++;
                else if (fabsf(y) > worst) worst = fabsf(y);
            }
        }
    }
    snprintf(what, sizeof what, "SVF stable in every mode at full resonance through a full sweep (largest output %.1f)", worst);
    check(!bad && worst < 40, what);
}

static double rms_of(void *e, int n) {
    static float x[SR];
    render_f(e, x, n);
    double sq = 0;
    for (int i = n / 2; i < n; i++) sq += x[i] * x[i];
    return sqrt(sq / (n - n / 2));
}

/* where a source's sound goes: parallel keeps the two filters apart, series feeds the SVF into the ladder */
static void routing(void) {
    static const struct { const char *what; int route, series, mix; int sounds; } t[] = {
        {"osc 1 to the SVF, parallel, mix all ladder: silent", 1, 0, 100, 0},
        {"osc 1 to the SVF, parallel, mix all SVF: sounds", 1, 0, 0, 1},
        {"osc 1 to the SVF, series, mix all ladder: sounds (SVF feeds the ladder)", 1, 1, 100, 1},
        {"osc 1 to the ladder, series, mix all SVF: silent (the SVF gets nothing)", 0, 1, 0, 0},
        {"osc 1 to both, parallel, mix half and half: sounds", 2, 0, 50, 1},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        void *e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "o1_route", t[i].route); set(e, "flt_route", t[i].series); set(e, "flt_mix", t[i].mix);
        note(e, 1, 48, 127);
        double r = rms_of(e, SR / 2);
        char what[160];
        snprintf(what, sizeof what, "%s (rms %.3f)", t[i].what, r);
        check(t[i].sounds ? r > 0.05 : r < 0.001, what);
        E->destroy(e);
    }
    void *e = E->create(NULL);
    char a[32];
    set(e, "svf_cut", 50); E->get_param(e, "svf_cut_display", a, sizeof a);
    char what[96];
    snprintf(what, sizeof what, "SVF cutoff knob reads \"%s\" at 50", a);
    check(!strcmp(a, "632 Hz"), what);
    E->destroy(e);
}

/* window peak of |x| every ms */
static int time_to(const float *x, int n, float frac) {
    float top = 0;
    for (int i = 0; i < n; i++) top = fmaxf(top, fabsf(x[i]));
    for (int w = 0; w + 44 <= n; w += 44) {
        float pk = 0;
        for (int i = w; i < w + 44; i++) pk = fmaxf(pk, fabsf(x[i]));
        if (pk >= frac * top) return w;
    }
    return n;
}

/* rms of the second difference against the rms: grows with the square of frequency, so high harmonics dominate */
static double brightness(const float *x, int n) {
    double d = 0, a = 0;
    for (int i = 2; i < n; i++) {
        double c = x[i] - 2 * x[i - 1] + x[i - 2];
        d += c * c;
        a += x[i] * x[i];
    }
    return sqrt(d / (a + 1e-30));
}

static void modulation(void) {
    static float x[SR * 3];
    char what[200], txt[32];
    void *e = patch(WAVE_SAW, 100, 1000, 100, 100);
    E->get_param(e, "amp_a_display", txt, sizeof txt);
    note(e, 1, 69, 127);
    render_f(e, x, SR / 2);
    double ms = time_to(x, SR / 2, 0.95f) / 44.1;
    snprintf(what, sizeof what, "amp attack knob reads \"%s\" and the note reaches 95%% after %.0f ms", txt, ms);
    check(!strcmp(txt, "100 ms") && ms > 85 && ms < 105, what);
    E->destroy(e);

    e = patch(WAVE_SAW, 0, 1000, 100, 100);   /* filter envelope: a pluck that opens the ladder and closes it */
    set(e, "flt_cut", 20); set(e, "lad_env", 100); set(e, "fenv_d", knob_ms(500)); set(e, "fenv_s", 0);
    note(e, 1, 48, 127);
    render_f(e, x, SR);
    /* the envelope moves the cutoff in octaves, so it falls fast at first: compare the first 20 ms with later */
    double early = brightness(x + 44, 838), late = brightness(x + SR - 4410, 4410);
    snprintf(what, sizeof what, "filter envelope: brightness %.4f in the first 20 ms, %.4f after a second", early, late);
    check(early > 3 * late, what);
    E->destroy(e);

    double f[2];   /* keyboard tracking: a self-oscillating ladder at 100% tracking plays the keys in tune */
    for (int k = 0; k < 2; k++) {
        e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(440)); set(e, "lad_kbd", 100);
        note(e, 1, 60 + 12 * k, 127);
        render_f(e, x, SR * 2);
        f[k] = peak_hz(x + SR * 2 - N);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "ladder keyboard tracking 100%%: C4 rings at %.1f Hz, C5 at %.1f Hz (ratio %.3f)", f[0], f[1], f[1] / f[0]);
    check(fabs(f[1] / f[0] - 2) < 0.02, what);

    for (int synced = 0; synced < 2; synced++) {   /* vibrato: a square LFO 1 at 1 Hz, +-12 semitones */
        e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "pitch_lfo", 100);
        if (synced) { E->set_param(e, "lfo_bpm", "120"); set(e, "lfo1_sync", 1); set(e, "lfo1_rate", 30); }
        else set(e, "lfo1_rate", 100 * log(1 / 0.05) / log(1000));
        E->get_param(e, "lfo1_rate_display", txt, sizeof txt);
        note(e, 1, 57, 127);
        render_f(e, x, SR);
        double hi = peak_hz(x + 2205), lo = peak_hz(x + SR / 2 + 2205);
        snprintf(what, sizeof what, "LFO 1 %s (\"%s\"): pitch %.1f Hz in the first half second, %.1f Hz in the second (want 440 / 110: A3 +-12 st)",
                 synced ? "synced to 1/2 at 120 BPM" : "free at 1 Hz", txt, hi, lo);
        check(fabs(cents(hi, 440)) < 30 && fabs(cents(lo, 110)) < 30 && !strcmp(txt, synced ? "1/2" : "1.0 Hz"), what);
        E->destroy(e);
    }

    e = patch(WAVE_SAW, 0, 1, 100, 10);   /* pitch envelope: a 2 s rise of two octaves */
    set(e, "menv_a", knob_ms(2000)); set(e, "menv_s", 100); set(e, "pitch_env", 100);
    note(e, 1, 45, 127);
    render_f(e, x, SR * 3);
    double start = peak_hz(x), end = peak_hz(x + SR * 3 - N);
    snprintf(what, sizeof what, "mod envelope to pitch: %.1f Hz at the start, %.1f Hz after its 2 s attack (want 440)", start, end);
    check(start < 200 && fabs(cents(end, 440)) < 30, what);
    E->destroy(e);

    e = patch(WAVE_SAW, 0, 1000, 100, 20);   /* a soft note after a loud one starts at its own level */
    note(e, 1, 60, 127);
    render_f(e, x, SR / 4);
    note(e, 0, 60, 0);
    render_f(e, x, SR / 2);
    note(e, 1, 60, 20);
    render_f(e, x, SR / 10);
    double head = 0, body = 0;
    for (int i = 0; i < 132; i++) head = fmax(head, fabs(x[i]));
    for (int i = 2205; i < 4410; i++) body = fmax(body, fabs(x[i]));
    snprintf(what, sizeof what, "a velocity-20 note after a velocity-127 one: first 3 ms peak %.3f, settled peak %.3f", head, body);
    check(head < 1.3 * body, what);
    E->destroy(e);
}

static void midi3(void *e, int st, int d1, int d2) {
    uint8_t m[3] = {(uint8_t)st, (uint8_t)d1, (uint8_t)d2};
    E->midi(e, m, 3);
}

static void matrix(void) {
    static float x[SR];
    char what[200];
    float slots[MOD_SLOTS * MOD_SLOT_PARAMS] = {0}, src[MS_COUNT] = {0}, dst[MD_COUNT];
    float a[] = {MS_VEL, MD_PITCH, 50, MS_OFF}, b[] = {MS_VEL, MD_PITCH, 50, MS_WHEEL}, c[] = {MS_LFO1, MD_AMP, -100, MS_OFF};
    memcpy(slots, a, sizeof a); memcpy(slots + 4, b, sizeof b); memcpy(slots + 8, c, sizeof c);
    src[MS_VEL] = 1; src[MS_WHEEL] = 0.5f; src[MS_LFO1] = 0.25f;
    mod_eval(slots, src, dst);
    snprintf(what, sizeof what, "matrix: two velocity slots, one via the half-up wheel, sum to %.3f (want 0.750); LFO to amp %.3f",
             dst[MD_PITCH], dst[MD_AMP]);
    check(fabsf(dst[MD_PITCH] - 0.75f) < 1e-6f && fabsf(dst[MD_AMP] + 0.25f) < 1e-6f, what);

    for (int wheel = 0; wheel < 2; wheel++) {   /* the default slot 1 (LFO 1 -> pitch via mod wheel), at full amount */
        void *e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "mod1_amt", 100); set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log(1 / 0.05) / log(1000));
        midi3(e, 0xb0, 1, wheel ? 127 : 0);
        note(e, 1, 57, 127);
        render_f(e, x, SR);
        double hi = peak_hz(x + 2205), lo = peak_hz(x + SR / 2 + 2205);
        snprintf(what, sizeof what, "mod wheel %s: %.1f Hz then %.1f Hz (want %s)", wheel ? "up" : "down", hi, lo,
                 wheel ? "880 / 55: +-24 st" : "220 / 220");
        check(wheel ? fabs(cents(hi, 880)) < 30 && fabs(cents(lo, 55)) < 30 : fabs(cents(hi, 220)) < 5 && fabs(cents(lo, 220)) < 5, what);
        E->destroy(e);
    }

    double br[2];   /* velocity -> ladder cutoff */
    for (int k = 0; k < 2; k++) {
        void *e = patch(WAVE_SAW, 0, 1, 100, 10);
        set(e, "flt_cut", 30); set(e, "mod2_src", MS_VEL); set(e, "mod2_dst", MD_LAD_CUT); set(e, "mod2_amt", 60);
        note(e, 1, 48, k ? 127 : 20);
        render_f(e, x, SR / 4);
        br[k] = brightness(x + SR / 8, SR / 8);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "velocity to ladder cutoff: brightness %.4f at velocity 20, %.4f at 127", br[0], br[1]);
    /* cutoff about 220 Hz vs 1.3 kHz here; this measure reads 0.0029 for a fixed 1.28 kHz ladder on this saw */
    check(br[1] > 2 * br[0], what);

    void *e = patch(WAVE_SAW, 0, 1, 100, 10);   /* aftertouch -> osc 1 level, from a level of 0 */
    set(e, "o1_level", 0); set(e, "mod3_src", MS_AT); set(e, "mod3_dst", MD_O1_LEVEL); set(e, "mod3_amt", 100);
    note(e, 1, 48, 127);
    double before = rms_of(e, SR / 4);
    uint8_t at[2] = {0xd0, 127};
    E->midi(e, at, 2);
    double after = rms_of(e, SR / 4);
    snprintf(what, sizeof what, "aftertouch to osc 1 level: rms %.3f before pressing, %.3f with full pressure", before, after);
    check(before < 0.001 && after > 0.05, what);
    E->destroy(e);

    e = patch(WAVE_SAW, 0, 1, 100, 10);   /* pitch bend, default range 2 semitones */
    midi3(e, 0xe0, 0x7f, 0x7f);
    note(e, 1, 57, 127);
    render_f(e, x, N + 4096);
    double up = peak_hz(x + 4096);
    snprintf(what, sizeof what, "pitch bend fully up: %.1f Hz (want %.1f, +2 st)", up, 220 * pow(2, 2 / 12.0));
    check(fabs(cents(up, 220 * pow(2, 2 / 12.0))) < 3, what);
    E->destroy(e);
}

static void cutoff_text(void) {
    void *e = E->create(NULL);
    char a[32], b[32], c[32];
    set(e, "flt_cut", 0); E->get_param(e, "flt_cut_display", a, sizeof a);
    set(e, "flt_cut", 50); E->get_param(e, "flt_cut_display", b, sizeof b);
    set(e, "flt_cut", 100); E->get_param(e, "flt_cut_display", c, sizeof c);
    char what[160];
    snprintf(what, sizeof what, "cutoff knob reads \"%s\", \"%s\", \"%s\" at 0, 50, 100", a, b, c);
    check(!strcmp(a, "20 Hz") && !strcmp(b, "632 Hz") && !strcmp(c, "20.00 kHz"), what);
    E->destroy(e);
}

static void noise_and_mix(void) {
    static float x[SR];
    void *e = patch(WAVE_SAW, 0, 1, 100, 10);
    set(e, "o1_level", 0); set(e, "noise_level", 100);
    note(e, 1, 60, 127);
    render_f(e, x, SR);
    double sum = 0, sq = 0;
    for (int i = 4096; i < SR; i++) { sum += x[i]; sq += x[i] * x[i]; }
    double mean = sum / (SR - 4096), rms = sqrt(sq / (SR - 4096));
    char what[128];
    /* 0.29 raw; the ladder, even fully open, rolls off the top octave, which in white noise is half the power */
    snprintf(what, sizeof what, "noise alone: rms %.3f (want about 0.24 through the open filter), mean %+.4f", rms, mean);
    check(rms > 0.21 && rms < 0.28 && fabs(mean) < 0.01, what);
    E->destroy(e);

    e = patch(WAVE_SAW, 0, 1, 100, 10);
    set(e, "o2_level", 100); set(e, "o3_level", 100); set(e, "noise_level", 100);
    note(e, 1, 36, 127);
    render_f(e, x, SR);
    float peak = 0;
    for (int i = 0; i < SR; i++) peak = fmaxf(peak, fabsf(x[i]));
    snprintf(what, sizeof what, "three oscillators and noise at full level peak at %.2f: limited, not clipped", peak);
    check(peak > 0.75f && peak < 1.0f, what);
    E->destroy(e);
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
    check(silent_after(e, 10), "All Sound Off (CC 120) is silent within 10 ms (a 2 ms fade, no click)");
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
    check(get(b, "amp_a") == 0 && get(b, "o1_wave") == 1 && get(b, "amp_r") == 0,
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

static int16_t *play(void *e, int16_t *o, int n, int blocks) {   /* one note, `blocks` x 128 frames long */
    note(e, 1, n, 110);
    for (int b = 0; b < blocks; b++, o += 256) E->render(e, o, 128);
    note(e, 0, n, 0);
    return o;
}

static int16_t *rest(void *e, int16_t *o, int blocks) {
    for (int b = 0; b < blocks; b++, o += 256) E->render(e, o, 128);
    return o;
}

/* 1: one saw (the Phase 2 sound); 2: three oscillators, osc 2 detuned and osc 3 a square an octave down; then a
 * resonant ladder sweep, plain and driven; an SVF band-pass sweep, then SVF high-pass into the ladder;
 * an envelope pluck, a tempo-synced LFO wobble and a vibrato lead;
 * 3: a sync lead, osc 2 synced to 1 and rising a semitone per note; 4: noise hits; 5: a run to the top of the keyboard */
static void demo_wav(const char *path) {
    enum { STEP = 86, RUN = 43, TAIL = 172 };   /* in 128-frame blocks: 0.25 s, 0.125 s, 0.5 s */
    static const int riff[] = {36, 48, 43, 46, 36, 48, 51, 50};
    static int16_t pcm[(11 * 8 * STEP + 13 * RUN + 7 * TAIL) * 256];
    int16_t *o = pcm;
    void *e = patch(WAVE_SAW, 5, 400, 40, 150);
    for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
    set(e, "o2_level", 80); set(e, "o2_fine", 8);
    set(e, "o3_level", 60); set(e, "o3_wave", WAVE_SQUARE); set(e, "o3_oct", 1);
    for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(WAVE_SAW, 5, 600, 60, 200);
    set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "o2_sync", 1);
    for (int k = 0; k < 8; k++) {
        set(e, "o2_semi", k + 3);
        o = play(e, o, riff[k] + 24, STEP);
    }
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(WAVE_SAW, 3, 300, 70, 120);   /* ladder: a resonant sweep up the riff twice, the second time driven */
    set(e, "o2_level", 80); set(e, "o2_fine", 7); set(e, "flt_res", 75);
    for (int k = 0; k < 16; k++) {
        if (k == 8) set(e, "flt_drive", 80);
        set(e, "flt_cut", 15 + 60 * (k % 8) / 7.0);
        o = play(e, o, riff[k % 8], STEP);
    }
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(WAVE_SAW, 3, 300, 70, 120);   /* SVF: band-pass sweep in parallel, then SVF high-pass into the ladder */
    set(e, "o1_route", 1); set(e, "o2_level", 80); set(e, "o2_fine", 7); set(e, "o2_route", 1);
    set(e, "svf_mode", SVF_BP); set(e, "svf_res", 70); set(e, "flt_mix", 0);
    for (int k = 0; k < 16; k++) {
        if (k == 8) { set(e, "svf_mode", SVF_HP); set(e, "flt_route", 1); set(e, "flt_mix", 100);
                      set(e, "flt_cut", 70); set(e, "flt_res", 40); }
        set(e, "svf_cut", 25 + 50 * (k % 8) / 7.0);
        o = play(e, o, riff[k % 8], STEP);
    }
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(WAVE_SAW, 1, 600, 0, 150);   /* modulation: an envelope pluck, a synced LFO wobble, a vibrato lead */
    set(e, "o2_level", 70); set(e, "o2_oct", 1); set(e, "flt_cut", 25); set(e, "flt_res", 55);
    set(e, "lad_env", 70); set(e, "fenv_d", knob_ms(250)); set(e, "fenv_s", 0);
    for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
    set(e, "amp_s", 100); set(e, "lad_env", 0); set(e, "flt_cut", 35); set(e, "lad_lfo", 60);
    set(e, "lfo2_wave", LFO_SINE); set(e, "lfo2_sync", 1); set(e, "lfo2_rate", 60);   /* 1/8 at 120 BPM */
    for (int k = 0; k < 4; k++) o = play(e, o, riff[2 * k], 2 * STEP);
    set(e, "lad_lfo", 0); set(e, "flt_cut", 80); set(e, "o2_oct", 2); set(e, "o2_fine", 6);
    set(e, "pitch_lfo", 3); set(e, "lfo1_wave", LFO_SINE);
    for (int k = 0; k < 4; k++) o = play(e, o, riff[k] + 24, 2 * STEP);
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(WAVE_SAW, 0, 120, 0, 120);
    set(e, "o1_level", 0); set(e, "noise_level", 100);
    for (int k = 0; k < 8; k++) o = play(e, o, 60, STEP);
    E->destroy(e);

    e = patch(WAVE_SAW, 2, 200, 80, 60);
    for (int n = 72; n <= 108; n += 3) o = play(e, o, n, RUN);   /* 13 notes */
    o = rest(e, o, TAIL);
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
    sync_aliasing();
    tuning();
    noise_and_mix();
    self_oscillation();
    filter_stability();
    filter_slopes();
    cutoff_text();
    svf_shapes();
    routing();
    modulation();
    matrix();
    envelope();
    keys();
    state();
    demo_wav(argc > 1 ? argv[1] : "demo.wav");
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
