/* ONE-ish offline checks beyond tools/test_port.sh, run through the engine API (mpc_engine()) on x86:
 *   aliasing  high saw, square, triangle, narrow pulse and blended notes: power that is not a harmonic of the note
 *             (aliasing folds back as inharmonic tones, the "whistle"), against a naive oscillator at the same pitch
 *   sync      the same for oscillator 2 hard-synced to oscillator 1, against a naive hard sync
 *   shapes    wave angle makes a triangle, pulse width adds even harmonics, ring mod gives sum and difference
 *             tones without the inputs, linear FM adds sidebands, every FM route stays bounded
 *   tuning    octave, frequency and beat frequency move each oscillator's pitch by that much
 *   noise/mix noise level; all sources at full level are limited, not clipped
 *   filter    self-oscillation is a clean, steady sine near the cutoff; the ladder alone stays stable in every mode
 *             and slope at full resonance and drive; LP/HP slopes measure 24/18/12/6 dB; the cutoff knob reads in Hz
 *   svf       LP/BP/HP/notch shapes, the resonance peak, stability at full resonance
 *   routing   parallel/series, the filter mix and each source's SVF/ladder buttons send the sound where they say
 *   mod       envelope timing and knob text, filter envelope, keyboard tracking, LFO vibrato free and tempo-synced,
 *             pitch envelope, a soft note after a loud one starts at its own level
 *   matrix    amounts and via scaling add up; mod wheel vibrato (the default slot 1); velocity to cutoff; aftertouch
 *             to a level; pitch bend
 *   voices    chords; with 4 voices six held keys keep the newest four; a repeated key doesn't double up; the
 *             sustain pedal holds and lets go; mono plays only the newest key
 *   effects   delay echoes on time (free and synced), ping-pong alternates, reverb tail grows with decay, chorus is
 *             stereo; every effect stays bounded at its maximum feedback or decay
 *   envelope  a released note reaches silence; full velocity and volume do not clip
 *   keys      repeated keys, mono fallback to a held key, All Notes Off / All Sound Off
 *   glide     LCT takes the same time for any interval, LCR twice as long for twice the interval, EXP slows as it
 *             nears the note; off is instant; a fresh voice doesn't glide, a used one glides from its last note;
 *             legato glide skips detached notes; gated glide holds still while the key is up; glissando steps in
 *             semitones; keyboard tracking follows the glide
 *   unison    a stack at detune 0 is in phase (2x one voice for 4, after the 1/sqrt(n) level), detune spreads the
 *             pitches up to +-50 cents and leaves a stack about as loud as one voice; stacks share the voice pool
 *             (8 voices, count 4: two notes, a third steals a whole stack); never more voices than the voice count;
 *             mono + unison stacks one note; release lets go of the whole stack; pitch variance stays within range
 *   state     save/restore round trip; damaged or oversized states don't break the instance
 *   wav       writes test/out/demo.wav to listen to
 *   bench     (argv[1] == "bench") microseconds per 128-frame block at 1, 4 and 8 voices on this machine; NOT the
 *             MPC's number
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
#include "env.h"
#include "modmatrix.h"
#include "fx.h"

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

/* an oscillator setting: Tri/Saw, wave angle, pulse width and wave mix (%) */
typedef struct { const char *name; int wave; double angle, pw, mix; } shape_t;
static const shape_t SAW = {"saw", WAVE_SAW, 0, 50, 0}, SQUARE = {"square", WAVE_SAW, 0, 50, 100};

static void set_shape(void *e, const char *osc, shape_t sh) {
    char k[16];
    snprintf(k, sizeof k, "%s_wave", osc); set(e, k, sh.wave);
    snprintf(k, sizeof k, "%s_angle", osc); set(e, k, sh.angle);
    snprintf(k, sizeof k, "%s_pw", osc); set(e, k, sh.pw);
    snprintf(k, sizeof k, "%s_mix", osc); set(e, k, sh.mix);
}

/* the same shape without band-limiting (osc.h's wave functions sampled directly) */
static void naive(float *x, int n, double f0, shape_t sh) {
    double t = 0, dt = f0 / SR;
    osc_shape_t s = osc_shape(sh.wave, sh.angle / 100, sh.pw / 100, sh.mix / 100, dt);
    for (int i = 0; i < n; i++) {
        x[i] = ((1 - s.mix) * osc_tri(&s, (float)t) + s.mix * osc_pulse(&s, (float)t)) * 0.35f;
        t += dt;
        if (t >= 1) t -= 1;
    }
}

/* power within 6 bins of f, in dB (relative units) */
static double band_db(const float *x, double f) {
    const double *pw = spectrum(x);
    double sum = 0, bin = (double)SR / N;
    for (int k = (int)(f / bin) - 6; k <= (int)(f / bin) + 6; k++) sum += pw[k];
    return 10 * log10(sum + 1e-30);
}

static double knob_ms(double ms) { return ms <= 1 ? 0 : 25 * log10(ms); }   /* envelope knobs: 1 ms .. 10 s */

/* amp envelope in ms (attack, decay, release) and % (sustain) */
static void *patch(shape_t wave, double a, double d, double s, double r) {
    void *e = E->create(NULL);
    set_shape(e, "o1", wave); set(e, "amp_a", knob_ms(a)); set(e, "amp_d", knob_ms(d)); set(e, "amp_s", s);
    set(e, "amp_r", knob_ms(r));
    set(e, "volume", 100);
    return e;
}

static void aliasing(void) {
    static float x[N + 4096];
    static const int notes[] = {84, 96, 103, 108};
    static const shape_t shapes[] = {
        {"saw", WAVE_SAW, 0, 50, 0}, {"square", WAVE_SAW, 0, 50, 100}, {"tri", WAVE_TRI, 50, 50, 0},
        {"ramp", WAVE_TRI, 85, 50, 0}, {"pls 15%", WAVE_SAW, 0, 15, 100}, {"saw+pls", WAVE_SAW, 0, 30, 50},
    };
    for (unsigned w = 0; w < sizeof shapes / sizeof shapes[0]; w++)
        for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; n++) {
            double f0 = 440 * pow(2, (notes[n] - 69) / 12.0);
            void *e = patch(shapes[w], 0, 1, 100, 10);
            note(e, 1, notes[n], 127);
            render_f(e, x, N + 4096);   /* the first 4096 frames are the attack: skipped */
            double ours = alias_db(x + 4096, f0);
            E->destroy(e);
            naive(x, N, f0, shapes[w]);
            double ref = alias_db(x, f0);
            char what[160];
            snprintf(what, sizeof what, "%-7s note %3d (%5.0f Hz): aliasing %6.1f dB, naive oscillator %6.1f dB",
                     shapes[w].name, notes[n], f0, ours, ref);
            /* regression guard. First measured 2026-10-27: -25.8 dB (saw, note 108) to -34.4 dB, 15-18 dB below naive.
             * A triangle aliases little even naive (its corners fall at 12 dB per octave), so it only has to stay low */
            check(shapes[w].wave == WAVE_TRI ? ours < -40 : ours < ref - 12 && ours < -24, what);
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
        void *e = patch(SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "o2_sync", 1); set(e, "o2_freq", 7);
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

/* the oscillator's shapes and the ring modulator and FM, by their spectra at A3 (220 Hz, harmonics well clear) */
static void shapes(void) {
    static float x[N + 4096];
    const shape_t tri = {"tri", WAVE_TRI, 50, 50, 0};
    char what[160];

    void *e = patch(tri, 0, 1, 100, 10);   /* a triangle's third harmonic is 1/9 of its first: -19.1 dB */
    note(e, 1, 57, 127);
    render_f(e, x, N + 4096);
    double h3 = band_db(x + 4096, 660) - band_db(x + 4096, 220), h2 = band_db(x + 4096, 440) - band_db(x + 4096, 220);
    snprintf(what, sizeof what, "Tri at 50%% angle is a triangle: 3rd harmonic %.1f dB (want -19.1), 2nd %.1f dB", h3, h2);
    check(fabs(h3 + 19.1) < 1 && h2 < -50, what);
    E->destroy(e);

    double h2_sq = 0;
    for (int w = 0; w < 2; w++) {   /* a square has no even harmonics; a 25% pulse's 2nd is 1/sqrt(2) of its 1st */
        e = patch((shape_t){"pulse", WAVE_SAW, 0, w ? 25 : 50, 100}, 0, 1, 100, 10);
        note(e, 1, 57, 127);
        render_f(e, x, N + 4096);
        double h = band_db(x + 4096, 440) - band_db(x + 4096, 220);
        if (!w) h2_sq = h;
        else {
            snprintf(what, sizeof what, "pulse width: 2nd harmonic %.1f dB at 50%%, %.1f dB at 25%% (want -3.0)", h2_sq, h);
            check(h2_sq < -50 && fabs(h + 3.0) < 1, what);
        }
        E->destroy(e);
    }

    e = patch(tri, 0, 1, 100, 10);   /* ring mod of 220 and 299 Hz triangles: 79 and 519 Hz, not 220 or 299 */
    set_shape(e, "o2", tri); set(e, "o2_freq", 5.3);
    set(e, "o1_level", 0); set(e, "ring_level", 100);
    note(e, 1, 57, 127);
    render_f(e, x, N + 4096);
    double tones = fmax(band_db(x + 4096, 78.8), band_db(x + 4096, 518.8));
    double inputs = fmax(band_db(x + 4096, 220), band_db(x + 4096, 298.8));
    snprintf(what, sizeof what, "ring mod 1 x 2: sum and difference tones %.1f dB above the inputs", tones - inputs);
    check(tones - inputs > 25, what);
    set(e, "ring_src", 1);   /* 2 x 3: osc 3 at 220 Hz, so the same tones */
    set_shape(e, "o3", tri);
    note(e, 0, 57, 0); note(e, 1, 57, 127);
    render_f(e, x, N + 4096);
    tones = fmax(band_db(x + 4096, 78.8), band_db(x + 4096, 518.8));
    inputs = fmax(band_db(x + 4096, 220), band_db(x + 4096, 298.8));
    snprintf(what, sizeof what, "ring mod 2 x 3: sum and difference tones %.1f dB above the inputs", tones - inputs);
    check(tones - inputs > 25, what);
    E->destroy(e);

    for (int amt = 0; amt <= 50; amt += 50) {   /* FM 1 > 2, both triangles at 220 Hz: sidebands at 440 Hz appear */
        e = patch(tri, 0, 1, 100, 10);
        set_shape(e, "o2", tri); set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "fm_amt", amt);
        note(e, 1, 57, 127);
        render_f(e, x, N + 4096);
        double side = band_db(x + 4096, 440) - band_db(x + 4096, 220), al = alias_db(x + 4096, 220);
        snprintf(what, sizeof what, "FM 1 > 2 at %d%%: 440 Hz sideband %.1f dB, inharmonic power %.1f dB", amt, side, al);
        check(amt ? side > -15 && al < -40 : side < -50, what);
        E->destroy(e);
    }
    for (int r = 0; r < 3; r++) {   /* every route at full depth, with sync and a high note, stays bounded */
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "o2_level", 100); set(e, "o3_level", 100); set(e, "o2_sync", 1); set(e, "o3_sync", 1);
        set(e, "fm_amt", 100); set(e, "fm_route", r); set(e, "o3_freq", 7);
        note(e, 1, 100, 127); note(e, 1, 30, 127);
        render_f(e, x, N + 4096);
        float peak = 0;
        int finite = 1;
        for (int i = 0; i < N + 4096; i++) { peak = fmaxf(peak, fabsf(x[i])); finite &= isfinite(x[i]); }
        snprintf(what, sizeof what, "FM route %d at 100%% with sync: peak %.2f, finite", r, peak);
        check(finite && peak > 0.05f && peak < 1.0f, what);
        E->destroy(e);
    }
}

static double cents(double hz, double want) { return 1200 * log2(hz / want); }

/* each oscillator's octave, frequency and beat frequency settings move its pitch by exactly that much */
static void tuning(void) {
    static float x[N + 4096];
    static const struct { const char *osc, *key; double val, ratio, plus_hz; } t[] = {
        {"o1", "o1_oct", 1, 0.5, 0}, {"o1", "o1_oct", 4, 4.0, 0}, {"o1", "o1_freq", 7, 1.4983070768766815, 0},
        {"o1", "o1_freq", -7, 0.6674199270850172, 0}, {"o1", "o1_freq", 0.5, 1.0293022366434921, 0},
        {"o2", "o2_oct", 3, 2.0, 0}, {"o3", "o3_freq", -0.25, 0.98566319864018, 0}, {"o2", "o2_beat", 3, 1, 3},
        {"o3", "o3_beat", -7, 1, -7},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        void *e = patch(SAW, 0, 1, 100, 10);
        char lvl[16];
        snprintf(lvl, sizeof lvl, "%s_level", t[i].osc);
        set(e, "o1_level", 0); set(e, lvl, 100); set(e, t[i].key, t[i].val);
        note(e, 1, 69, 127);
        render_f(e, x, N + 4096);
        double want = 440 * t[i].ratio + t[i].plus_hz, got = peak_hz(x + 4096), off = cents(got, want);
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
        void *e = patch(SAW, 0, 1, 100, 10);
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

/* a mixer channel's two filter buttons: 0 ladder, 1 SVF, 2 both, 3 neither */
static void route(void *e, const char *ch, int r) {
    char k[16];
    snprintf(k, sizeof k, "%s_lad", ch); set(e, k, r == 0 || r == 2);
    snprintf(k, sizeof k, "%s_svf", ch); set(e, k, r == 1 || r == 2);
}

/* where a source's sound goes: parallel keeps the two filters apart, series feeds the SVF into the ladder */
static void routing(void) {
    static const struct { const char *what; int route, series, mix; int sounds; } t[] = {
        {"osc 1 to neither filter: silent", 3, 0, 50, 0},
        {"osc 1 to the SVF, parallel, mix all ladder: silent", 1, 0, 100, 0},
        {"osc 1 to the SVF, parallel, mix all SVF: sounds", 1, 0, 0, 1},
        {"osc 1 to the SVF, series, mix all ladder: sounds (SVF feeds the ladder)", 1, 1, 100, 1},
        {"osc 1 to the ladder, series, mix all SVF: silent (the SVF gets nothing)", 0, 1, 0, 0},
        {"osc 1 to both, parallel, mix half and half: sounds", 2, 0, 50, 1},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        void *e = patch(SAW, 0, 1, 100, 10);
        route(e, "o1", t[i].route); set(e, "flt_route", t[i].series); set(e, "flt_mix", t[i].mix);
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
    void *e = patch(SAW, 100, 1000, 100, 100);
    E->get_param(e, "amp_a_display", txt, sizeof txt);
    note(e, 1, 69, 127);
    render_f(e, x, SR / 2);
    double ms = time_to(x, SR / 2, 0.95f) / 44.1;
    snprintf(what, sizeof what, "amp attack knob reads \"%s\" and the note reaches 95%% after %.0f ms", txt, ms);
    check(!strcmp(txt, "100 ms") && ms > 85 && ms < 105, what);
    E->destroy(e);

    e = patch(SAW, 0, 1000, 100, 100);   /* filter envelope: a pluck that opens the ladder and closes it */
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
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(440)); set(e, "lad_kbd", 100);
        note(e, 1, 60 + 12 * k, 127);
        render_f(e, x, SR * 2);
        f[k] = peak_hz(x + SR * 2 - N);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "ladder keyboard tracking 100%%: C4 rings at %.1f Hz, C5 at %.1f Hz (ratio %.3f)", f[0], f[1], f[1] / f[0]);
    check(fabs(f[1] / f[0] - 2) < 0.02, what);

    for (int synced = 0; synced < 2; synced++) {   /* vibrato: a square LFO 1 at 1 Hz, +-12 semitones */
        e = patch(SAW, 0, 1, 100, 10);
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

    e = patch(SAW, 0, 1, 100, 10);   /* pitch envelope: a 2 s rise of two octaves */
    set(e, "menv_a", knob_ms(2000)); set(e, "menv_s", 100); set(e, "pitch_env", 100);
    note(e, 1, 45, 127);
    render_f(e, x, SR * 3);
    double start = peak_hz(x), end = peak_hz(x + SR * 3 - N);
    snprintf(what, sizeof what, "mod envelope to pitch: %.1f Hz at the start, %.1f Hz after its 2 s attack (want 440)", start, end);
    check(start < 200 && fabs(cents(end, 440)) < 30, what);
    E->destroy(e);

    e = patch(SAW, 0, 1000, 100, 20);   /* a soft note after a loud one starts at its own level */
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
        void *e = patch(SAW, 0, 1, 100, 10);
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
        void *e = patch(SAW, 0, 1, 100, 10);
        set(e, "flt_cut", 30); set(e, "mod2_src", MS_VEL); set(e, "mod2_dst", MD_LAD_CUT); set(e, "mod2_amt", 60);
        note(e, 1, 48, k ? 127 : 20);
        render_f(e, x, SR / 4);
        br[k] = brightness(x + SR / 8, SR / 8);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "velocity to ladder cutoff: brightness %.4f at velocity 20, %.4f at 127", br[0], br[1]);
    /* cutoff about 220 Hz vs 1.3 kHz here; this measure reads 0.0029 for a fixed 1.28 kHz ladder on this saw */
    check(br[1] > 2 * br[0], what);

    void *e = patch(SAW, 0, 1, 100, 10);   /* aftertouch -> osc 1 level, from a level of 0 */
    set(e, "o1_level", 0); set(e, "mod3_src", MS_AT); set(e, "mod3_dst", MD_O1_LEVEL); set(e, "mod3_amt", 100);
    note(e, 1, 48, 127);
    double before = rms_of(e, SR / 4);
    uint8_t at[2] = {0xd0, 127};
    E->midi(e, at, 2);
    double after = rms_of(e, SR / 4);
    snprintf(what, sizeof what, "aftertouch to osc 1 level: rms %.3f before pressing, %.3f with full pressure", before, after);
    check(before < 0.001 && after > 0.05, what);
    E->destroy(e);

    e = patch(SAW, 0, 1, 100, 10);   /* pitch bend, default range 2 semitones */
    midi3(e, 0xe0, 0x7f, 0x7f);
    note(e, 1, 57, 127);
    render_f(e, x, N + 4096);
    double up = peak_hz(x + 4096);
    snprintf(what, sizeof what, "pitch bend fully up: %.1f Hz (want %.1f, +2 st)", up, 220 * pow(2, 2 / 12.0));
    check(fabs(cents(up, 220 * pow(2, 2 / 12.0))) < 3, what);
    E->destroy(e);
}

/* power (dB, against the strongest of them) at each frequency, +-3 bins */
static void powers_db(const float *x, const double *hz, int n, double *db) {
    const double *pw = spectrum(x);
    double bin = (double)SR / N, top = 1e-30;
    for (int i = 0; i < n; i++) {
        int k0 = (int)(hz[i] / bin + 0.5);
        double sum = 0;
        for (int k = k0 - 3; k <= k0 + 3; k++) sum += pw[k];
        db[i] = sum;
        if (sum > top) top = sum;
    }
    for (int i = 0; i < n; i++) db[i] = 10 * log10(db[i] / top + 1e-30);
}

static double note_hz(int n) { return 440 * pow(2, (n - 69) / 12.0); }

static int silent_after(void *e, int ms);

static void voices(void) {
    static float x[N + 8192];
    char what[220];
    /* six held keys, 4 voices: the oldest two are stolen. A 600 Hz ladder keeps each note close to its fundamental. */
    static const int keys[] = {48, 50, 52, 53, 55, 57};
    double hz[6], db[6];
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "flt_cut", knob_for_hz(600));
    for (int i = 0; i < 6; i++) { note(e, 1, keys[i], 100); hz[i] = note_hz(keys[i]); }
    render_f(e, x, N + 8192);
    powers_db(x + 8192, hz, 6, db);
    snprintf(what, sizeof what, "4 voices, six keys held: C3 %.0f, D3 %.0f (stolen) | E3 %.0f, F3 %.0f, G3 %.0f, A3 %.0f dB",
             db[0], db[1], db[2], db[3], db[4], db[5]);
    /* a stolen note would sit near 0 dB; what remains at -30 dB is the saturator's intermodulation of the others */
    check(db[0] < -25 && db[1] < -25 && db[2] > -12 && db[3] > -12 && db[4] > -12 && db[5] > -12, what);
    E->destroy(e);

    e = patch(SAW, 0, 1, 100, 10);   /* mono: a chord plays only its newest key */
    set(e, "voices", 0); set(e, "flt_cut", knob_for_hz(600));
    note(e, 1, 48, 100); note(e, 1, 52, 100);
    render_f(e, x, N + 8192);
    powers_db(x + 8192, hz, 3, db);
    snprintf(what, sizeof what, "mono: C3 + E3 held plays E3 only (C3 %.0f dB, E3 %.0f dB)", db[0], db[2]);
    check(db[0] < -40 && db[2] > -1, what);
    E->destroy(e);

    e = patch(SAW, 0, 1, 100, 10);   /* the same key twice replays its voice, not a second one */
    note(e, 1, 60, 100);
    double one = rms_of(e, SR / 4);
    note(e, 1, 60, 100);
    double two = rms_of(e, SR / 4);
    snprintf(what, sizeof what, "the same key twice: rms %.3f, then %.3f (one voice, not two)", one, two);
    check(two < 1.2 * one, what);
    E->destroy(e);

    e = patch(SAW, 0, 1, 100, 100);   /* sustain pedal */
    midi3(e, 0xb0, 64, 127);
    note(e, 1, 60, 100); note(e, 0, 60, 0);
    double held = rms_of(e, SR / 2);
    midi3(e, 0xb0, 64, 0);
    int quiet = silent_after(e, 1000);
    snprintf(what, sizeof what, "sustain pedal: a released key holds (rms %.3f) until the pedal lifts, then stops", held);
    check(held > 0.05 && quiet, what);
    E->destroy(e);
    e = patch(SAW, 0, 1, 100, 100);   /* a chord held while switching to mono, then released, must stop */
    note(e, 1, 48, 100); note(e, 1, 52, 100);
    render_f(e, x, SR / 10);
    set(e, "voices", 0);
    note(e, 0, 48, 0); note(e, 0, 52, 0);
    check(silent_after(e, 1000), "a chord held through a switch to mono stops when its keys are released");
    E->destroy(e);

    env_t env;   /* a pluck (sustain 0) frees its voice when the decay ends, instead of holding it at silence */
    memset(&env, 0, sizeof env);
    env_set_times(&env, 1, 50, 0, 50, SR);
    env_gate_on(&env);
    for (int i = 0; i < SR / 2; i++) env_next(&env);
    check(env.stage == ENV_IDLE, "a pluck envelope (sustain 0) is idle once its decay ends, with the key still down");
}

static fx_t fxs;   /* 1.3 MB: static, not on the stack */

static fx_params_t fx_none(void) {
    fx_params_t p;
    memset(&p, 0, sizeof p);
    p.dly_tone = 1;
    return p;
}

/* run the effects on `n` samples of `in` (NULL: an impulse at 0), into l, r */
static void fx_run(const fx_params_t *p, const float *in, float *l, float *r, int n) {
    static float x[SR * 4];
    memset(&fxs, 0, sizeof fxs);
    for (int i = 0; i < n; i++) x[i] = in ? in[i] : (i == 0);
    for (int at = 0; at < n; at += 128) fx_process(&fxs, p, x + at, l + at, r + at, n - at < 128 ? n - at : 128);
}

static int loudest(const float *x, int from, int to) {
    int best = from;
    for (int i = from; i < to; i++)
        if (fabsf(x[i]) > fabsf(x[best])) best = i;
    return best;
}

static void effects(void) {
    static float l[SR * 4], r[SR * 4], saw[SR * 4];
    char what[200];
    fx_params_t p = fx_none();
    p.dly_ms = 250; p.dly_mix = 1;   /* one echo, 250 ms after an impulse */
    fx_run(&p, NULL, l, r, SR);
    int at = loudest(l, 100, SR);
    snprintf(what, sizeof what, "delay 250 ms: echo at %.1f ms (level %.2f)", at / 44.1, l[at]);
    check(abs(at - 11025) <= 2 && l[at] > 0.8f, what);

    p.dly_fb = 0.5f; p.dly_pingpong = 1;   /* ping-pong: first repeat left, second right */
    fx_run(&p, NULL, l, r, SR);
    int l1 = loudest(l, 100, 16000), r2 = loudest(r, 16000, 27000);
    snprintf(what, sizeof what, "ping-pong: first echo left at %.0f ms (%.2f, right %.2f), second right at %.0f ms (%.2f)",
             l1 / 44.1, l[l1], r[l1], r2 / 44.1, r[r2]);
    check(abs(l1 - 11025) <= 2 && fabsf(r[l1]) < 0.01f && abs(r2 - 22050) <= 4 && r[r2] > 0.2f, what);

    for (int i = 0; i < SR * 4; i++) saw[i] = 2.0f * (float)fmod(i * 110.0 / SR, 1.0) - 1.0f;
    float worst = 0;
    int bad = 0;
    fx_params_t hot[4] = {fx_none(), fx_none(), fx_none(), fx_none()};
    hot[0].dly_ms = 80; hot[0].dly_fb = 0.95f; hot[0].dly_mix = 1; hot[0].dly_tone = 1;
    hot[1].mod_type = MODFX_FLANGER; hot[1].mod_rate_hz = 0.3f; hot[1].mod_depth = 1; hot[1].mod_fb = 0.95f; hot[1].mod_mix = 1;
    hot[2].mod_type = MODFX_PHASER; hot[2].mod_rate_hz = 0.3f; hot[2].mod_depth = 1; hot[2].mod_fb = 0.95f; hot[2].mod_mix = 1;
    hot[3].rev_decay = 0.97f; hot[3].rev_damp = 0; hot[3].rev_mix = 1;
    for (int k = 0; k < 4; k++) {
        fx_run(&hot[k], saw, l, r, SR * 4);
        for (int i = 0; i < SR * 4; i++) {
            if (!isfinite(l[i]) || !isfinite(r[i])) bad++;
            worst = fmaxf(worst, fmaxf(fabsf(l[i]), fabsf(r[i])));
        }
    }
    snprintf(what, sizeof what, "delay, flanger, phaser at 95%% feedback and reverb at full decay stay bounded (largest %.1f)", worst);
    check(!bad && worst < 25, what);

    double tail[2];   /* reverb: energy 1.0-1.5 s after an impulse, short and long decay */
    for (int k = 0; k < 2; k++) {
        p = fx_none(); p.rev_mix = 1; p.rev_decay = k ? 0.9f : 0.4f; p.rev_damp = 0.3f;
        fx_run(&p, NULL, l, r, SR * 2);
        double e = 0;
        for (int i = SR; i < SR + SR / 2; i++) e += l[i] * l[i] + r[i] * r[i];
        tail[k] = 10 * log10(e + 1e-30);
    }
    snprintf(what, sizeof what, "reverb tail 1-1.5 s after an impulse: %.0f dB at short decay, %.0f dB at long", tail[0], tail[1]);
    check(tail[1] > tail[0] + 20 && tail[1] > -60, what);

    p = fx_none(); p.mod_type = MODFX_CHORUS; p.mod_rate_hz = 0.8f; p.mod_depth = 0.7f; p.mod_mix = 0.5f;
    fx_run(&p, saw, l, r, SR);
    double diff = 0, sum = 0;
    for (int i = SR / 2; i < SR; i++) { diff += (l[i] - r[i]) * (l[i] - r[i]); sum += l[i] * l[i]; }
    snprintf(what, sizeof what, "chorus spreads to stereo: left-right difference %.0f dB below the left", 10 * log10(sum / diff));
    check(diff > sum * 0.01, what);

    void *e = patch(SAW, 0, 1, 100, 10);   /* synced delay time reads as the division */
    char txt[32];
    E->set_param(e, "lfo_bpm", "120"); set(e, "dly_sync", 1); set(e, "dly_time", 60);
    E->get_param(e, "dly_time_display", txt, sizeof txt);
    snprintf(what, sizeof what, "delay synced at 120 BPM reads \"%s\" (want 1/8)", txt);
    check(!strcmp(txt, "1/8"), what);
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
    void *e = patch(SAW, 0, 1, 100, 10);
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

    e = patch(SAW, 0, 1, 100, 10);
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
    void *e = patch(SQUARE, 0, 100, 100, 200);
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
    void *e = patch(SAW, 0, 100, 100, 50);
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
    void *a = patch(SQUARE, 123, 456, 78, 901), *b = E->create(NULL);
    set(a, "volume", 42);
    int n = E->get_param(a, "state", st, sizeof st);
    E->set_param(b, "state", st);
    int same = n > 0;
    for (int i = 0; i < NPARAMS; i++) same &= get(a, PARAMS[i].key) == get(b, PARAMS[i].key);
    check(same, "saved state restores every value on a new instance");

    E->set_param(b, "state", "amp_s=nan;volume=inf;amp_a=-50;o1_wave=7;bogus;=3;amp_r=");
    /* a state starts from the defaults (an older project lacks newer keys), so bad values leave the default */
    check(get(b, "amp_s") == 70 && get(b, "volume") == 70, "a damaged state's NaN and infinite values leave the defaults");
    check(get(b, "amp_a") == 0 && get(b, "o1_wave") == 1 && get(b, "amp_r") == 0,
          "a damaged state's out-of-range values are clamped to the param's range");
    memset(st, 'x', sizeof st - 1);
    st[sizeof st - 1] = 0;
    memcpy(st, "volume=5;", 9);
    E->set_param(b, "state", st);
    check(get(b, "volume") == 70, "a state too long for the chunk is ignored, not half restored");
    E->destroy(a);
    E->destroy(b);
}

/* the pitch over time of a near-sine: at each rising zero crossing, the pitch (MIDI note number) from the period since
 * the last one, at time t (ms from the start of x) */
enum { MAXPTS = 4096 };
static int track(const float *x, int n, double *t, double *st) {
    double last = -1;
    int k = 0;
    for (int i = 1; i < n && k < MAXPTS; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double at = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
            if (last >= 0) { t[k] = at / 44.1; st[k] = 69 + 12 * log2(SR / (at - last) / 440); k++; }
            last = at;
        }
    return k;
}

/* the tracked pitch nearest time ms */
static double pitch_at(const double *t, const double *st, int k, double ms) {
    int best = 0;
    for (int i = 1; i < k; i++)
        if (fabs(t[i] - ms) < fabs(t[best] - ms)) best = i;
    return st[best];
}

/* when the tracked pitch first comes within 25 cents of `to` (ms), or -1 */
static double arrival(const double *t, const double *st, int k, double to) {
    for (int i = 0; i < k; i++)
        if (fabs(st[i] - to) < 0.25) return t[i];
    return -1;
}

static const shape_t TRI = {"tri", WAVE_TRI, 50, 50, 0};

/* a triangle (near a sine, so zero crossings track its pitch) through the open ladder, glide on, 2 s release */
static void *glide_patch(int type, double ms, int mono) {
    void *e = patch(TRI, 0, 1, 100, 2000);
    set(e, "flt_cut", 100); set(e, "glide", 1); set(e, "glide_time", knob_ms(ms)); set(e, "glide_type", type);
    if (mono) set(e, "voices", 0);
    return e;
}

/* hold `from`, then press `to` (legato): the tracked pitch from the press on, over `ms` */
static int glide_run(void *e, int from, int to, int ms, double *t, double *st) {
    static float x[SR * 4];
    note(e, 1, from, 100);
    render_f(e, x, SR / 10);
    note(e, 1, to, 100);
    render_f(e, x, SR * ms / 1000);
    return track(x, SR * ms / 1000, t, st);
}

static void glide_unison(void) {
    static double t[MAXPTS], st[MAXPTS];
    static float x[SR * 2];   /* the longest render here is 1.5 s */
    char what[240];
    int k;

    /* LCT: one and two octaves both take the glide time (500 ms); LCR: the glide time per octave */
    double took[2][2];
    for (int type = 0; type < 2; type++)
        for (int oct = 1; oct <= 2; oct++) {
            void *e = glide_patch(type == 0 ? 1 : 0, 500, 1);
            k = glide_run(e, 45, 45 + 12 * oct, 1500, t, st);
            took[type][oct - 1] = arrival(t, st, k, 45 + 12 * oct);
            E->destroy(e);
        }
    snprintf(what, sizeof what, "LCT glide, 500 ms: one octave arrives after %.0f ms, two octaves after %.0f ms (same time)",
             took[0][0], took[0][1]);
    check(fabs(took[0][0] - 500) < 50 && fabs(took[0][1] / took[0][0] - 1) < 0.08, what);
    snprintf(what, sizeof what, "LCR glide, 500 ms per octave: one octave after %.0f ms, two after %.0f ms (twice as long)",
             took[1][0], took[1][1]);
    check(fabs(took[1][0] - 500) < 50 && fabs(took[1][1] / took[1][0] - 2) < 0.15, what);

    void *e = glide_patch(2, 600, 1);   /* EXP: quick at first, slowing as it nears the note */
    k = glide_run(e, 45, 57, 2000, t, st);
    double third = pitch_at(t, st, k, 200) - 45, two = pitch_at(t, st, k, 400) - 45, end = pitch_at(t, st, k, 1900) - 57;
    snprintf(what, sizeof what, "EXP glide, 600 ms, up an octave: %.1f st after 200 ms, %.1f after 400 ms, %+.1f cents at 1.9 s",
             third, two, end * 100);
    check(third > 6.6 && third < 8.6 && two - third < third && two - third > 1.5 && fabs(end) < 0.05, what);
    E->destroy(e);

    e = glide_patch(1, 500, 1);   /* glide off: straight to the note */
    set(e, "glide", 0);
    k = glide_run(e, 45, 57, 300, t, st);
    double worst = 0;
    for (int i = 0; i < k; i++) if (t[i] > 15) worst = fmax(worst, fabs(st[i] - 57));
    snprintf(what, sizeof what, "glide off: the new note is in tune from 15 ms on (worst %.1f cents)", worst * 100);
    check(worst < 0.1, what);
    E->destroy(e);

    e = glide_patch(1, 500, 0);   /* poly: a fresh voice starts at its note; a used one glides from its own last note */
    set(e, "voices", 1); set(e, "amp_r", knob_ms(20));
    note(e, 1, 45, 100);
    render_f(e, x, SR / 4);
    k = track(x, SR / 4, t, st);
    double fresh = fabs(pitch_at(t, st, k, 30) - 45);
    note(e, 0, 45, 0);
    render_f(e, x, SR / 2);
    note(e, 1, 57, 100);
    render_f(e, x, SR / 2);
    k = track(x, SR / 2, t, st);
    double mid = pitch_at(t, st, k, 250);
    snprintf(what, sizeof what, "poly: a fresh voice is %.1f cents off its note at 30 ms; the reused voice is at %.1f st "
             "half way through its glide from 45 to 57 (want 51)", fresh * 100, mid);
    check(fresh < 0.1 && fabs(mid - 51) < 0.6, what);
    E->destroy(e);

    double det[2];   /* legato glide: a detached note jumps; without it, the same detached note glides */
    for (int lg = 0; lg < 2; lg++) {
        e = glide_patch(1, 500, 1);
        set(e, "glide_legato", lg);
        note(e, 1, 45, 100);
        render_f(e, x, SR / 10);
        note(e, 0, 45, 0);
        render_f(e, x, SR / 20);
        note(e, 1, 57, 100);
        render_f(e, x, SR / 2);
        k = track(x, SR / 2, t, st);
        det[lg] = pitch_at(t, st, k, 125);
        E->destroy(e);
    }
    e = glide_patch(1, 500, 1);
    set(e, "glide_legato", 1);
    k = glide_run(e, 45, 57, 500, t, st);
    double leg = pitch_at(t, st, k, 250);
    snprintf(what, sizeof what, "legato glide: a detached note is at %.1f st after 125 ms (want 57; %.1f without legato "
             "glide), a legato one at %.1f st half way (want 51)", det[1], det[0], leg);
    check(fabs(det[1] - 57) < 0.1 && det[0] < 55 && fabs(leg - 51) < 0.6, what);
    E->destroy(e);

    double spread[2], held = 0, back = 0, final = 0;   /* gated glide: the pitch holds still while the key is up */
    for (int gated = 0; gated < 2; gated++) {
        e = glide_patch(1, 1000, 1);
        set(e, "glide_gated", gated);
        note(e, 1, 45, 100); note(e, 0, 45, 0);
        note(e, 1, 69, 100);
        render_f(e, x, SR * 3 / 10);
        note(e, 0, 69, 0);
        render_f(e, x, SR * 6 / 10);
        k = track(x, SR * 6 / 10, t, st);
        double lo = 200, hi = 0;
        for (int i = 0; i < k; i++) if (t[i] > 15) { lo = fmin(lo, st[i]); hi = fmax(hi, st[i]); }
        spread[gated] = hi - lo;
        if (gated) {   /* pressing the key again picks the glide up where it stopped */
            held = pitch_at(t, st, k, 590);
            note(e, 1, 69, 100);
            render_f(e, x, SR * 15 / 10);
            k = track(x, SR * 15 / 10, t, st);
            back = pitch_at(t, st, k, 15);
            final = pitch_at(t, st, k, 1400);
        }
        E->destroy(e);
    }
    snprintf(what, sizeof what, "gated glide: with the key up the pitch moves %.2f st (%.1f st ungated); pressed again it "
             "goes on from %.1f st (was %.1f) to %.1f st (want 69)", spread[1], spread[0], back, held, final);
    check(spread[1] < 0.1 && spread[0] > 3 && fabs(back - held) < 0.5 && fabs(final - 69) < 0.1, what);

    e = glide_patch(1, 1200, 1);   /* glissando: the heard pitch moves in semitone steps */
    set(e, "glissando", 1);
    k = glide_run(e, 45, 57, 1300, t, st);
    int on_step = 0, total = 0, visited[13] = {0}, steps = 0;
    for (int i = 0; i < k; i++)
        if (t[i] > 20 && t[i] < 1150) {
            total++;
            double r = round(st[i]);
            if (fabs(st[i] - r) < 0.1) { on_step++; if (r >= 45 && r <= 57 && !visited[(int)r - 45]++) steps++; }
        }
    snprintf(what, sizeof what, "glissando: %d of %d pitch readings sit on a semitone, %d different semitones on the way",
             on_step, total, steps);
    check(on_step > 0.85 * total && steps >= 11, what);
    E->destroy(e);

    e = patch(SAW, 0, 1, 100, 10);   /* keyboard tracking follows the glide: a self-oscillating ladder at 100% */
    set(e, "voices", 0); set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(440));
    set(e, "lad_kbd", 100); set(e, "glide", 1); set(e, "glide_type", 1); set(e, "glide_time", knob_ms(500));
    note(e, 1, 60, 100);
    render_f(e, x, SR);
    note(e, 1, 72, 100);
    render_f(e, x, SR);
    k = track(x, SR, t, st);
    double half = pitch_at(t, st, k, 250), done = pitch_at(t, st, k, 900);
    snprintf(what, sizeof what, "keyboard tracking follows the glide: the filter rings at %.1f st half way, %.1f st at the "
             "end (want 75, 81)", half, done);
    check(fabs(half - 75) < 0.6 && fabs(done - 81) < 0.2, what);
    E->destroy(e);

    /* unison: level, in phase at detune 0 (volume low: below the limiter) */
    double lvl[4];
    for (int c = 0; c < 4; c++) {   /* unison off; 4 at detune 0; 4 at detune 100; 8 voices asked of a 2-voice pool */
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "volume", 10); set(e, "unison", c > 0); set(e, "uni_detune", c == 2 ? 100 : 0);
        if (c == 3) { set(e, "voices", 1); set(e, "uni_count", 6); }
        note(e, 1, 57, 127);
        lvl[c] = rms_of(e, SR / 2);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "unison 4: %.2fx one voice at detune 0 (in phase, want 2), %.2fx at detune 100%% (want "
             "about 1); count 8 on 2 voices stacks 2: %.2fx (want 1.41)", lvl[1] / lvl[0], lvl[2] / lvl[0], lvl[3] / lvl[0]);
    check(fabs(lvl[1] / lvl[0] - 2) < 0.1 && lvl[2] / lvl[0] > 0.7 && lvl[2] / lvl[0] < 1.4 &&
          fabs(lvl[3] / lvl[0] - sqrt(2)) < 0.08, what);

    double inner[2], outer = 0;   /* detune spreads the stack around the note, at most +-50 cents */
    for (int d = 0; d < 2; d++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "volume", 20); set(e, "flt_cut", 100); set(e, "unison", 1); set(e, "uni_detune", d ? 100 : 0);
        note(e, 1, 93, 127);
        render_f(e, x, N + 8192);
        const double *pw = spectrum(x + 8192);
        int c0 = (int)(note_hz(93) / ((double)SR / N) + 0.5);
        double a5 = 0, a25 = 0, a60 = 0;
        for (int b = c0 - 60; b <= c0 + 60; b++) {
            if (abs(b - c0) <= 5) a5 += pw[b];
            if (abs(b - c0) <= 25) a25 += pw[b];
            a60 += pw[b];
        }
        inner[d] = a5 / a25;
        if (d) outer = a25 / a60;
        E->destroy(e);
    }
    snprintf(what, sizeof what, "unison detune: %.0f%% of the fundamental's power within 13 Hz of 1760 Hz at detune 0, "
             "%.0f%% at 100%%; %.1f%% of it within 66 cents", inner[0] * 100, inner[1] * 100, outer * 100);
    check(inner[0] > 0.97 && inner[1] < 0.7 && outer > 0.97, what);

    static const int keys3[] = {48, 52, 55};   /* 8 voices, count 4: two notes, a third steals a whole stack */
    double hz3[3] = {note_hz(48), note_hz(52), note_hz(55)}, db2[3], db3[3];
    e = patch(SAW, 0, 1, 100, 10);
    set(e, "voices", 4); set(e, "unison", 1); set(e, "uni_count", 2); set(e, "uni_detune", 0);
    set(e, "flt_cut", knob_for_hz(600)); set(e, "volume", 25);
    note(e, 1, keys3[0], 100); note(e, 1, keys3[1], 100);
    render_f(e, x, N + 8192);
    powers_db(x + 8192, hz3, 3, db2);
    note(e, 1, keys3[2], 100);
    render_f(e, x, N + 8192);
    powers_db(x + 8192, hz3, 3, db3);
    snprintf(what, sizeof what, "unison 4 of 8 voices: C3 + E3 held both sound (%.0f, %.0f dB); adding G3 steals C3's "
             "stack (C3 %.0f | E3 %.0f, G3 %.0f dB)", db2[0], db2[1], db3[0], db3[1], db3[2]);
    check(db2[0] > -6 && db2[1] > -6 && db3[0] < -25 && db3[1] > -6 && db3[2] > -6, what);
    note(e, 0, 52, 0); note(e, 0, 55, 0);
    check(silent_after(e, 1000), "releasing the keys lets go of every voice in their stacks");
    E->destroy(e);

    double mono_lvl[2];   /* mono + unison: one note stacked; a second key moves the whole stack */
    for (int u = 0; u < 2; u++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "voices", 0); set(e, "volume", 10); set(e, "unison", u); set(e, "uni_count", 2); set(e, "uni_detune", 0);
        set(e, "flt_cut", knob_for_hz(600));
        note(e, 1, 48, 100);
        mono_lvl[u] = rms_of(e, SR / 2);
        if (u) {
            note(e, 1, 52, 100);
            render_f(e, x, N + 8192);
            powers_db(x + 8192, hz3, 2, db2);
        }
        E->destroy(e);
    }
    snprintf(what, sizeof what, "mono + unison 4: %.2fx the mono level (want 2); C3 then E3 held plays E3 only (C3 %.0f dB)",
             mono_lvl[1] / mono_lvl[0], db2[0]);
    check(fabs(mono_lvl[1] / mono_lvl[0] - 2) < 0.1 && db2[0] < -40 && db2[1] > -1, what);

    double lo = 1e9, hi = -1e9;   /* pitch variance 50 cents: each new note lands within +-50 cents, and they differ */
    e = patch(SAW, 0, 1, 100, 10);
    set(e, "pitch_var", 50);
    for (int n = 0; n < 16; n++) {
        note(e, 1, 57, 127);
        render_f(e, x, N + 2048);
        double c = cents(peak_hz(x + 2048), 220);
        lo = fmin(lo, c); hi = fmax(hi, c);
        note(e, 0, 57, 0);
    }
    snprintf(what, sizeof what, "pitch variance 50 cents: 16 notes land between %+.1f and %+.1f cents", lo, hi);
    check(lo > -51 && hi < 51 && hi - lo > 30, what);
    E->destroy(e);

    e = E->create(NULL);
    char a[32], b[32], c[32];
    set(e, "glide_time", 0); E->get_param(e, "glide_time_display", a, sizeof a);
    set(e, "glide_time", 50); E->get_param(e, "glide_time_display", b, sizeof b);
    set(e, "glide_time", 100); E->get_param(e, "glide_time_display", c, sizeof c);
    snprintf(what, sizeof what, "glide time knob reads \"%s\", \"%s\", \"%s\" at 0, 50, 100", a, b, c);
    check(!strcmp(a, "0 ms") && !strcmp(b, "100 ms") && !strcmp(c, "10.00 s"), what);
    E->destroy(e);
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
    static int16_t pcm[(12 * 8 * STEP + 13 * RUN + 7 * TAIL) * 256];
    int16_t *o = pcm;
    void *e = patch(SAW, 5, 400, 40, 150);
    for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
    set(e, "o2_level", 80); set(e, "o2_freq", 0.08);
    set(e, "o3_level", 60); set_shape(e, "o3", SQUARE); set(e, "o3_oct", 1);
    for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(SAW, 5, 600, 60, 200);
    set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "o2_sync", 1);
    for (int k = 0; k < 8; k++) {
        set(e, "o2_freq", k < 4 ? k + 3 : 7 - k * 0.5);
        o = play(e, o, riff[k] + 24, STEP);
    }
    o = rest(e, o, TAIL);
    E->destroy(e);

    /* the new oscillator: a triangle sweeping its wave angle, PWM, ring mod, then FM bells */
    e = patch((shape_t){"tri", WAVE_TRI, 50, 50, 0}, 3, 400, 70, 150);
    for (int k = 0; k < 2; k++) { set(e, "o1_angle", 50 + 45 * k); o = play(e, o, riff[k] + 12, STEP); }
    set_shape(e, "o1", SQUARE);
    for (int k = 2; k < 4; k++) { set(e, "o1_pw", 50 - 18 * (k - 1)); o = play(e, o, riff[k] + 12, STEP); }
    set_shape(e, "o1", SAW); set(e, "o1_level", 0); set(e, "ring_level", 100); set(e, "o2_freq", 6.5);
    for (int k = 4; k < 6; k++) o = play(e, o, riff[k] + 24, STEP);
    set(e, "ring_level", 0); set(e, "o2_level", 100); set(e, "o2_freq", 0); set(e, "fm_amt", 40);
    set_shape(e, "o1", (shape_t){"tri", WAVE_TRI, 50, 50, 0}); set_shape(e, "o2", (shape_t){"tri", WAVE_TRI, 50, 50, 0});
    set(e, "o1_oct", 3); set(e, "o1_freq", 2);
    for (int k = 6; k < 8; k++) o = play(e, o, riff[k] + 24, STEP);
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(SAW, 3, 300, 70, 120);   /* ladder: a resonant sweep up the riff twice, the second time driven */
    set(e, "o2_level", 80); set(e, "o2_freq", 0.07); set(e, "flt_res", 75);
    for (int k = 0; k < 16; k++) {
        if (k == 8) set(e, "flt_drive", 80);
        set(e, "flt_cut", 15 + 60 * (k % 8) / 7.0);
        o = play(e, o, riff[k % 8], STEP);
    }
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(SAW, 3, 300, 70, 120);   /* SVF: band-pass sweep in parallel, then SVF high-pass into the ladder */
    route(e, "o1", 1); set(e, "o2_level", 80); set(e, "o2_freq", 0.07); route(e, "o2", 1);
    set(e, "svf_mode", SVF_BP); set(e, "svf_res", 70); set(e, "flt_mix", 0);
    for (int k = 0; k < 16; k++) {
        if (k == 8) { set(e, "svf_mode", SVF_HP); set(e, "flt_route", 1); set(e, "flt_mix", 100);
                      set(e, "flt_cut", 70); set(e, "flt_res", 40); }
        set(e, "svf_cut", 25 + 50 * (k % 8) / 7.0);
        o = play(e, o, riff[k % 8], STEP);
    }
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(SAW, 1, 600, 0, 150);   /* modulation: an envelope pluck, a synced LFO wobble, a vibrato lead */
    set(e, "o2_level", 70); set(e, "o2_oct", 1); set(e, "flt_cut", 25); set(e, "flt_res", 55);
    set(e, "lad_env", 70); set(e, "fenv_d", knob_ms(250)); set(e, "fenv_s", 0);
    for (int k = 0; k < 8; k++) o = play(e, o, riff[k], STEP);
    set(e, "amp_s", 100); set(e, "lad_env", 0); set(e, "flt_cut", 35); set(e, "lad_lfo", 60);
    set(e, "lfo2_wave", LFO_SINE); set(e, "lfo2_sync", 1); set(e, "lfo2_rate", 60);   /* 1/8 at 120 BPM */
    for (int k = 0; k < 4; k++) o = play(e, o, riff[2 * k], 2 * STEP);
    set(e, "lad_lfo", 0); set(e, "flt_cut", 80); set(e, "o2_oct", 2); set(e, "o2_freq", 0.06);
    set(e, "pitch_lfo", 3); set(e, "lfo1_wave", LFO_SINE);
    for (int k = 0; k < 4; k++) o = play(e, o, riff[k] + 24, 2 * STEP);
    o = rest(e, o, TAIL);
    E->destroy(e);

    e = patch(SAW, 0, 120, 0, 120);
    set(e, "o1_level", 0); set(e, "noise_level", 100);
    for (int k = 0; k < 8; k++) o = play(e, o, 60, STEP);
    E->destroy(e);

    e = patch(SAW, 2, 200, 80, 60);
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

/* a busy patch (three oscillators, both filters in series, the matrix busy) held at 1, 4 and 8 voices */
static void bench(void) {
    enum { BLOCKS = 50000 };
    int16_t buf[256];
    static const int counts[] = {1, 4, 8}, opt[] = {0, 2, 4};
    for (int c = 0; c < 3; c++) {
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", opt[c]); set(e, "o2_level", 80); set(e, "o3_level", 60);
        route(e, "o1", 2); set(e, "flt_route", 1); set(e, "lad_env", 40); set(e, "flt_res", 50);
        set(e, "mod2_src", 6); set(e, "mod2_dst", 9); set(e, "mod2_amt", 30);
        for (int k = 0; k < counts[c]; k++) note(e, 1, 48 + 3 * k, 100);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        printf("bench: %d voice%s %6.2f us per 128-frame block on this PC (%.2f per voice; MPC budget 2902 us, shared;"
               " device numbers need tools/bench.sh)\n", counts[c], counts[c] > 1 ? "s" : " ", us, us / counts[c]);
        E->destroy(e);
    }
    void *u = patch(SAW, 5, 300, 70, 250);   /* the same busy patch as 2 unison stacks of 4, gliding (the first 10 s) */
    set(u, "voices", 4); set(u, "o2_level", 80); set(u, "o3_level", 60);
    route(u, "o1", 2); set(u, "flt_route", 1); set(u, "lad_env", 40); set(u, "flt_res", 50);
    set(u, "mod2_src", 6); set(u, "mod2_dst", 9); set(u, "mod2_amt", 30);
    set(u, "unison", 1); set(u, "uni_count", 2); set(u, "uni_detune", 40); set(u, "pitch_var", 10);
    set(u, "glide", 1); set(u, "glide_type", 1); set(u, "glide_time", 100);   /* 10 s */
    note(u, 1, 36, 100); note(u, 1, 40, 100); note(u, 0, 36, 0); note(u, 0, 40, 0);
    note(u, 1, 72, 100); note(u, 1, 76, 100);
    struct timespec u0, u1;
    clock_gettime(CLOCK_MONOTONIC, &u0);
    for (int b = 0; b < BLOCKS; b++) E->render(u, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &u1);
    double uus = ((u1.tv_sec - u0.tv_sec) * 1e9 + (u1.tv_nsec - u0.tv_nsec)) / 1e3 / BLOCKS;
    printf("bench: 8 voices as 2 unison stacks + glide %6.2f us per 128-frame block on this PC (%.2f per voice)\n",
           uus, uus / 8);
    E->destroy(u);

    void *e = patch(SAW, 5, 300, 70, 250);   /* the effects on top of 8 voices */
    set(e, "voices", 4); set(e, "modfx_type", 1); set(e, "modfx_mix", 50); set(e, "dly_mix", 30); set(e, "rev_mix", 30);
    for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
    printf("bench: 4 voices + chorus, delay, reverb %6.2f us per 128-frame block on this PC\n", us);
    E->destroy(e);
}

int main(int argc, char **argv) {
    E = mpc_engine();
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
    aliasing();
    sync_aliasing();
    shapes();
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
    voices();
    effects();
    envelope();
    keys();
    state();
    glide_unison();
    demo_wav(argc > 1 ? argv[1] : "demo.wav");
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
