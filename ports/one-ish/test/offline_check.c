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
 *   svf pair  series makes 24 dB slopes and keeps one element's peak; parallel at spacing 0 is the single filter
 *             exactly; parallel with spacing has two peaks, series notch two notches; stable at full resonance in
 *             every configuration and under filter FM; Link Cutoff keeps the offset; keyboard tracking at -100% and
 *             +200%; filter FM adds sidebands only when its amount is up
 *   routing   parallel/series, the filter mix and each source's SVF/ladder buttons send the sound where they say
 *   mod       envelope timing and knob text, filter envelope, keyboard tracking, LFO vibrato free and tempo-synced,
 *             pitch envelope, a soft note after a loud one starts at its own level
 *   matrix    amounts and via scaling add up; mod wheel vibrato (the default slot 1); velocity to cutoff; aftertouch
 *             to a level; pitch bend
 *   lfo/matrix  LFO shapes and variation, S&H vs smooth noise, polarity, start phase and note-on reset, delay, fade in,
 *             repeats, smoothing, ranges and synced divisions with dotted / triplet timing; LFO 3 to wave angle and
 *             pulse width; matrix amounts to 300%, controller amount and inversion, controller alone, transforms;
 *             slots 9-16, Constant and Exp 1 sources, LFO rate and envelope time / time scale destinations
 *   voices    chords; with 4 voices six held keys keep the newest four; a repeated key doesn't double up; the
 *             sustain pedal holds and lets go; mono plays only the newest key
 *   effects   master delay echoes on time (free and synced, left and right, ping-pong and its offset); sends at 0,
 *             master off and a synth effect off or at mix 0 are exactly dry; every reverb's tail grows with decay and
 *             holds when frozen; a bigger size rings longer; series feeds the echoes to the reverb; width 0 is mono
 *   tier3     every synth effect type stays bounded at full feedback/depth/resonance and changes the sound; delay
 *             types are on time; chorus spread; bit crusher levels and rate; resonator peaks; phaser notches (6 vs 12
 *             stages); CTRL 1/2 read and move their targets
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
 *   env extras  curves, delay, hold, loop, latch, sync, time scale, velocity amount, multi trig and reset
 *   keyboard  mono note priority low/high/last; each voice stealing mode with a full pool; HOLD (legato adds,
 *             non-legato replaces, with the pedal it adds, off lets go); sostenuto; pitch bend up/down ranges and
 *             per-oscillator offsets, the retired bend_range; velocity curves; transpose, octave shift, master and fine
 *             tune; the key-track centre moves tracking; pitch tracking 0/100/200%; note-on reset; hard sync sources
 *   noise/stereo/drift  red, white and purple noise slopes and levels, the colour mix, the noise EG (attack, sustain,
 *             release, sustain off as a decay, off by default); the pan law (equal power, centre exact, hard left
 *             silences the right), spread places voices apart (mono stays put, a unison stack fans out), the mod
 *             effect's mono input; drift 0 changes nothing, 100% stays within +-4 cents
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

static void route(void *e, const char *ch, int r);
static double get(void *e, const char *k);

/* gain of the SVF pair alone for a quiet sine, in dB */
static double svf2_gain_db(int mode, int pair, double space, double reso, double fc, double f) {
    svf2_t sv;
    memset(&sv, 0, sizeof sv);
    svf2_set(&sv, (float)fc, (float)space, (float)reso, mode, pair, SR);
    double in = 0, out = 0;
    for (int i = 0; i < SR; i++) {
        float x = 0.1f * sinf((float)(2 * M_PI * f * i / SR));
        float y = svf2_next(&sv, x);
        if (i >= SR / 2) { in += x * x; out += y * y; }
    }
    return 10 * log10(out / in + 1e-30);
}

/* the strongest 1/6-octave band of a sound (power summed over 6 FFT frames), on a 1/24-octave grid from 50 Hz up:
 * where a resonant bandpass sits in filtered noise, which a single FFT bin finds only roughly */
static double band_peak_hz(const float *x) {
    static double sum[N / 2];
    memset(sum, 0, sizeof sum);
    for (int fr = 0; fr < 6; fr++) {
        const double *pw = spectrum(x + fr * N);
        for (int k = 0; k < N / 2; k++) sum[k] += pw[k];
    }
    double best = 0, best_hz = 0, bin = (double)SR / N;
    for (double c = 50; c < 16000; c *= pow(2, 1 / 24.0)) {
        double p = 0;
        for (int k = (int)(c * pow(2, -1 / 12.0) / bin); k <= (int)(c * pow(2, 1 / 12.0) / bin) && k < N / 2; k++) p += sum[k];
        if (p > best) { best = p; best_hz = c; }
    }
    return best_hz;
}

/* the SVF as a pair of elements (spacing, series/parallel), Link Cutoff, keyboard tracking to +-200%, filter FM */
static void svf_pair(void) {
    char what[220];
    /* series: two 12 dB elements make 24 dB; at no resonance the cutoff still sits at -6 dB, as one element */
    double pass = svf2_gain_db(SVF_LP, SVF_SERIES, 0, 0, 1000, 250), stop = svf2_gain_db(SVF_LP, SVF_SERIES, 0, 0, 1000, 4000);
    snprintf(what, sizeof what, "SVF pair series LP: %.1f dB at a quarter of the cutoff, %.1f dB two octaves above "
             "(want about -48: 24 dB/oct)", pass, stop);
    check(pass > -1 && fabs(stop - pass + 48.2) < 3, what);
    double hp_pass = svf2_gain_db(SVF_HP, SVF_SERIES, 0, 0, 1000, 4000), hp_stop = svf2_gain_db(SVF_HP, SVF_SERIES, 0, 0, 1000, 250);
    snprintf(what, sizeof what, "SVF pair series HP: %.1f dB two octaves above the cutoff, %.1f dB two below", hp_pass, hp_stop);
    check(hp_pass > -1 && fabs(hp_stop - hp_pass + 48.2) < 3, what);
    double s_fc = svf2_gain_db(SVF_LP, SVF_SERIES, 0, 0, 1000, 1000), o_fc = svf_gain_db(SVF_LP, 0, 1000, 1000);
    double s_pk = svf2_gain_db(SVF_LP, SVF_SERIES, 0, 1, 1000, 1000), o_pk = svf_gain_db(SVF_LP, 1, 1000, 1000);
    snprintf(what, sizeof what, "SVF pair series vs one element at the cutoff: %.1f / %.1f dB at no resonance, "
             "%+.1f / %+.1f dB at full", s_fc, o_fc, s_pk, o_pk);
    check(fabs(s_fc - o_fc) < 1 && fabs(s_pk - o_pk) < 2, what);

    /* parallel at spacing 0 is the old single element, sample for sample, while the cutoff sweeps */
    {
        svf_t one;
        svf2_t two;
        memset(&one, 0, sizeof one);
        memset(&two, 0, sizeof two);
        double t = 0, diff = 0;
        for (int b = 0; b < 400; b++) {
            float hz = (float)(20 * pow(1000, b / 400.0));
            svf_set(&one, hz, 0.7f, b / 100 % 4, SR);
            svf2_set(&two, hz, 0, 0.7f, b / 100 % 4, SVF_PARALLEL, SR);
            for (int i = 0; i < 128; i++) {
                float x = (float)(2 * t - 1);
                diff = fmax(diff, fabs(svf_next(&one, x) - svf2_next(&two, x)));
                t += 110.0 / SR;
                if (t >= 1) t -= 1;
            }
        }
        snprintf(what, sizeof what, "SVF pair parallel at spacing 0 is the single 12 dB filter of before (largest difference %g)", diff);
        check(diff == 0, what);
    }

    /* parallel with spacing +50% (2 octaves): resonant BP peaks at the cutoff and two octaves above it */
    double peaks[4];
    int np = 0;
    {
        double g[200], f[200];
        int n = 0;
        for (double hz = 125; hz < 8000 && n < 200; hz *= pow(2, 1 / 24.0), n++) {
            f[n] = hz;
            g[n] = svf2_gain_db(SVF_BP, SVF_PARALLEL, 0.5, 0.9, 500, hz);
        }
        for (int i = 1; i < n - 1; i++)
            if (g[i] > g[i - 1] && g[i] >= g[i + 1] && g[i] > -6 && np < 4) peaks[np++] = f[i];
    }
    snprintf(what, sizeof what, "SVF pair parallel BP, spacing +50%%, cutoff 500 Hz: %d peaks, at %.0f and %.0f Hz (want 500, 2000)",
             np, np > 0 ? peaks[0] : 0, np > 1 ? peaks[1] : 0);
    check(np == 2 && fabs(log2(peaks[0] / 500)) < 1 / 12.0 && fabs(log2(peaks[1] / 2000)) < 1 / 12.0, what);
    double n1 = svf2_gain_db(SVF_NOTCH, SVF_SERIES, -0.25, 0.5, 2000, 2000), n2 = svf2_gain_db(SVF_NOTCH, SVF_SERIES, -0.25, 0.5, 2000, 1000),
           mid = svf2_gain_db(SVF_NOTCH, SVF_SERIES, -0.25, 0.5, 2000, 1414), out = svf2_gain_db(SVF_NOTCH, SVF_SERIES, -0.25, 0.5, 2000, 100);
    snprintf(what, sizeof what, "SVF pair series notch, spacing -25%% (1 octave down): %.1f dB at 2 kHz, %.1f at 1 kHz, "
             "%.1f between, %.1f far below", n1, n2, mid, out);
    check(n1 < -30 && n2 < -30 && mid > -15 && out > -1, what);

    /* full resonance in every mode, pair and spacing, the cutoff sweeping and filter FM swinging it +-4 octaves */
    float worst = 0;
    int bad = 0;
    for (int mode = 0; mode < 4; mode++)
        for (int pair = 0; pair < 2; pair++)
            for (int sp = -1; sp <= 1; sp++)
                for (int fm = 0; fm < 2; fm++) {
                    svf2_t sv;
                    memset(&sv, 0, sizeof sv);
                    double t = 0;
                    for (int b = 0; b < 700; b++) {
                        double pos = b < 350 ? b / 350.0 : (700 - b) / 350.0;
                        svf2_set(&sv, (float)(20 * pow(1000, pos)), (float)sp, 1.0f, mode, pair, SR);
                        for (int i = 0; i < 128; i++) {
                            float x = (float)(2 * t - 1);
                            float y = fm ? svf2_next_fm(&sv, x, exp2f(4 * sinf((float)(2 * M_PI * 333 * (b * 128 + i) / SR))))
                                         : svf2_next(&sv, x);
                            t += 55.0 / SR;
                            if (t >= 1) t -= 1;
                            if (!isfinite(y)) bad++;
                            else if (fabsf(y) > worst) worst = fabsf(y);
                        }
                    }
                }
    snprintf(what, sizeof what, "SVF pair stable at full resonance in every mode, pair and spacing, with and without "
             "filter FM (largest output %.1f)", worst);
    check(!bad && worst < 40, what);
    worst = 0;
    for (int slope = 0; slope < 4; slope++) {
        ladder_t l;
        memset(&l, 0, sizeof l);
        double t = 0;
        for (int b = 0; b < 700; b++) {
            double pos = b < 350 ? b / 350.0 : (700 - b) / 350.0;
            ladder_set(&l, (float)(20 * pow(1000, pos)), 1.0f, 1.0f, LADDER_LP, slope, SR);
            for (int i = 0; i < 128; i++) {
                float y = ladder_next_fm(&l, (float)(2 * t - 1) * 3.0f, exp2f(4 * sinf((float)(2 * M_PI * 333 * (b * 128 + i) / SR))));
                t += 55.0 / SR;
                if (t >= 1) t -= 1;
                if (!isfinite(y)) bad++;
                else if (fabsf(y) > worst) worst = fabsf(y);
            }
        }
    }
    snprintf(what, sizeof what, "ladder stable with filter FM at full resonance and drive (largest output %.2f)", worst);
    check(!bad && worst < 8, what);

    /* Link Cutoff: either knob moves both, keeping the offset; an end holds and the offset comes back */
    void *e = E->create(NULL);
    set(e, "flt_cut", 40); set(e, "svf_cut", 60); set(e, "flt_link", 1);
    set(e, "flt_cut", 50);
    double a1 = get(e, "svf_cut");
    set(e, "svf_cut", 80);
    double a2 = get(e, "flt_cut");
    set(e, "flt_cut", 95);
    double a3 = get(e, "svf_cut");
    set(e, "flt_cut", 70);
    double a4 = get(e, "svf_cut");
    set(e, "flt_link", 0); set(e, "flt_cut", 10);
    double a5 = get(e, "svf_cut");
    snprintf(what, sizeof what, "Link Cutoff (offset +20): ladder 50 -> SVF %.0f, SVF 80 -> ladder %.0f, ladder 95 -> SVF %.0f "
             "(held at the end), ladder 70 -> SVF %.0f (offset back); unlinked, SVF stays %.0f", a1, a2, a3, a4, a5);
    check(a1 == 70 && a2 == 60 && a3 == 100 && a4 == 90 && a5 == 90, what);
    E->destroy(e);

    /* keyboard tracking beyond 100%: a self-oscillating ladder an octave up at -100% and +200% */
    static float x[SR * 3];
    static const double track[] = {-100, 200};
    for (int t = 0; t < 2; t++) {
        double f[2];
        for (int k = 0; k < 2; k++) {
            e = patch(SAW, 0, 1, 100, 10);
            set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(440)); set(e, "lad_kbd", track[t]);
            note(e, 1, 60 + 12 * k, 127);
            render_f(e, x, SR * 2);
            f[k] = peak_hz(x + SR * 2 - N);
            E->destroy(e);
        }
        double want = pow(2, track[t] / 100);
        snprintf(what, sizeof what, "ladder keyboard tracking %+.0f%%: C4 rings at %.1f Hz, C5 at %.1f Hz (ratio %.3f, want %.3f)",
                 track[t], f[0], f[1], f[1] / f[0], want);
        check(fabs(f[1] / f[0] / want - 1) < 0.02, what);
    }
    for (int t = 0; t < 2; t++) {   /* the SVF can't ring by itself: find its resonant band in noise */
        double f[2];
        for (int k = 0; k < 2; k++) {
            e = patch(SAW, 0, 1, 100, 10);
            set(e, "o1_level", 0); set(e, "noise_level", 100); route(e, "noise", 1); set(e, "flt_mix", 0);
            set(e, "svf_mode", SVF_BP); set(e, "svf_pair", SVF_SERIES); set(e, "svf_res", 100);
            set(e, "svf_cut", knob_for_hz(500)); set(e, "svf_kbd", track[t]);
            note(e, 1, 60 + 12 * k, 127);
            render_f(e, x, SR / 4 + 6 * N);
            f[k] = band_peak_hz(x + SR / 4);
            E->destroy(e);
        }
        double want = pow(2, track[t] / 100);
        snprintf(what, sizeof what, "SVF keyboard tracking %+.0f%%: band at %.0f Hz on C4, %.0f Hz on C5 (ratio %.2f, want %.2f)",
                 track[t], f[0], f[1], f[1] / f[0], want);
        check(fabs(log2(f[1] / f[0] / want)) < 1 / 12.0, what);
    }

    /* filter FM: osc 2 (not heard, 5 semitones up) swings the cutoff at audio rate, adding sidebands that are not
     * harmonics of the note; at amount 0 there are none */
    static const char *const fm_key[] = {"lad_fm", "svf_fm"};
    for (int flt = 0; flt < 2; flt++) {
        double inh[2];
        for (int on = 0; on < 2; on++) {
            e = patch(SAW, 0, 1, 100, 10);
            set(e, "o2_freq", 5);
            if (flt) { route(e, "o1", 1); set(e, "flt_mix", 0); set(e, "svf_cut", knob_for_hz(800)); set(e, "svf_res", 50); set(e, "svf_fm_src", 1); }
            else { set(e, "flt_cut", knob_for_hz(800)); set(e, "flt_res", 50); set(e, "lad_fm_src", 1); }
            set(e, fm_key[flt], on ? 50 : 0);
            note(e, 1, 45, 127);
            render_f(e, x, SR / 4 + N);
            inh[on] = alias_db(x + SR / 4, 110);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "%s filter FM from osc 2: inharmonic power %.1f dB at amount 0, %.1f dB at 50%%",
                 flt ? "SVF" : "ladder", inh[0], inh[1]);
        check(inh[0] < -60 && inh[1] > -30, what);
    }
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
        if (synced) { E->set_param(e, "lfo_bpm", "120"); set(e, "lfo1_sync", 1); set(e, "lfo1_rate", 500 / 12.0); }
        else set(e, "lfo1_rate", 100 * log10(1 / 0.01) / 3);   /* the Low range: 0.01 .. 10 Hz */
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
    float slots[MOD_SLOTS * MOD_SLOT_PARAMS] = {0}, extra[MOD_SLOTS * MOD_SLOT_EXTRA] = {0}, src[MS_COUNT] = {0}, dst[MD_COUNT];
    float a[] = {MS_VEL, MD_PITCH, 50, MS_OFF}, b[] = {MS_VEL, MD_PITCH, 50, MS_WHEEL}, c[] = {MS_LFO1, MD_AMP, -100, MS_OFF};
    memcpy(slots, a, sizeof a); memcpy(slots + 4, b, sizeof b); memcpy(slots + 8, c, sizeof c);
    for (int i = 0; i < MOD_SLOTS; i++) extra[i * MOD_SLOT_EXTRA] = 100;   /* via amount at its default, +100% */
    src[MS_VEL] = 1; src[MS_WHEEL] = 0.5f; src[MS_LFO1] = 0.25f;
    mod_eval(slots, extra, src, 0, dst);
    snprintf(what, sizeof what, "matrix: two velocity slots, one via the half-up wheel, sum to %.3f (want 0.750); LFO to amp %.3f",
             dst[MD_PITCH], dst[MD_AMP]);
    check(fabsf(dst[MD_PITCH] - 0.75f) < 1e-6f && fabsf(dst[MD_AMP] + 0.25f) < 1e-6f, what);

    for (int wheel = 0; wheel < 2; wheel++) {   /* the default slot 1 (LFO 1 -> pitch via mod wheel), at full amount */
        void *e = patch(SAW, 0, 1, 100, 10);
        set(e, "mod1_amt", 100); set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(1 / 0.01) / 3);
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
    env_cfg_t cfg;
    memset(&env, 0, sizeof env);
    memset(&cfg, 0, sizeof cfg);
    env_config(&cfg, (const float[5]){0, 1, 0, 50, 50}, 0, 0, 0.5f, 0.5f, SR);
    env.cfg = &cfg;
    env_gate_on(&env);
    for (int i = 0; i < SR / 2; i++) env_next(&env);
    check(env.stage == ENV_IDLE, "a pluck envelope (sustain 0) is idle once its decay ends, with the key still down");
}

static fx_t fxs;   /* about 3.1 MB: static, not on the stack */

/* every effect off or neutral: the master effects on with both sends at 0, neutral settings for the rest */
static fx_params_t fx_none(void) {
    fx_params_t p;
    memset(&p, 0, sizeof p);
    p.gain = 1; p.tone = 1; p.time_ms = 7; p.time_r_ms = 7; p.dtime_ms = 100; p.rate_hz = 0.5f; p.rate_r_hz = 0.5f;
    p.vib_hz = 5; p.bits = 8; p.srate_hz = FX_SR; p.reso = 0.5f; p.ftype = RES_BP;
    p.cut_hz[0] = 500; p.cut_hz[1] = 1000; p.cut_hz[2] = 5000; p.lvl[0] = p.lvl[1] = p.lvl[2] = 1;
    p.mfx_on = 1; p.width = 1;
    p.dly_l_ms = p.dly_r_ms = 250; p.dly_tone = 1;
    p.rev_rt60 = 2; p.rev_size = 1; p.rev_diff = 1; p.rev_tone = 1; p.rev_mod = 1; p.rev_early = 0.5f; p.rev_damp = 0.3f;
    return p;
}

/* run the effects on `n` samples of `in` (NULL: an impulse at IMP, after the first block: a send fades in over the
 * block it starts in) on both inputs, into l, r; from a fresh state */
#define IMP 128
static void fx_run(const fx_params_t *p, const float *in, float *l, float *r, int n) {
    static float x[SR * 6];
    memset(&fxs, 0, sizeof fxs);
    for (int i = 0; i < n; i++) x[i] = in ? in[i] : (i == IMP);
    for (int at = 0; at < n; at += 128) {
        int m = n - at < 128 ? n - at : 128;
        fx_process(&fxs, p, NULL, x + at, x + at, l + at, r + at, m);
    }
}

static int loudest(const float *x, int from, int to) {
    int best = from;
    for (int i = from; i < to; i++)
        if (fabsf(x[i]) > fabsf(x[best])) best = i;
    return best;
}

static double energy_db(const float *l, const float *r, int from, int to) {
    double e = 0;
    for (int i = from; i < to; i++) e += l[i] * l[i] + r[i] * r[i];
    return 10 * log10(e + 1e-30);
}

static const char *const SFX_NAMES[SFX_NTYPES] = {"delay", "bbd", "tape", "echo", "flanger", "stereo_flanger", "chorus",
    "dual_chorus", "ensemble", "stereo_chorus", "phaser6", "phaser12", "time_phaser", "bitcrusher", "resonator"};
static const char *const REV_NAMES[3] = {"plate", "room", "hall"};

/* the master delay and reverb, and the routing around them */
static void effects(void) {
    static float l[SR * 6], r[SR * 6], saw[SR * 6];
    char what[240];
    fx_params_t p = fx_none();
    p.send1 = 1;   /* one echo, 250 ms after an impulse */
    fx_run(&p, NULL, l, r, SR);
    int at = loudest(l, IMP + 100, SR) - IMP;
    snprintf(what, sizeof what, "delay 250 ms: echo at %.2f ms (level %.2f)", at / 44.1, l[at + IMP]);
    check(at == 11025 && l[at + IMP] > 0.99f, what);

    p.dly_r_ms = 400;   /* left and right times of their own */
    fx_run(&p, NULL, l, r, SR);
    int al = loudest(l, IMP + 100, SR) - IMP, ar = loudest(r, IMP + 100, SR) - IMP;
    snprintf(what, sizeof what, "stereo delay 250/400 ms: left echo at %.1f ms, right at %.1f ms", al / 44.1, ar / 44.1);
    check(al == 11025 && ar == 17640 && l[al + IMP] > 0.99f && r[ar + IMP] > 0.99f, what);

    p.dly_r_ms = 250; p.dly_fb = 0.5f; p.dly_pp = 1;   /* ping-pong: first repeat left, second right */
    fx_run(&p, NULL, l, r, SR);
    int l1 = loudest(l, IMP + 100, 16000) - IMP, r2 = loudest(r, 16000, 27000) - IMP;
    snprintf(what, sizeof what, "ping-pong: first echo left at %.0f ms (%.2f, right %.2f), second right at %.0f ms (%.2f)",
             l1 / 44.1, l[l1 + IMP], r[l1 + IMP], r2 / 44.1, r[r2 + IMP]);
    check(l1 == 11025 && fabsf(r[l1 + IMP]) < 0.01f && abs(r2 - 22050) <= 1 && r[r2 + IMP] > 0.2f, what);
    p.dly_offset = 1;   /* offset 100%: the right repeat half the left time later */
    fx_run(&p, NULL, l, r, SR);
    r2 = loudest(r, 16000, 33000) - IMP;
    snprintf(what, sizeof what, "ping-pong offset 100%%: second echo right at %.0f ms (want 625)", r2 / 44.1);
    check(abs(r2 - 27563) <= 1 && r[r2 + IMP] > 0.2f, what);

    /* synced times: the division, at the host tempo */
    void *e = patch(SAW, 0, 1, 100, 10);
    char txt[48], txt2[48];
    E->set_param(e, "lfo_bpm", "120"); set(e, "dly_sync", 1); set(e, "dly_time", 50); set(e, "dly_time_r", 55);
    E->get_param(e, "dly_time_display", txt, sizeof txt);
    E->get_param(e, "dly_time_r_display", txt2, sizeof txt2);
    snprintf(what, sizeof what, "delay synced reads \"%s\" / \"%s\" (want 1/8, 1/8.); 1/8 at 120 BPM is %.1f ms, 1/4T %.1f ms",
             txt, txt2, fx_div_ms(50, 120), fx_div_ms(60, 120));
    check(!strcmp(txt, "1/8") && !strcmp(txt2, "1/8.") && fabsf(fx_div_ms(50, 120) - 250) < 0.01f
          && fabsf(fx_div_ms(60, 120) - 1000.0f / 3) < 0.01f, what);
    set(e, "dly_sync", 0); set(e, "dly_time", 100);
    E->get_param(e, "dly_time_display", txt, sizeof txt);
    set(e, "modfx_sync", 1); set(e, "modfx_rate", 0);
    E->get_param(e, "modfx_rate_display", txt2, sizeof txt2);
    snprintf(what, sizeof what, "delay time at full reads \"%s\" (want 2.00 s); synth FX rate synced at 0 reads \"%s\" (want 4)",
             txt, txt2);
    check(!strcmp(txt, "2.00 s") && !strcmp(txt2, "4"), what);
    E->destroy(e);

    /* sends at 0, master off, synth effect off or at mix 0: exactly dry */
    for (int i = 0; i < SR * 6; i++) saw[i] = 0.8f * (2.0f * (float)fmod(i * 110.0 / SR, 1.0) - 1.0f);
    int exact = 1;
    for (int k = 0; k < 4; k++) {
        p = fx_none();
        if (k == 1) { p.mfx_on = 0; p.send1 = p.send2 = 1; }
        if (k == 2) { p.sfx_on = 1; p.sfx_type = SFX_CHORUS; p.mix = 0; }
        if (k == 3) { p.sfx_on = 0; p.sfx_type = SFX_ECHO; p.mix = 1; p.width = 2; p.series = 1; }
        fx_run(&p, saw, l, r, SR);
        for (int i = 0; i < SR; i++) exact &= l[i] == saw[i] && r[i] == saw[i];
    }
    check(exact, "sends at 0, master effects off, synth effect at mix 0 or off: the output is the input exactly");

    /* the reverb tails grow with decay, every type; freeze holds the tail */
    for (int t = 0; t < 3; t++) {
        double tail[2];
        for (int k = 0; k < 2; k++) {
            p = fx_none(); p.send2 = 1; p.rev_type = t; p.rev_rt60 = k ? 5.0f : 0.5f;
            fx_run(&p, NULL, l, r, SR * 2);
            tail[k] = energy_db(l, r, SR, SR + SR / 2);
        }
        snprintf(what, sizeof what, "%s reverb: tail 1-1.5 s after an impulse %.0f dB at 0.5 s decay, %.0f dB at 5 s",
                 REV_NAMES[t], tail[0], tail[1]);
        check(tail[1] > tail[0] + 30 && tail[1] > -60, what);

        p = fx_none(); p.send2 = 1; p.rev_type = t; p.rev_rt60 = 3;   /* 1 s of saw, then freeze */
        memset(&fxs, 0, sizeof fxs);
        for (int at2 = 0; at2 < SR * 5; at2 += 128) {
            p.rev_freeze = at2 >= SR;
            fx_process(&fxs, &p, NULL, saw + at2, saw + at2, l + at2, r + at2, 128);
        }
        for (int i = 0; i < SR * 5; i++) { l[i] -= saw[i]; r[i] -= saw[i]; }   /* the reverb alone */
        double e2 = energy_db(l, r, SR * 2, SR * 2 + SR / 2), e4 = energy_db(l, r, SR * 4, SR * 4 + SR / 2);
        snprintf(what, sizeof what, "%s reverb freeze: %.1f dB at 2 s, %.1f dB at 4 s (holds within 3 dB)", REV_NAMES[t], e2, e4);
        check(fabs(e2 - e4) < 3 && e2 > 0, what);
    }

    /* size: through the engine, a bigger space at the same decay knob rings longer */
    double sz[2];
    for (int k = 0; k < 2; k++) {
        void *v = patch(SAW, 0, 30, 0, 10);
        static float buf[SR * 3];
        set(v, "mfx_send2", 100); set(v, "rev_type", REV_HALL); set(v, "rev_decay", 40); set(v, "rev_size", k ? 100 : 0);
        note(v, 1, 60, 100);
        render_f(v, buf, SR / 10);
        note(v, 0, 60, 0);
        render_f(v, buf, SR * 2);
        sz[k] = energy_db(buf + SR, buf + SR, 0, SR / 2);
        E->destroy(v);
    }
    snprintf(what, sizeof what, "hall size 50%% vs 200%%: tail 1.1-1.6 s after a note %.0f dB vs %.0f dB", sz[0], sz[1]);
    check(sz[1] > sz[0] + 20, what);

    /* series: the delay's echoes go through the reverb only; width 0: the returns in mono */
    p = fx_none(); p.send1 = 1; p.series = 1;
    fx_run(&p, NULL, l, r, SR);
    float direct = fabsf(l[IMP + 11025]);
    double rv = energy_db(l, r, 11025, SR);
    p.series = 0;
    fx_run(&p, NULL, l, r, SR);
    snprintf(what, sizeof what, "series 100%%: the 250 ms echo comes back through the reverb (%.2f at the echo, was %.2f; "
             "%.0f dB after it)", direct, l[IMP + 11025], rv);
    check(direct < 0.3f && l[IMP + 11025] > 0.99f && rv > -20, what);
    p = fx_none(); p.send1 = 0.5f; p.send2 = 1; p.dly_r_ms = 330; p.width = 0;
    fx_run(&p, saw, l, r, SR);
    float side = 0;
    for (int i = 0; i < SR; i++) side = fmaxf(side, fabsf(l[i] - r[i]));
    snprintf(what, sizeof what, "width 0: left and right returns equal (largest difference %.2g)", side);
    check(side < 1e-5f, what);
}

/* the synth effect types and the parameters the manual gives them */
static void effects_tier3(void) {
    static float l[SR * 4], r[SR * 4], saw[SR * 4];
    char what[240];
    for (int i = 0; i < SR * 4; i++) saw[i] = 2.0f * (float)fmod(i * 110.0 / SR, 1.0) - 1.0f;

    /* every type stays bounded at its extremes: full feedback, depth, resonance, noise and gain */
    float worst = 0;
    int bad = 0, same = 0;
    for (int t = 0; t < SFX_NTYPES; t++) {
        fx_params_t p = fx_none();
        p.sfx_on = 1; p.sfx_type = t; p.mix = 1; p.fb = p.fb_r = p.dfb = 0.95f; p.depth = 1; p.reso = 1;
        p.noise = 1; p.decay = 1; p.room = 1; p.wow = 1; p.flutter = 1; p.bias = 1; p.vib_depth = 1; p.rate_hz = 3;
        p.time_ms = fx_mod_time_ms(t, 100); p.time_r_ms = fx_mod_time_ms(t, 60); p.dtime_ms = 80; p.bits = 2;
        p.srate_hz = 3000;
        fx_run(&p, saw, l, r, SR * 4);
        float w = 0, d = 0;
        for (int i = 0; i < SR * 4; i++) {
            if (!isfinite(l[i]) || !isfinite(r[i])) bad++;
            w = fmaxf(w, fmaxf(fabsf(l[i]), fabsf(r[i])));
            d = fmaxf(d, fabsf(l[i] - saw[i]));
        }
        if (d < 0.01f) same++;
        if (w > worst) worst = w;
    }
    snprintf(what, sizeof what, "all %d synth effect types at full feedback, depth and resonance stay bounded "
             "(largest %.1f) and change the sound (%d unchanged)", SFX_NTYPES, worst, same);
    check(!bad && worst < 50 && !same, what);
    fx_params_t hot[4] = {fx_none(), fx_none(), fx_none(), fx_none()};
    hot[0].send1 = 1; hot[0].dly_fb = 0.95f; hot[0].dly_pp = 1; hot[0].dly_l_ms = 80;
    for (int k = 1; k < 4; k++) { hot[k].send2 = 1; hot[k].rev_type = k - 1; hot[k].rev_rt60 = 20; hot[k].rev_size = 2;
                                  hot[k].rev_damp = 0; hot[k].rev_mod = 2; hot[k].rev_freeze = k == 2; }
    worst = 0;
    for (int k = 0; k < 4; k++) {
        fx_run(&hot[k], saw, l, r, SR * 4);
        for (int i = 0; i < SR * 4; i++) {
            if (!isfinite(l[i]) || !isfinite(r[i])) bad++;
            worst = fmaxf(worst, fmaxf(fabsf(l[i]), fabsf(r[i])));
        }
    }
    snprintf(what, sizeof what, "master delay at 95%% feedback, reverbs at 20 s decay, size 200%% and freeze stay bounded "
             "(largest %.1f)", worst);
    check(!bad && worst < 50, what);

    /* delay-type times are exact */
    for (int t = SFX_DELAY; t <= SFX_ECHO; t++) {
        fx_params_t p = fx_none();
        p.sfx_on = 1; p.sfx_type = t; p.mix = 1; p.dtime_ms = 100; p.depth = 0; p.wow = 0; p.flutter = 0; p.bias = 0;
        fx_run(&p, NULL, l, r, SR / 2);
        int at = IMP + 10;
        while (at < SR / 2 - 1 && fabsf(l[at]) < 0.1f) at++;
        at -= IMP;
        snprintf(what, sizeof what, "%s: the echo of an impulse arrives at %.2f ms (want 100)", SFX_NAMES[t], at / 44.1);
        check(t == SFX_DELAY ? at == 4410 && l[at + IMP] > 0.99f : abs(at - 4410) <= 6, what);
    }

    /* chorus spreads to stereo; spread 0 keeps it centred */
    for (int k = 0; k < 2; k++) {
        fx_params_t p = fx_none();
        p.sfx_on = 1; p.sfx_type = SFX_CHORUS; p.rate_hz = 0.8f; p.depth = 0.7f; p.mix = 0.5f; p.spread = k ? 0.5f : 0;
        fx_run(&p, saw, l, r, SR);
        double diff = 0, sum = 0;
        for (int i = SR / 2; i < SR; i++) { diff += (l[i] - r[i]) * (l[i] - r[i]); sum += l[i] * l[i]; }
        snprintf(what, sizeof what, "chorus spread %d%%: left-right difference %.0f dB below the left", k ? 50 : 0,
                 10 * log10(sum / (diff + 1e-30)));
        check(k ? diff > sum * 0.01 : diff < sum * 1e-9, what);
    }

    /* bit crusher: 4 bits leave at most 17 levels; 1 kHz holds each value for about 44 samples */
    fx_params_t p = fx_none();
    p.sfx_on = 1; p.sfx_type = SFX_CRUSH; p.mix = 1; p.bits = 4; p.srate_hz = FX_SR;
    fx_run(&p, saw, l, r, SR);
    int levels = 0;
    static float seen[64];
    for (int i = 0; i < SR && levels < 64; i++) {
        int found = 0;
        for (int j = 0; j < levels; j++) found |= seen[j] == l[i];
        if (!found) seen[levels++] = l[i];
    }
    p.bits = 24; p.srate_hz = 1000;
    fx_run(&p, saw, l, r, SR);
    int changes = 0;
    for (int i = 1; i < SR; i++) changes += l[i] != l[i - 1];
    snprintf(what, sizeof what, "bit crusher: 4 bits give %d levels (want <= 17); 1 kHz changes %d times a second (want ~1000)",
             levels, changes);
    check(levels <= 17 && levels > 4 && changes >= 990 && changes <= 1001, what);

    /* resonator: a band-pass bank rings at its three cutoffs */
    p = fx_none(); p.sfx_on = 1; p.sfx_type = SFX_RESON; p.mix = 1; p.reso = 0.95f; p.depth = 0;
    p.cut_hz[0] = 300; p.cut_hz[1] = 1200; p.cut_hz[2] = 4000;
    static float noise[SR];
    uint32_t seed = 1;
    for (int i = 0; i < SR; i++) noise[i] = fx_white(&seed) * 0.3f;
    fx_run(&p, noise, l, r, N);
    double pk[3], off = band_db(l, 700);
    for (int j = 0; j < 3; j++) pk[j] = band_db(l, p.cut_hz[j]);
    snprintf(what, sizeof what, "resonator on noise: %.0f, %.0f, %.0f dB at 300 Hz, 1.2 kHz, 4 kHz; %.0f dB at 700 Hz between",
             pk[0], pk[1], pk[2], off);
    check(pk[0] > off + 10 && pk[1] > off + 10 && pk[2] > off + 10, what);

    /* phasers, all stages at 1 kHz (depth 0), mix 50%: 6 stages notch 1 kHz out; 12 stages pass it and notch where each
     * stage turns 75 degrees instead */
    double lv[2][2];
    for (int k = 0; k < 2; k++)
        for (int q = 0; q < 2; q++) {
            double t0 = tan(M_PI * 1000.0 / SR), hz = q ? SR / M_PI * atan(t0 * tan(37.5 * M_PI / 180)) : 1000.0;
            static float sine[SR];
            for (int i = 0; i < SR; i++) sine[i] = (float)(0.5 * sin(2 * M_PI * hz * i / SR));
            p = fx_none(); p.sfx_on = 1; p.sfx_type = k ? SFX_PHASER12 : SFX_PHASER6; p.mix = 0.5f; p.depth = 0;
            p.sweep = (float)(log(25.0) / log(100.0));
            fx_run(&p, sine, l, r, SR);
            double e = 0, ein = 0;
            for (int i = SR / 2; i < SR; i++) { e += l[i] * l[i]; ein += sine[i] * sine[i]; }
            lv[k][q] = 10 * log10(e / ein + 1e-30);
        }
    snprintf(what, sizeof what, "phasers at 1 kHz: 6-stage %.0f dB (a notch), 12-stage %.1f dB (passes); 12-stage at %.0f Hz "
             "%.0f dB (a notch)", lv[0][0], lv[1][0], SR / M_PI * atan(tan(M_PI * 1000.0 / SR) * tan(37.5 * M_PI / 180)), lv[1][1]);
    check(lv[0][0] < -30 && lv[1][0] > -1 && lv[1][1] < -30, what);

    /* CTRL 1/2 read as their targets; they move them */
    void *e = patch(SAW, 0, 1, 100, 10);
    char a[48], b[48], c[48];
    set(e, "modfx_type", SFX_CHORUS); set(e, "modfx_ctrl1", 50); set(e, "modfx_ctrl2", -20);
    E->get_param(e, "modfx_ctrl1_display", a, sizeof a);
    E->get_param(e, "modfx_ctrl2_display", b, sizeof b);
    set(e, "modfx_c1_dst", FXD_MIX);
    E->get_param(e, "modfx_ctrl1_display", c, sizeof c);
    snprintf(what, sizeof what, "chorus CTRL 1 / 2 read \"%s\" / \"%s\"; targeted at Mix: \"%s\"", a, b, c);
    check(!strcmp(a, "Rate +50%") && !strcmp(b, "Depth -20%") && !strcmp(c, "Mix +50%"), what);
    /* CTRL 1 at -100% on Mix silences the wet signal: the output equals the dry one */
    static float o1[SR], o2[SR];
    set(e, "modfx_on", 1); set(e, "modfx_mix", 60); set(e, "modfx_ctrl1", -100);
    note(e, 1, 57, 100); render_f(e, o1, SR / 2); note(e, 0, 57, 0);
    E->destroy(e);
    e = patch(SAW, 0, 1, 100, 10);
    note(e, 1, 57, 100); render_f(e, o2, SR / 2); note(e, 0, 57, 0);
    E->destroy(e);
    int eq = 1;
    for (int i = 0; i < SR / 2; i++) eq &= o1[i] == o2[i];
    check(eq, "CTRL 1 at -100% on Mix 60%: the output is the dry sound exactly");
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

/* one envelope on its own (env.h): times in ms (delay, attack, hold, decay, release), sustain 0..1, curves -1..1 */
static void env_setup(env_t *e, env_cfg_t *c, float d, float a, float h, float dc, float s, float r, float acurve,
                      float dcurve, float rcurve) {
    memset(e, 0, sizeof *e);
    memset(c, 0, sizeof *c);
    env_config(c, (const float[5]){d, a, h, dc, r}, s, acurve, dcurve, rcurve, SR);
    c->multi = 1;
    e->cfg = c;
}

/* the envelope's level after `ms` more milliseconds */
static float env_after(env_t *e, double ms) {
    float l = e->level;
    for (int i = (int)(ms * SR / 1000 + 0.5); i > 0; i--) l = env_next(e);
    return l;
}

/* peak of |x| between two times in ms */
static double peak_in(const float *x, double from, double to) {
    double p = 0;
    for (int i = (int)(from * SR / 1000); i < (int)(to * SR / 1000); i++) p = fmax(p, fabs(x[i]));
    return p;
}

static void envelope_extras(void) {
    static float x[SR * 3];
    char what[240], txt[32], txt2[32];
    env_t e;
    env_cfg_t c;

    /* curves: the level halfway through a 100 ms attack, and a 100 ms decay to 0, for log, linear and exp; every
     * curve still takes the stage's whole time */
    static const float curves[3] = {-1, 0, 1};
    float mid_a[3], mid_d[3];
    int a_len[3];
    for (int k = 0; k < 3; k++) {
        env_setup(&e, &c, 0, 100, 0, 100, 0, 100, curves[k], curves[k], 0);
        env_gate_on(&e);
        mid_a[k] = env_after(&e, 50);
        int n = SR / 20;
        while (e.stage == ENV_ATTACK && n < SR) { env_next(&e); n++; }
        a_len[k] = n;
        env_after(&e, 50);
        mid_d[k] = e.level;
    }
    snprintf(what, sizeof what, "attack curves log/linear/exp: %.2f / %.2f / %.2f at mid-attack, each attack %d / %d / %d samples (want 4410)",
             mid_a[0], mid_a[1], mid_a[2], a_len[0], a_len[1], a_len[2]);
    check(mid_a[0] > 0.6f && fabsf(mid_a[1] - 0.5f) < 0.01f && mid_a[2] < 0.4f && abs(a_len[0] - 4410) <= 2 &&
          abs(a_len[1] - 4410) <= 2 && abs(a_len[2] - 4410) <= 2, what);
    snprintf(what, sizeof what, "decay curves log/linear/exp: %.2f / %.2f / %.2f at mid-decay (an exp decay falls fast, then slows)",
             mid_d[0], mid_d[1], mid_d[2]);
    check(mid_d[0] > 0.6f && fabsf(mid_d[1] - 0.5f) < 0.01f && mid_d[2] < 0.4f, what);

    /* the default decay curve (50%) is the old one-pole decay: 99% of the way in the decay time */
    env_setup(&e, &c, 0, 1, 0, 1000, 0, 100, 0, 0.5f, 0.5f);
    env_gate_on(&e);
    while (e.stage == ENV_ATTACK) env_next(&e);
    float q1 = env_after(&e, 250), q2 = env_after(&e, 250), q4 = env_after(&e, 490);
    snprintf(what, sizeof what, "default decay (curve 50%%): %.3f, %.3f, %.4f at 250, 500, 990 ms of 1 s (one-pole: %.3f, %.3f, %.4f)",
             q1, q2, q4, exp(-4.6 * 0.25), exp(-4.6 * 0.5), exp(-4.6 * 0.99));
    check(fabs(q1 - exp(-4.6 * 0.25)) < 0.012 && fabs(q2 - exp(-4.6 * 0.5)) < 0.012 && q4 < 0.012, what);

    /* hold keeps the peak; delay holds the start */
    env_setup(&e, &c, 30, 10, 100, 100, 0, 100, 0, 0.5f, 0.5f);
    env_gate_on(&e);
    float d1 = env_after(&e, 29), a1 = env_after(&e, 6), h1 = env_after(&e, 10), h2 = env_after(&e, 90), dc = env_after(&e, 10);
    snprintf(what, sizeof what, "delay 30 ms, attack 10, hold 100: %.2f at 29 ms, %.2f at 35, %.2f at 45, %.2f at 135, %.2f at 145",
             d1, a1, h1, h2, dc);
    check(d1 == 0 && a1 > 0.3f && a1 < 0.7f && h1 == 1 && h2 == 1 && dc < 0.9f, what);

    /* loop: delay 10, attack 10, hold 10, decay 20 to 50%, release 30: a 80 ms trapezoid while held; key up lets
     * it finish its release and stop; latched, it keeps cycling */
    static float lv[SR];
    env_setup(&e, &c, 10, 10, 10, 20, 0.5f, 30, 0, 0.5f, 0.5f);
    c.loop = 1;
    env_gate_on(&e);
    for (int i = 0; i < SR / 2; i++) lv[i] = env_next(&e);
    int per = (int)(0.08 * SR);
    float diff = 0, top = 0, low = 1;
    for (int i = per; i < SR / 2 - per; i++) {
        diff = fmaxf(diff, fabsf(lv[i] - lv[i + per]));
        top = fmaxf(top, lv[i]);
        low = fminf(low, lv[i]);
    }
    env_gate_off(&e);
    env_after(&e, 31);
    int stopped = e.stage == ENV_IDLE;
    snprintf(what, sizeof what, "loop: repeats every 80 ms (largest difference %.4f), %.2f..%.2f each cycle; key up stops it %s",
             diff, low, top, stopped ? "after the release" : "NOT");
    check(diff < 1e-3f && top == 1 && low == 0 && stopped, what);
    env_setup(&e, &c, 10, 10, 10, 20, 0.5f, 30, 0, 0.5f, 0.5f);
    c.loop = c.latch = 1;
    env_gate_on(&e);
    env_after(&e, 45);
    env_gate_off(&e);
    top = 0;
    for (int i = 0; i < SR / 2; i++) top = fmaxf(top, env_next(&e));
    snprintf(what, sizeof what, "loop + latch: keeps cycling after key up (peak %.2f in the next 500 ms)", top);
    check(top == 1 && e.stage != ENV_IDLE, what);

    /* latch through the engine: the note holds at sustain after key up until LATCH is switched off */
    void *en = patch(SAW, 1, 50, 50, 50);
    set(en, "amp_latch", 1);
    note(en, 1, 57, 127);
    render_f(en, x, SR / 2);
    double held = peak_in(x, 400, 500);
    note(en, 0, 57, 0);
    render_f(en, x, SR);
    double after = peak_in(x, 900, 1000);
    set(en, "amp_latch", 0);
    int gone = silent_after(en, 300);
    snprintf(what, sizeof what, "latch: key up keeps the sustain level (%.3f held, %.3f a second after key up); latch off releases it",
             held, after);
    check(held > 0.05 && fabs(after - held) < 0.1 * held && gone, what);
    E->destroy(en);

    /* delay through the engine: the note starts when its delay is over */
    en = patch(SAW, 1, 1000, 100, 100);
    set(en, "amp_delay", knob_ms(200));
    E->get_param(en, "amp_delay_display", txt, sizeof txt);
    note(en, 1, 57, 127);
    render_f(en, x, SR / 2);
    double before = peak_in(x, 0, 195), on = peak_in(x, 210, 300);
    snprintf(what, sizeof what, "amp delay \"%s\": %.4f before 195 ms, %.3f after 210 ms", txt, before, on);
    check(!strcmp(txt, "200 ms") && before < 1e-3 && on > 0.1, what);
    E->destroy(en);

    /* sync: an attack of a quarter note follows the tempo; the knob reads the division */
    double t95[2];
    for (int k = 0; k < 2; k++) {
        en = patch(SAW, 1, 1000, 100, 100);
        E->set_param(en, "lfo_bpm", k ? "60" : "120");
        set(en, "amp_sync", 1);
        set(en, "amp_a", 60);   /* the 5th-longest division: 1/4 */
        E->get_param(en, "amp_a_display", txt, sizeof txt);
        note(en, 1, 57, 127);
        render_f(en, x, SR * 3 / 2);
        t95[k] = time_to(x, SR * 3 / 2, 0.95f) / 44.1;
        E->destroy(en);
    }
    snprintf(what, sizeof what, "amp attack synced to \"%s\": 95%% after %.0f ms at 120 BPM, %.0f ms at 60 (want 475, 950)",
             txt, t95[0], t95[1]);
    check(!strcmp(txt, "1/4") && fabs(t95[0] - 475) < 15 && fabs(t95[1] - 950) < 25, what);

    /* time scale 200% doubles every stage time */
    double ts[2];
    for (int k = 0; k < 2; k++) {
        en = patch(SAW, 100, 1000, 100, 100);
        if (k) set(en, "amp_scale", 50 + 50 * log10(2));
        E->get_param(en, "amp_scale_display", k ? txt : txt2, sizeof txt);
        note(en, 1, 57, 127);
        render_f(en, x, SR / 2);
        ts[k] = time_to(x, SR / 2, 0.95f) / 44.1;
        E->destroy(en);
    }
    snprintf(what, sizeof what, "time scale \"%s\" vs \"%s\": a 100 ms attack reaches 95%% after %.0f ms, %.0f ms", txt2, txt,
             ts[0], ts[1]);
    check(!strcmp(txt2, "100%") && !strcmp(txt, "200%") && fabs(ts[1] / ts[0] - 2) < 0.1, what);

    /* velocity amount: 100% (the default) scales loudness by velocity as before, 0% ignores it, -100% inverts it */
    static const int amts[3] = {100, 0, -100};
    double loud[3][2];
    for (int k = 0; k < 3; k++)
        for (int v = 0; v < 2; v++) {
            en = patch(SAW, 1, 1000, 100, 100);
            set(en, "amp_vel", amts[k]);
            note(en, 1, 57, v ? 127 : 32);
            loud[k][v] = rms_of(en, SR / 4);
            E->destroy(en);
        }
    snprintf(what, sizeof what, "amp velocity amount 100/0%%: velocity 32 against 127 = %.3f / %.3f (want %.3f / 1); -100%%: "
             "rms %.3f at 32, %.4f at 127", loud[0][0] / loud[0][1], loud[1][0] / loud[1][1], 32 / 127.0, loud[2][0], loud[2][1]);
    check(fabs(loud[0][0] / loud[0][1] - 32 / 127.0) < 0.02 && fabs(loud[1][0] / loud[1][1] - 1) < 0.01 &&
          loud[2][0] > 10 * loud[2][1], what);

    /* multi trig and reset in mono: a legato note after the envelope has settled at 30% */
    double lev[3], first[3];
    for (int k = 0; k < 3; k++) {   /* 0: multi on (default), 1: multi off, 2: multi on + reset */
        en = patch(SAW, 50, 50, 30, 100);
        set(en, "voices", 0);
        if (k == 1) set(en, "amp_multi", 0);
        if (k == 2) set(en, "amp_reset", 1);
        note(en, 1, 57, 127);
        render_f(en, x, SR / 2);
        double settled = peak_in(x, 400, 500);
        note(en, 1, 60, 127);
        render_f(en, x, SR / 5);
        first[k] = peak_in(x, 0, 5) / settled;
        lev[k] = peak_in(x, 40, 60) / settled;
        E->destroy(en);
    }
    snprintf(what, sizeof what, "mono legato note, level against before: multi trig on %.2f (restarts), off %.2f (carries on); "
             "reset: first 5 ms %.2f (from 0), without %.2f", lev[0], lev[1], first[2], first[0]);
    check(lev[0] > 1.5 && fabs(lev[1] - 1) < 0.15 && first[2] < 0.5 && first[0] > 0.8, what);
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
    set(e, "voices", 7); set(e, "unison", 1); set(e, "uni_count", 2); set(e, "uni_detune", 0);
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

/* the pitch (Hz) of a held note after the wheel and pitch settings: render past the first control steps, then measure */
static double held_hz(void *e, int key) {
    static float x[N + 4096];
    note(e, 1, key, 127);
    render_f(e, x, N + 4096);
    return peak_hz(x + 4096);
}

/* which of the keys sound now (power at each fundamental against the strongest, dB), through a 600 Hz ladder */
static void key_powers(void *e, const int *keys, int n, double *db) {
    static float x[N + 8192];
    double hz[8];
    for (int i = 0; i < n; i++) hz[i] = note_hz(keys[i]);
    render_f(e, x, N + 8192);
    powers_db(x + 8192, hz, n, db);
}

/* a patch of held saws (sustain 100%, 10 ms release) through a 600 Hz ladder, with `voices` (option index) */
static void *held_patch(int voices) {
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "voices", voices); set(e, "flt_cut", knob_for_hz(600)); set(e, "volume", 40);
    return e;
}

/* the largest sample difference between two runs of a mono patch: a first note held for `blocks1` x 128 frames, then a
 * legato second note; compared over the 2048 samples after the second note */
static void reset_run(int reset, int blocks1, float *out) {
    static float x[128 * 32];
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "voices", 0); set(e, "o1_reset", reset); set(e, "volume", 40);
    note(e, 1, 57, 127);
    render_f(e, x, 128 * blocks1);
    note(e, 1, 60, 127);
    render_f(e, out, 2048);
    E->destroy(e);
}

/* Tier 2 voice and keyboard options: mono note priority, voice stealing modes, HOLD, sostenuto, pitch bend up/down and
 * per-oscillator offsets, velocity curves, transpose / octave shift / master and fine tune, key-track centre, pitch
 * tracking, note-on reset and the hard sync source */
static void voice_keyboard_options(void) {
    static float x[N + 8192];
    char what[260];
    double db[8];

    /* mono note priority: keys 48, 55, 52 held in that order; then the sounding key is released */
    static const int k3[] = {48, 52, 55};
    static const char *const PRIO[] = {"Low", "High", "Last"};
    static const int plays[3] = {48, 55, 52}, then[3] = {52, 52, 55};
    for (int pr = 0; pr < 3; pr++) {
        void *e = held_patch(0);
        set(e, "note_priority", pr);
        note(e, 1, 48, 100); note(e, 1, 55, 100); note(e, 1, 52, 100);
        double a[3], b[3];
        key_powers(e, k3, 3, a);
        note(e, 0, plays[pr], 0);
        key_powers(e, k3, 3, b);
        int ok = 1;
        for (int i = 0; i < 3; i++) {
            ok &= k3[i] == plays[pr] ? a[i] > -1 : a[i] < -25;
            ok &= k3[i] == then[pr] ? b[i] > -1 : b[i] < -25;
        }
        snprintf(what, sizeof what, "note priority %s: 48, 55, 52 held plays %d (%.0f/%.0f/%.0f dB); releasing it plays %d "
                 "(%.0f/%.0f/%.0f dB)", PRIO[pr], plays[pr], a[0], a[1], a[2], then[pr], b[0], b[1], b[2]);
        check(ok, what);
        E->destroy(e);
    }

    /* voice stealing with 2 voices full (48 then 52 held) and a third key, 55 */
    static const char *const STEAL[] = {"No Stealing", "Oldest", "Quietest", "Newest"};
    static const int stolen[4] = {55, 48, 52, 52};   /* the key that ends up silent */
    for (int m = 0; m < 4; m++) {
        void *e = held_patch(1);
        set(e, "steal_mode", m);
        note(e, 1, 48, 127); note(e, 1, 52, m == 2 ? 30 : 127);   /* Quietest: the newer, softer note goes */
        render_f(e, x, SR / 10);
        note(e, 1, 55, 127);
        key_powers(e, k3, 3, db);
        int ok = 1;
        for (int i = 0; i < 3; i++) ok &= k3[i] == stolen[m] ? db[i] < -25 : db[i] > -15;
        snprintf(what, sizeof what, "steal mode %s, 2 voices: 48 and 52 held, then 55: %d is silent (48 %.0f, 52 %.0f, "
                 "55 %.0f dB)", STEAL[m], stolen[m], db[0], db[1], db[2]);
        check(ok, what);
        E->destroy(e);
    }

    /* HOLD: released keys keep sounding; a legato key adds; a non-legato key replaces; with the pedal down it adds */
    void *e = held_patch(3);
    set(e, "hold", 1);
    note(e, 1, 48, 100); note(e, 1, 52, 100); note(e, 0, 48, 0); note(e, 0, 52, 0);
    double h1[3], h2[3], h3[3];
    key_powers(e, k3, 3, h1);
    note(e, 1, 55, 100); note(e, 0, 55, 0);   /* non-legato: replaces */
    key_powers(e, k3, 3, h2);
    midi3(e, 0xb0, 64, 127);
    note(e, 1, 48, 100); note(e, 0, 48, 0);   /* non-legato with the pedal down: adds */
    midi3(e, 0xb0, 64, 0);
    key_powers(e, k3, 3, h3);
    snprintf(what, sizeof what, "hold: 48 + 52 released still sound (%.0f, %.0f dB); 55 alone replaces them (48 %.0f, 52 "
             "%.0f, 55 %.0f dB); 48 with the pedal down adds (48 %.0f, 55 %.0f dB)", h1[0], h1[1], h2[0], h2[1], h2[2],
             h3[0], h3[2]);
    check(h1[0] > -6 && h1[1] > -6 && h2[0] < -25 && h2[1] < -25 && h2[2] > -1 && h3[0] > -6 && h3[2] > -6, what);
    set(e, "hold", 0);
    check(silent_after(e, 500), "hold: switching it off lets go of the latched notes");
    E->destroy(e);

    e = held_patch(3);   /* legato under hold adds */
    set(e, "hold", 1);
    note(e, 1, 48, 100); note(e, 1, 52, 100); note(e, 0, 48, 0); note(e, 1, 55, 100); note(e, 0, 52, 0);
    note(e, 0, 55, 0);
    key_powers(e, k3, 3, db);
    snprintf(what, sizeof what, "hold: keys played legato all stay (%.0f, %.0f, %.0f dB)", db[0], db[1], db[2]);
    check(db[0] > -6 && db[1] > -6 && db[2] > -6, what);
    E->destroy(e);

    /* sostenuto (CC 66): 48 held as it goes down holds after release; 52 played after it does not */
    e = held_patch(3);
    note(e, 1, 48, 100);
    midi3(e, 0xb0, 66, 127);
    note(e, 1, 52, 100); note(e, 0, 48, 0); note(e, 0, 52, 0);
    key_powers(e, k3, 3, db);
    snprintf(what, sizeof what, "sostenuto: the note held as it went down sounds on (%.0f dB), the one played after "
             "stops on release (%.0f dB)", db[0], db[1]);
    check(db[0] > -1 && db[1] < -25, what);
    midi3(e, 0xb0, 66, 0);
    check(silent_after(e, 500), "sostenuto: lifting the pedal lets the held note go");
    E->destroy(e);

    /* pitch bend up / down with their own ranges, the retired bend_range, and per-oscillator offsets (note 57: 220 Hz) */
    static const struct { int wheel_hi, wheel_lo; double up, down, osc, up_off, dn_off; double want_st; } B[] = {
        {0x7f, 0x7f, 12, -2, 1, 0, 0, 12}, {0x00, 0x00, 2, -7, 1, 0, 0, -7}, {0x7f, 0x7f, -5, -2, 1, 0, 0, -5},
        {0x7f, 0x7f, 2, -2, 1, 7, 0, 9}, {0x7f, 0x7f, 2, -2, 2, 0, 0, 2}, {0x00, 0x00, 2, -2, 1, 0, -12, -14},
    };
    for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "bend_up", B[i].up); set(e, "bend_down", B[i].down);
        set(e, "o1_bend_up_off", B[i].up_off); set(e, "o1_bend_dn_off", B[i].dn_off);   /* osc 1's offsets only */
        if (B[i].osc == 2) { set(e, "o1_level", 0); set(e, "o2_level", 100); set(e, "o1_bend_up_off", 7); }
        midi3(e, 0xe0, B[i].wheel_lo, B[i].wheel_hi);
        double got = held_hz(e, 57), want = 220 * pow(2, B[i].want_st / 12);
        snprintf(what, sizeof what, "bend up %+g / down %+g, osc %g offsets %+g / %+g, wheel %s: %.1f Hz (want %.1f)",
                 B[i].up, B[i].down, B[i].osc, B[i].osc == 2 ? 7.0 : B[i].up_off, B[i].dn_off,
                 B[i].wheel_hi ? "up" : "down", got, want);
        check(fabs(cents(got, want)) < 3, what);
        E->destroy(e);
    }
    e = E->create(NULL);
    set(e, "bend_range", 5);
    snprintf(what, sizeof what, "an older project's bend_range 5 becomes bend up %+g, down %+g", get(e, "bend_up"),
             get(e, "bend_down"));
    check(get(e, "bend_up") == 5 && get(e, "bend_down") == -5, what);
    E->destroy(e);

    /* velocity curves: the level at velocities 32, 64, 96 against 127 (amp velocity amount 100%: level = velocity) */
    static const char *const CURVE[] = {"Soft", "Medium", "Hard", "Compress", "Expand"};
    for (int c = 0; c < 5; c++) {
        double ref = 0, got[3], want[3];
        int ok = 1;
        for (int k = 0; k < 4; k++) {
            e = patch(SAW, 0, 1, 100, 10);
            set(e, "volume", 10); set(e, "vel_curve", c);
            int vel = k < 3 ? 32 * (k + 1) : 127;
            note(e, 1, 57, vel);
            double r = rms_of(e, SR / 4);
            E->destroy(e);
            if (k == 3) { ref = r; break; }
            got[k] = r;
            double v = vel / 127.0, u = 2 * v - 1;
            want[k] = c == 0 ? sqrt(v) : c == 1 ? v : c == 2 ? v * v : c == 3 ? 0.5 + 0.5 * u * fabs(u)
                    : 0.5 + 0.5 * copysign(sqrt(fabs(u)), u);
        }
        for (int k = 0; k < 3; k++) { got[k] /= ref; ok &= fabs(got[k] - want[k]) < 0.02; }
        snprintf(what, sizeof what, "velocity curve %s: levels %.2f %.2f %.2f at velocity 32/64/96 (want %.2f %.2f %.2f)",
                 CURVE[c], got[0], got[1], got[2], want[0], want[1], want[2]);
        check(ok, what);
    }

    /* transpose, octave shift, master and fine tune (note 57: 220 Hz) */
    static const struct { const char *k1; double v1; const char *k2; double v2; double want; } T[] = {
        {"transpose", 5, NULL, 0, 220 * 1.3348398541700344}, {"transpose", -12, NULL, 0, 110},
        {"octave_shift", 3, NULL, 0, 440}, {"octave_shift", 0, "transpose", 7, 55 * 1.4983070768766815},
        {"master_tune", 432, NULL, 0, 216}, {"fine_tune", 50, NULL, 0, 220 * 1.0293022366434921},
        {"master_tune", 415, "fine_tune", -100, 207.5 / 1.0594630943592953},
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, T[i].k1, T[i].v1);
        if (T[i].k2) set(e, T[i].k2, T[i].v2);
        double got = held_hz(e, 57);
        snprintf(what, sizeof what, "%s %g%s%s %g: note 57 plays %.2f Hz (want %.2f)", T[i].k1, T[i].v1, T[i].k2 ? ", " : "",
                 T[i].k2 ? T[i].k2 : "", T[i].k2 ? T[i].v2 : 0, got, T[i].want);
        check(fabs(cents(got, T[i].want)) < 2, what);
        E->destroy(e);
    }

    /* key-track centre: a self-oscillating ladder at 100% tracking rings an octave lower for key 72 with the centre
     * moved from 60 to 72 */
    double ring[2];
    for (int c = 0; c < 2; c++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "flt_res", 100); set(e, "flt_cut", knob_for_hz(440)); set(e, "lad_kbd", 100);
        set(e, "key_center", c ? 72 : 60);
        note(e, 1, 72, 100);
        render_f(e, x, N + 8192);
        ring[c] = peak_hz(x + 8192);
        E->destroy(e);
    }
    snprintf(what, sizeof what, "key track centre 60 -> 72: key 72 rings the ladder at %.0f Hz, then %.0f Hz (an octave "
             "down: %.0f cents)", ring[0], ring[1], cents(ring[0], ring[1]));
    check(fabs(cents(ring[0], ring[1]) - 1200) < 15, what);
    e = E->create(NULL);
    char kt[2][32];
    E->get_param(e, "key_center_display", kt[0], sizeof kt[0]);
    set(e, "key_center", 69); E->get_param(e, "key_center_display", kt[1], sizeof kt[1]);
    snprintf(what, sizeof what, "key track centre reads \"%s\" by default, \"%s\" at 69", kt[0], kt[1]);
    check(!strcmp(kt[0], "C4 (60)") && !strcmp(kt[1], "A4 (69)"), what);
    E->destroy(e);

    /* pitch tracking: 0% plays the centre note's pitch whatever the key, 200% doubles the distance from it */
    static const struct { double track; int key; double want; } P[] = {
        {0, 72, 261.6255653}, {0, 50, 261.6255653}, {100, 64, 329.6275569}, {200, 66, 523.2511306}, {200, 57, 184.9972114},
    };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "o1_track", P[i].track);
        double got = held_hz(e, P[i].key);
        snprintf(what, sizeof what, "pitch tracking %g%%: key %d plays %.2f Hz (want %.2f)", P[i].track, P[i].key, got,
                 P[i].want);
        check(fabs(cents(got, P[i].want)) < 3, what);
        E->destroy(e);
    }

    /* note-on reset: after a legato note the waveform is the same whatever the oscillator's phase was */
    static float r1[2048], r2[2048];
    double diff[2];
    for (int rs = 0; rs < 2; rs++) {
        reset_run(rs, 10, r1);
        reset_run(rs, 13, r2);
        diff[rs] = 0;
        for (int i = 16; i < 2048; i++) diff[rs] = fmax(diff[rs], fabs(r1[i] - r2[i]));
    }
    snprintf(what, sizeof what, "note-on reset: two legato notes after different held times differ by at most %.4f "
             "with it (%.3f without)", diff[1], diff[0]);
    check(diff[1] < 0.01 && diff[0] > 0.1, what);

    /* hard sync source: osc 2 synced to osc 3 repeats at osc 3's pitch, not osc 1's */
    static const struct { const char *slave, *src_key; int src; const char *master_freq; } S[] = {
        {"o2", "o2_sync_src", 0, "o3_freq"}, {"o2", "o2_sync_src", 1, "o3_freq"}, {"o3", "o3_sync_src", 1, "o2_freq"},
    };
    for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++) {
        char k[24];
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "o1_level", 0); set(e, "volume", 30);
        snprintf(k, sizeof k, "%s_level", S[i].slave); set(e, k, 100);
        snprintf(k, sizeof k, "%s_sync", S[i].slave); set(e, k, 1);
        snprintf(k, sizeof k, "%s_freq", S[i].slave); set(e, k, 4);   /* 277 Hz: no simple ratio to either */
        set(e, S[i].src_key, S[i].src);
        set(e, S[i].master_freq, -5);   /* the other oscillator: 164.8 Hz; osc 1 at 220 */
        note(e, 1, 57, 127);
        render_f(e, x, N + 8192);
        powers_db(x + 8192, (const double[]){note_hz(52), 220}, 2, db);
        int from_osc1 = S[i].src == 0;
        snprintf(what, sizeof what, "%s synced to %s: power at 164.8 Hz %.0f dB, at 220 Hz %.0f dB", S[i].slave,
                 from_osc1 ? "osc 1" : S[i].slave[1] == '2' ? "osc 3" : "osc 2", db[0], db[1]);
        check(from_osc1 ? db[0] < -30 && db[1] > -1 : db[1] < -30 && db[0] > -1, what);
        E->destroy(e);
    }
    e = patch(SAW, 0, 1, 100, 10);   /* each syncing the other: bounded */
    set(e, "o2_level", 100); set(e, "o3_level", 100); set(e, "o2_sync", 1); set(e, "o3_sync", 1);
    set(e, "o2_sync_src", 1); set(e, "o3_sync_src", 1); set(e, "o3_freq", 3);
    note(e, 1, 57, 127);
    double r = rms_of(e, SR / 4);
    snprintf(what, sizeof what, "osc 2 and 3 synced to each other: rms %.3f, finite", r);
    check(isfinite(r) && r > 0.01, what);
    E->destroy(e);
}

/* render `frames` of both channels as floats */
static void render_lr(void *s, float *l, float *r, int frames) {
    int16_t buf[256];
    for (int at = 0; at < frames; at += 128) {
        int n = frames - at < 128 ? frames - at : 128;
        E->render(s, buf, n);
        for (int i = 0; i < n; i++) { l[at + i] = buf[2 * i] / 32768.0f; r[at + i] = buf[2 * i + 1] / 32768.0f; }
    }
}

/* a noise-only patch: the noise channel into the open ladder, amp envelope 1 ms / full sustain / 10 ms */
static void *noise_patch(int pair, double mix) {
    void *e = patch(SAW, 0, 1, 100, 10);
    set(e, "o1_level", 0); set(e, "noise_level", 100); set(e, "volume", 30);
    set(e, "noise_color_pair", pair); set(e, "noise_color_mix", mix);
    return e;
}

/* octave-band slope (dB per octave, least squares over the bands at 250 Hz .. 4 kHz) and total RMS of a noise patch,
 * from 8 averaged spectra */
static double noise_slope(void *e, double *rms) {
    enum { K = 8 };
    static float x[N * K + 4096];
    static double avg[N / 2];
    note(e, 1, 60, 127);
    render_f(e, x, N * K + 4096);
    memset(avg, 0, sizeof avg);
    double sq = 0;
    for (int k = 0; k < K; k++) {
        const float *w = x + 4096 + k * N;
        for (int i = 0; i < N; i++) sq += w[i] * w[i];
        const double *pw = spectrum(w);
        for (int b = 0; b < N / 2; b++) avg[b] += pw[b];
    }
    *rms = sqrt(sq / (N * K));
    double sx = 0, sy = 0, sxx = 0, sxy = 0, bin = (double)SR / N;
    for (int o = 0; o < 5; o++) {
        double f = 250.0 * (1 << o), p = 0;
        for (int b = (int)(f / M_SQRT2 / bin); b <= (int)(f * M_SQRT2 / bin); b++) p += avg[b];
        double y = 10 * log10(p / (f * M_SQRT2 - f / M_SQRT2) + 1e-30);   /* power per Hz */
        sx += o; sy += y; sxx += o * o; sxy += o * y;
    }
    return (5 * sxy - sx * sy) / (5 * sxx - sx * sx);
}

/* RMS of x in 5 ms windows (env[i]: window i) */
static void rms_env(const float *x, int n, double *env) {
    enum { W = SR / 200 };
    for (int i = 0; i < n / W; i++) {
        double sq = 0;
        for (int j = 0; j < W; j++) sq += x[i * W + j] * x[i * W + j];
        env[i] = sqrt(sq / W);
    }
}

static double env_mean(const double *env, int from_ms, int to_ms) {
    double m = 0;
    for (int i = from_ms / 5; i < to_ms / 5; i++) m += env[i];
    return m / (to_ms / 5 - from_ms / 5);
}

static double db(double a, double b) { return 20 * log10((a + 1e-12) / (b + 1e-12)); }

/* noise colours, colour mix and the noise EG; synth pan and voice spread; analog drift */
static void noise_stereo_drift(void) {
    static float x[SR * 2], l[SR], r[SR], m[SR];
    static double env[SR * 2 / (SR / 200)];
    char what[220];
    enum { RW = 0, RP = 1, WP = 2 };   /* the "noise_color_pair" options */

    /* colours: red falls 6 dB per octave, purple rises 6, white is flat; each at about the same level (the same RMS
     * before the filter; the open ladder still trims the top octave, where purple has the most and red the least) */
    double rms[3], slope[3];
    void *e = noise_patch(RW, 0); slope[0] = noise_slope(e, &rms[0]); E->destroy(e);     /* red */
    e = noise_patch(RW, 100); slope[1] = noise_slope(e, &rms[1]); E->destroy(e);         /* white */
    e = noise_patch(WP, 100); slope[2] = noise_slope(e, &rms[2]); E->destroy(e);         /* purple */
    snprintf(what, sizeof what, "noise colours over 250 Hz-4 kHz: red %+.2f, white %+.2f, purple %+.2f dB/oct (want -6, 0, +6)",
             slope[0], slope[1], slope[2]);
    check(fabs(slope[0] + 6) < 0.6 && fabs(slope[1]) < 0.6 && fabs(slope[2] - 6) < 0.6, what);
    snprintf(what, sizeof what, "noise colours at about one level: red %+.1f dB, purple %+.1f dB against white",
             db(rms[0], rms[1]), db(rms[2], rms[1]));
    check(fabs(db(rms[0], rms[1])) < 2 && fabs(db(rms[2], rms[1])) < 2, what);

    /* colour mix: Red+Purple at 0 / 100 is the pure colour; Red+White halfway is in between (pink-ish), equal power */
    double s_rp0, s_rp100, s_pink, r_pink, dummy;
    e = noise_patch(RP, 0); s_rp0 = noise_slope(e, &dummy); E->destroy(e);
    e = noise_patch(RP, 100); s_rp100 = noise_slope(e, &dummy); E->destroy(e);
    e = noise_patch(RW, 50); s_pink = noise_slope(e, &r_pink); E->destroy(e);
    snprintf(what, sizeof what, "colour mix: Red+Purple at 0%% %+.1f, at 100%% %+.1f dB/oct; Red+White at 50%% %+.1f dB/oct "
             "(between), %+.1f dB against white", s_rp0, s_rp100, s_pink, db(r_pink, rms[1]));
    check(fabs(s_rp0 + 6) < 0.6 && fabs(s_rp100 - 6) < 0.6 && s_pink < -1 && s_pink > -5.5 && fabs(db(r_pink, rms[1])) < 1.5,
          what);

    /* the noise EG: attack 100 ms (linear), sustain on holds, release 100 ms after key up; sustain off decays */
    for (int sus = 1; sus >= 0; sus--) {
        e = noise_patch(RW, 100);
        set(e, "amp_r", knob_ms(2000));   /* the amp stays open after key up: what fades is the noise EG */
        set(e, "noise_env", 1); set(e, "noise_a", knob_ms(100)); set(e, "noise_r", knob_ms(100)); set(e, "noise_sus", sus);
        note(e, 1, 60, 127);
        render_f(e, x, SR / 2);
        note(e, 0, 60, 0);
        render_f(e, x + SR / 2, SR / 2);
        rms_env(x, SR, env);
        double top = env_mean(env, 150, 250);
        if (sus) {
            int t70 = 0;
            while (t70 < 200 && env[t70] < 0.7 * top) t70++;
            double early = env_mean(env, 0, 20), held = env_mean(env, 350, 500), after = env_mean(env, 750, 1000);
            snprintf(what, sizeof what, "noise EG, attack 100 ms: 70%% at %d ms, first 20 ms %.0f dB; sustain on holds "
                     "(%+.1f dB at 350-500 ms); release 100 ms: %.0f dB 250 ms after key up", t70 * 5, db(early, top),
                     db(held, top), db(after, top));
            check(t70 * 5 >= 55 && t70 * 5 <= 90 && db(early, top) < -12 && fabs(db(held, top)) < 1 && db(after, top) < -40,
                  what);
        } else {
            double peak = env_mean(env, 95, 115), late = env_mean(env, 350, 500);
            snprintf(what, sizeof what, "noise EG, sustain off: peaks after the attack, then the release acts as a decay "
                     "with the key down (%.0f dB at 350-500 ms)", db(late, peak));
            check(db(late, peak) < -40, what);
        }
        E->destroy(e);
    }
    e = noise_patch(RW, 100);   /* the noise EG off: the noise follows the amp envelope only */
    set(e, "noise_a", knob_ms(2000)); set(e, "noise_sus", 0);
    note(e, 1, 60, 127);
    render_f(e, x, SR / 2);
    rms_env(x, SR / 2, env);
    snprintf(what, sizeof what, "noise EG off (the default) leaves the noise alone: %+.1f dB at 10-50 ms against 300-500 ms",
             db(env_mean(env, 10, 50), env_mean(env, 300, 500)));
    check(fabs(db(env_mean(env, 10, 50), env_mean(env, 300, 500))) < 1, what);
    char ta[32], tr[32];
    set(e, "noise_a", 0); set(e, "noise_r", 50);
    E->get_param(e, "noise_a_display", ta, sizeof ta); E->get_param(e, "noise_r_display", tr, sizeof tr);
    snprintf(what, sizeof what, "noise EG knobs read \"%s\" at 0 and \"%s\" at 50", ta, tr);
    check(!strcmp(ta, "1.0 ms") && !strcmp(tr, "100 ms"), what);
    E->destroy(e);

    /* pan: equal power, the centre exactly as mono, hard left silences the right */
    static const int pans[] = {-100, -50, 0, 50, 100};
    double pw[5], lr[5][2];
    for (int k = 0; k < 5; k++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "volume", 30); set(e, "pan", pans[k]);
        note(e, 1, 48, 127);
        render_lr(e, l, r, SR / 2);
        double sl = 0, sr = 0;
        int same = 1;
        for (int i = SR / 4; i < SR / 2; i++) { sl += l[i] * l[i]; sr += r[i] * r[i]; }
        for (int i = 0; i < SR / 2; i++) same = same && l[i] == r[i];
        lr[k][0] = sl; lr[k][1] = sr; pw[k] = sl + sr;
        if (pans[k] == 0) check(same, "pan centre: left and right are the same samples");
        if (pans[k] == -100) {
            int silent = 1;
            for (int i = 0; i < SR / 2; i++) silent = silent && r[i] == 0;
            check(silent, "pan hard left: the right channel is silent");
        }
        E->destroy(e);
    }
    double lo = pw[0], hi = pw[0];
    for (int k = 1; k < 5; k++) { lo = fmin(lo, pw[k]); hi = fmax(hi, pw[k]); }
    snprintf(what, sizeof what, "pan law: total power within %.2f dB from hard left to hard right; hard left's left %+.2f dB "
             "against the centre's (want +3.01); pan +50%% leans right by %.1f dB", 10 * log10(hi / lo),
             10 * log10(lr[0][0] / lr[2][0]), 10 * log10(lr[3][1] / lr[3][0]));
    check(10 * log10(hi / lo) < 0.1 && fabs(10 * log10(lr[0][0] / lr[2][0]) - 3.01) < 0.1 && 10 * log10(lr[3][1] / lr[3][0]) > 3,
          what);

    /* spread: two voices move apart as spread goes up; one voice (mono) stays on the pan; a unison stack fans out */
    double side[3];
    static const int spreads[] = {0, 50, 100};
    for (int k = 0; k < 3; k++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "volume", 30); set(e, "voices", 1); set(e, "spread", spreads[k]);   /* 2 voices */
        note(e, 1, 48, 127); note(e, 1, 55, 127);
        render_lr(e, l, r, SR / 2);
        double d = 0, s = 0;
        for (int i = SR / 4; i < SR / 2; i++) { d += (l[i] - r[i]) * (l[i] - r[i]); s += (l[i] + r[i]) * (l[i] + r[i]); }
        side[k] = d / s;
        E->destroy(e);
    }
    snprintf(what, sizeof what, "spread places two voices apart: side/mid power %.3f, %.3f, %.3f at 0, 50, 100%%",
             side[0], side[1], side[2]);
    check(side[0] == 0 && side[1] > 0.01 && side[2] > side[1] * 2, what);
    for (int uni = 0; uni < 2; uni++) {
        e = patch(SAW, 0, 1, 100, 10);
        set(e, "volume", 30); set(e, "voices", 0); set(e, "spread", 100);   /* mono */
        if (uni) { set(e, "unison", 1); set(e, "uni_count", 0); set(e, "uni_detune", 30); }   /* a stack of 2 */
        note(e, 1, 48, 127); note(e, 0, 48, 0); note(e, 1, 50, 127);
        render_lr(e, l, r, SR / 2);
        double d = 0, s = 0;
        for (int i = 0; i < SR / 2; i++) { d += (l[i] - r[i]) * (l[i] - r[i]); s += l[i] * l[i]; }
        snprintf(what, sizeof what, uni ? "spread 100%%, mono with a unison stack of 2: the stack fans out (side %.3f)"
                                        : "spread 100%%, mono: the one voice stays on the pan (side %.3f)", d / s);
        check(uni ? d / s > 0.5 : d == 0, what);
        E->destroy(e);
    }

    /* the mod effect: its input is the unpanned mono sum, so a hard-left voice's chorus wet sits at the pan (centre) */
    e = patch(SAW, 0, 1, 100, 10);
    set(e, "volume", 30); set(e, "voices", 1); set(e, "spread", 100);
    set(e, "modfx_on", 1); set(e, "modfx_type", SFX_CHORUS); set(e, "modfx_mix", 100);
    note(e, 1, 48, 127);   /* the first note's place is the right (ORDER[1]) */
    render_lr(e, l, r, SR / 2);
    double sl = 0, sr = 0;
    for (int i = SR / 4; i < SR / 2; i++) { sl += l[i] * l[i]; sr += r[i] * r[i]; }
    snprintf(what, sizeof what, "chorus at full mix on a voice spread hard right: the wet reaches the left too (%+.1f dB L/R)",
             10 * log10(sl / sr));
    check(sl > sr * 0.1, what);
    E->destroy(e);

    /* drift: 0 changes nothing at all; 100% wanders within +-4 cents */
    void *a = patch(TRI, 0, 1, 100, 10), *b = patch(TRI, 0, 1, 100, 10);
    set(b, "drift", 100); set(b, "drift", 0);
    note(a, 1, 60, 127); note(b, 1, 60, 127);
    render_lr(a, l, r, SR / 2);
    render_lr(b, m, x, SR / 2);
    int same = 1;
    for (int i = 0; i < SR / 2; i++) same = same && l[i] == m[i] && r[i] == x[i];
    check(same, "drift 0: the same samples as without it");
    E->destroy(a); E->destroy(b);
    for (int amt = 0; amt <= 100; amt += 100) {
        enum { WIN = 12 };
        static float y[N * WIN + 4096];
        e = patch(TRI, 0, 1, 100, 10);
        set(e, "volume", 30); set(e, "drift", amt);
        note(e, 1, 81, 127);   /* 880 Hz */
        render_f(e, y, N * WIN + 4096);
        double lo_c = 1e9, hi_c = -1e9;
        for (int k = 0; k < WIN; k++) {
            double c = cents(peak_hz(y + 4096 + k * N), 880);
            lo_c = fmin(lo_c, c); hi_c = fmax(hi_c, c);
        }
        snprintf(what, sizeof what, "drift %d%%: pitch over %.1f s stays within %+.2f .. %+.2f cents", amt,
                 WIN * (double)N / SR, lo_c, hi_c);
        check(amt ? lo_c > -4.3 && hi_c < 4.3 && hi_c - lo_c > 0.5 : hi_c - lo_c < 0.05, what);
        E->destroy(e);
    }
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

/* each effect alone on a saw, its typical settings: microseconds per 128-frame block (the effects run once, shared) */
static double fx_time(const fx_params_t *p) {
    enum { BLOCKS = 20000 };
    static float x[128], l[128], r[128];
    for (int i = 0; i < 128; i++) x[i] = 2.0f * (float)fmod(i * 110.0 / SR, 1.0) - 1.0f;
    memset(&fxs, 0, sizeof fxs);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < BLOCKS; b++) fx_process(&fxs, p, NULL, x, x, l, r, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
}

static void fx_bench(void) {
    printf("bench: effect buffers %.2f MB (synth effect %.2f, delay %.2f, reverb %.2f), allocated with the instance\n",
           sizeof(fx_t) / 1048576.0, sizeof(sfx_t) / 1048576.0, sizeof(mdly_t) / 1048576.0, sizeof(rev_t) / 1048576.0);
    for (int t = 0; t < SFX_NTYPES; t++) {
        fx_params_t p = fx_none();
        p.sfx_on = 1; p.sfx_type = t; p.mix = 0.5f; p.fb = 0.3f; p.depth = 0.5f; p.dtime_ms = 350; p.noise = 0.2f;
        p.time_ms = fx_mod_time_ms(t, 60); p.time_r_ms = fx_mod_time_ms(t, 70); p.vib_depth = 0.2f; p.tone = 0.7f;
        printf("bench: synth FX %-15s %6.2f us per 128-frame block\n", SFX_NAMES[t], fx_time(&p));
    }
    fx_params_t p = fx_none();
    p.send1 = 0.3f; p.dly_fb = 0.4f; p.dly_r_ms = 375;
    printf("bench: master delay (stereo)    %6.2f us per 128-frame block\n", fx_time(&p));
    for (int t = 0; t < 3; t++) {
        p = fx_none(); p.send2 = 0.3f; p.rev_type = t;
        printf("bench: master reverb %-10s %6.2f us per 128-frame block\n", REV_NAMES[t], fx_time(&p));
    }
    p = fx_none(); p.sfx_on = 1; p.sfx_type = SFX_ENSEMBLE; p.mix = 0.5f; p.send1 = 0.3f; p.send2 = 0.3f;
    p.rev_type = REV_HALL; p.series = 0.3f;
    printf("bench: ensemble + delay + hall  %6.2f us per 128-frame block (the heaviest likely chain)\n", fx_time(&p));
}

static void write_wav(const char *path, const int16_t *pcm, uint32_t frames) {
    uint32_t bytes = frames * 4;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); failures++; return; }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SR); put32(f, SR * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes); fwrite(pcm, 4, frames, f);
    fclose(f);
}

/* one short phrase per effect (a chord, a run, the tail) into <dir>/fx_<name>.wav, to listen to each type */
static void fx_wavs(const char *demo_path) {
    enum { CHORD = 344, NOTE = 86, TAIL = 516 };   /* 1 s, 0.25 s, 1.5 s in 128-frame blocks */
    static int16_t pcm[(CHORD + 4 * NOTE + TAIL) * 256];
    static const int run[4] = {64, 67, 72, 74};
    char dir[256], path[320];
    snprintf(dir, sizeof dir, "%s", demo_path);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    for (int k = 0; k < SFX_NTYPES + 5; k++) {
        void *e = patch(SAW, 5, 400, 70, 300);
        set(e, "o2_level", 70); set(e, "o2_freq", 0.08); set(e, "flt_cut", 70); set(e, "volume", 70);
        const char *name;
        if (k < SFX_NTYPES) {
            name = SFX_NAMES[k];
            set(e, "modfx_on", 1); set(e, "modfx_type", k);
            if (k <= SFX_ECHO) set(e, "modfx_mix", 35);
            if (k == SFX_CRUSH) { set(e, "modfx_mix", 100); set(e, "modfx_bits", 6); }
            if (k == SFX_RESON) set(e, "modfx_mix", 80);
        } else if (k == SFX_NTYPES) {   /* 300 ms left, 450 ms right */
            name = "master_delay"; set(e, "mfx_send1", 40); set(e, "dly_time", 80.8); set(e, "dly_time_r", 84.9);
        } else if (k == SFX_NTYPES + 1) {
            name = "master_pingpong"; set(e, "mfx_send1", 40); set(e, "dly_pp", 1); set(e, "dly_fb", 45);
        } else {
            name = REV_NAMES[k - SFX_NTYPES - 2]; set(e, "mfx_send2", 40); set(e, "rev_type", k - SFX_NTYPES - 2);
        }
        int16_t *o = pcm;
        note(e, 1, 48, 100); note(e, 1, 55, 100); note(e, 1, 60, 100);
        for (int b = 0; b < CHORD; b++, o += 256) E->render(e, o, 128);
        note(e, 0, 48, 0); note(e, 0, 55, 0); note(e, 0, 60, 0);
        for (int n = 0; n < 4; n++) o = play(e, o, run[n], NOTE);
        o = rest(e, o, TAIL);
        E->destroy(e);
        snprintf(path, sizeof path, "%sfx_%s.wav", dir, name);
        write_wav(path, pcm, (uint32_t)((o - pcm) / 2));
    }
    printf("wrote %d effect WAVs to %s (fx_*.wav)\n", SFX_NTYPES + 5, dir[0] ? dir : "./");
}

/* a busy patch (three oscillators, both filters in series, the matrix busy) held at 1, 4 and 8 voices */
static void bench(void) {
    enum { BLOCKS = 50000 };
    int16_t buf[256];
    static const int counts[] = {1, 4, 8}, opt[] = {0, 3, 7};
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
    set(u, "voices", 7); set(u, "o2_level", 80); set(u, "o3_level", 60);
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
    for (int fm = 0; fm < 2; fm++) {   /* 8 voices of the busy patch with the SVF pair in series, filter FM off and on */
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7); set(e, "o2_level", 80); set(e, "o3_level", 60);
        route(e, "o1", 2); set(e, "flt_route", 1); set(e, "lad_env", 40); set(e, "flt_res", 50);
        set(e, "mod2_src", 6); set(e, "mod2_dst", 9); set(e, "mod2_amt", 30);
        set(e, "svf_pair", SVF_SERIES); set(e, "svf_space", 25);
        if (fm) { set(e, "lad_fm", 40); set(e, "svf_fm", 40); set(e, "svf_fm_src", 2); }
        for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        printf("bench: 8 voices, SVF pair in series, filter FM %s: %6.2f us per 128-frame block (%.2f per voice)\n",
               fm ? "on " : "off", us, us / 8);
        E->destroy(e);
    }
    {   /* 8 voices of the busy patch with deep modulation: four per-voice LFOs (note reset, sine morphs, fades), all 16
         * slots busy, including per-voice envelope times and LFO rates moved by an LFO every control step */
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7); set(e, "o2_level", 80); set(e, "o3_level", 60);
        route(e, "o1", 2); set(e, "flt_route", 1); set(e, "lad_env", 40); set(e, "flt_res", 50);
        static const char *const lk[] = {"lfo1", "lfo2", "lfo3", "lfo4"};
        char k[32];
        for (int n = 0; n < 4; n++) {
            snprintf(k, sizeof k, "%s_reset", lk[n]); set(e, k, 1);
            snprintf(k, sizeof k, "%s_wave", lk[n]); set(e, k, LFO_SINE);
            snprintf(k, sizeof k, "%s_var", lk[n]); set(e, k, 40);
            snprintf(k, sizeof k, "%s_fade_in", lk[n]); set(e, k, 80);
            snprintf(k, sizeof k, "%s_smooth", lk[n]); set(e, k, 20);
        }
        set(e, "angle_lfo3", 30); set(e, "pw_lfo3", 30);
        static const int dst[16] = {MD_PITCH, MD_LAD_CUT, MD_SVF_CUT, MD_O1_PW, MD_O2_ANGLE, MD_AMP, MD_FLT_MIX, MD_O3_LEVEL,
                                    MD_FENV_DELAY + MDE_ATTACK, MD_FENV_DELAY + MDE_COUNT + MDE_DECAY,
                                    MD_FENV_DELAY + 2 * MDE_COUNT + MDE_SCALE, MD_LFO1_RATE + MDL_RATE,
                                    MD_LFO1_RATE + MDL_COUNT + MDL_SEMIS, MD_LFO1_RATE + 2 * MDL_COUNT + MDL_VAR,
                                    MD_LAD_ENV, MD_O1_BEAT};
        static const int src[4] = {MS_LFO1, MS_LFO2, MS_LFO3, MS_LFO4};
        for (int s = 0; s < 16; s++) {
            snprintf(k, sizeof k, "mod%d_src", s + 1); set(e, k, src[s % 4]);
            snprintf(k, sizeof k, "mod%d_dst", s + 1); set(e, k, dst[s]);
            snprintf(k, sizeof k, "mod%d_amt", s + 1); set(e, k, 5);
            snprintf(k, sizeof k, "mod%d_via", s + 1); set(e, k, MS_VEL);
            snprintf(k, sizeof k, "mod%d_xf", s + 1); set(e, k, s % 5);
        }
        for (int n = 0; n < 8; n++) note(e, 1, 48 + 3 * n, 100);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        printf("bench: 8 voices, deep modulation (4 per-voice LFOs, 16 slots, envelope times and LFO rates moving): "
               "%6.2f us per 128-frame block (%.2f per voice)\n", us, us / 8);
        E->destroy(e);
    }
    for (int k = 0; k < 2; k++) {   /* 8 voices of the busy patch: spread and drift; then also coloured noise with its EG */
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7); set(e, "o2_level", 80); set(e, "o3_level", 60);
        route(e, "o1", 2); set(e, "flt_route", 1); set(e, "lad_env", 40); set(e, "flt_res", 50);
        set(e, "mod2_src", 6); set(e, "mod2_dst", 9); set(e, "mod2_amt", 30);
        set(e, "spread", 100); set(e, "pan", -20); set(e, "drift", 100);
        if (k) { set(e, "noise_level", 40); set(e, "noise_color_mix", 50); set(e, "noise_env", 1); }
        for (int j = 0; j < 8; j++) note(e, 1, 48 + 3 * j, 100);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        printf("bench: 8 voices, spread + drift%s: %6.2f us per 128-frame block (%.2f per voice)\n",
               k ? " + red/white noise with its EG" : "", us, us / 8);
        E->destroy(e);
    }
    void *e = patch(SAW, 5, 300, 70, 250);   /* the effects on top of 8 voices */
    set(e, "voices", 7); set(e, "modfx_on", 1); set(e, "modfx_type", SFX_CHORUS); set(e, "mfx_send1", 30);
    set(e, "mfx_send2", 30);
    for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
    printf("bench: 4 voices + chorus, delay, reverb %6.2f us per 128-frame block on this PC\n", us);
    E->destroy(e);
    fx_bench();
}

/* a triangle (near a sine, so zero crossings track its pitch) through the open ladder, for pitch modulation */
static void *pitch_patch(void) {
    void *e = patch(TRI, 0, 1, 100, 10);
    set(e, "flt_cut", 100);
    return e;
}

/* play note 57 (220 Hz) for ms after `idle_ms` of silence; the tracked pitch (MIDI note numbers) from the note-on */
static int pitch_run(void *e, int idle_ms, int ms, double *t, double *st) {
    static float x[SR * 3];
    render_f(e, x, SR * idle_ms / 1000);
    note(e, 1, 57, 127);
    render_f(e, x, SR * ms / 1000);
    return track(x, SR * ms / 1000, t, st);
}

/* the LFO options and the deep matrix: wave shapes and variation, polarity, start phase and note-on reset, delay and
 * fades, repeats, smoothing, ranges, synced divisions with dotted / triplet timing; LFO 3's hard-wires to wave angle
 * and pulse width; source amounts past 100%, controller amount and inversion, a controller alone, the transforms;
 * slots 9-16, the new sources and destinations (envelope times and time scale, LFO rate in semitones) */
static void lfo_matrix(void) {
    static double t[MAXPTS], st[MAXPTS];
    static float x[SR * 2];
    char what[240], txt[32];

    /* lfo.h wave shapes at a few phases */
    lfo_t l;
    lfo_init(&l, 1);
    const struct { int wave; float var, phase, want; const char *what; } w[] = {
        {LFO_TRI, 0, 0, -1, "triangle starts at -1"}, {LFO_TRI, 0, 0.25f, 0, "triangle crosses 0 at a quarter"},
        {LFO_TRI, 1, 0.125f, -0.70711f, "triangle at variation 100% is a sine"},
        {LFO_SINE, 0, 0.25f, 1, "sine peaks at a quarter"}, {LFO_SINE, 1, 0.125f, 0.5f, "sine at 100% is a triangle"},
        {LFO_SAW, 0, 0.25f, 0.5f, "saw falls"}, {LFO_SAW, 0.5f, 0.25f, 0, "saw at 50% is a triangle"},
        {LFO_RAMP, 0, 0.25f, -0.5f, "ramp rises"}, {LFO_SQUARE, 0, 0.4f, 1, "square high in its first half"},
        {LFO_SQUARE, 0.6f, 0.3f, -1, "square at 60% is a 20% pulse"}, {LFO_SQUARE, 1, 0.01f, -1, "square at 100% is always low"},
    };
    int ok = 1;
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) {
        l.phase = w[i].phase;
        float y = lfo_shape(&l, w[i].wave, w[i].var);
        if (fabsf(y - w[i].want) > 1e-4f) { printf("     %s: %.4f, want %.4f\n", w[i].what, y, w[i].want); ok = 0; }
    }
    check(ok, "LFO shapes: triangle/sine, saw/ramp morphs, square width from the variation");
    lfo_init(&l, 7);
    float sh_step = 0, nz_step = 0, sh_last = 0, nz_last = 0;
    for (int i = 0; i < 4000; i++) {   /* 100 steps per cycle */
        lfo_advance(&l, 0.01f);
        float a = lfo_shape(&l, LFO_SH, 0), b = lfo_shape(&l, LFO_NOISE, 0);
        if (i) { sh_step = fmaxf(sh_step, fabsf(a - sh_last)); nz_step = fmaxf(nz_step, fabsf(b - nz_last)); }
        sh_last = a; nz_last = b;
    }
    snprintf(what, sizeof what, "S&H jumps (largest step %.2f), smooth noise glides (largest step %.3f)", sh_step, nz_step);
    check(sh_step > 0.5f && nz_step < 0.05f, what);

    /* the matrix's arithmetic */
    float slots[MOD_SLOTS * MOD_SLOT_PARAMS] = {0}, extra[MOD_SLOTS * MOD_SLOT_EXTRA] = {0}, src[MS_COUNT] = {0}, dst[MD_COUNT];
    src[MS_VEL] = 0.5f; src[MS_WHEEL] = 0.5f; src[MS_LFO1] = -0.8f; src[MS_AT] = 0.9f; src[MS_CONST] = 1;
    const struct { float s, d, amt, via, via_amt, xf, xf_amt, want; const char *what; } m[] = {
        {MS_VEL, MD_PITCH, 300, MS_OFF, 100, MX_NONE, 0, 1.5f, "source amount 300%"},
        {MS_VEL, MD_PITCH, 100, MS_WHEEL, -100, MX_NONE, 0, 0.25f, "controller amount -100% inverts the controller"},
        {MS_VEL, MD_PITCH, 100, MS_WHEEL, 0, MX_NONE, 0, 0.5f, "controller amount 0 ignores the controller"},
        {MS_VEL, MD_PITCH, 100, MS_WHEEL, 50, MX_NONE, 0, 0.375f, "controller amount 50%"},
        {MS_OFF, MD_PITCH, 0, MS_WHEEL, -80, MX_NONE, 0, -0.4f, "a controller alone is the source"},
        {MS_LFO1, MD_PITCH, 100, MS_OFF, 100, MX_LIMIT_LOW, 50, 0, "limit low 50% of a bipolar LFO: 0"},
        {MS_AT, MD_PITCH, 100, MS_OFF, 100, MX_LIMIT_HIGH, 25, 0.25f, "limit high 25% of aftertouch"},
        {MS_LFO1, MD_PITCH, 200, MS_OFF, 100, MX_SQUARED, 0, 1.28f, "squared, then the amount"},
        {MS_LFO1, MD_PITCH, 100, MS_OFF, 100, MX_CUBED, 0, -0.512f, "cubed keeps the sign"},
        {MS_CONST, MD_LFO1_RATE + MDL_SEMIS, -50, MS_OFF, 100, MX_NONE, 0, -0.5f, "constant to a new destination"},
    };
    ok = 1;
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
        float sl[] = {m[i].s, m[i].d, m[i].amt, m[i].via}, ex[] = {m[i].via_amt, m[i].xf, m[i].xf_amt};
        memset(slots, 0, sizeof slots);
        memcpy(slots + (MOD_SLOTS - 1) * MOD_SLOT_PARAMS, sl, sizeof sl);   /* in the last slot: all 16 are read */
        memcpy(extra + (MOD_SLOTS - 1) * MOD_SLOT_EXTRA, ex, sizeof ex);
        mod_eval(slots, extra, src, 1u << MS_LFO1, dst);
        float got = dst[(int)m[i].d];
        if (fabsf(got - m[i].want) > 1e-5f) { printf("     %s: %.4f, want %.4f\n", m[i].what, got, m[i].want); ok = 0; }
    }
    check(ok, "matrix: amounts to 300%, controller amount and inversion, controller alone, limit/squared/cubed");

    /* LFO 3 to wave angle (unipolar square: angle 50% -> 100%, triangle -> saw) and to pulse width (bipolar square:
     * a square's width swings 5% / 95%); measured by the 2nd harmonic, which the symmetric shapes lack */
    for (int k = 0; k < 2; k++) {
        double h2[2];
        for (int on = 0; on < 2; on++) {
            void *e = patch(k ? SQUARE : TRI, 0, 1, 100, 10);
            set(e, "flt_cut", 100); set(e, "lfo3_wave", LFO_SQUARE); set(e, "lfo3_rate", 100 * log10(0.5 / 0.01) / 3);
            set(e, "lfo3_reset", 1);
            if (!k) set(e, "lfo3_polarity", 1);
            set(e, k ? "pw_lfo3" : "angle_lfo3", on ? 100 : 0);
            note(e, 1, 57, 127);
            render_f(e, x, N + 2205);   /* the first second is LFO 3's high half */
            h2[on] = band_db(x + 2205, 440) - band_db(x + 2205, 220);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "LFO 3 to %s at 0 / 100%%: 2nd harmonic %.1f / %.1f dB against the 1st",
                 k ? "pulse width" : "wave angle", h2[0], h2[1]);
        check(h2[0] < -40 && h2[1] > -12, what);
    }

    /* note-on reset and start phase: a 1 Hz square vibrato of +-12 st starts high at phase 0, low at 50% */
    for (int ph = 0; ph < 2; ph++) {
        void *e = pitch_patch();
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(1 / 0.01) / 3); set(e, "pitch_lfo", 100);
        set(e, "lfo1_reset", 1); set(e, "lfo1_phase", ph ? 50 : 0);
        int k = pitch_run(e, 333, 1000, t, st);
        double a = pitch_at(t, st, k, 250), b = pitch_at(t, st, k, 750);
        snprintf(what, sizeof what, "note-on reset, start phase %d%%: note %.2f at 250 ms, %.2f at 750 ms (want %s)",
                 ph ? 50 : 0, a, b, ph ? "45 then 69" : "69 then 45");
        check(fabs(a - (ph ? 45 : 69)) < 0.3 && fabs(b - (ph ? 69 : 45)) < 0.3, what);
        E->destroy(e);
    }

    /* delay (the restarted LFO waits, then starts at its start phase), fade in, repeats and smoothing */
    {
        void *e = pitch_patch();
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(1 / 0.01) / 3); set(e, "pitch_lfo", 100);
        set(e, "lfo1_reset", 1); set(e, "lfo1_delay", knob_ms(500));
        E->get_param(e, "lfo1_delay_display", txt, sizeof txt);
        int k = pitch_run(e, 100, 1600, t, st);
        double a = pitch_at(t, st, k, 300), b = pitch_at(t, st, k, 800), c = pitch_at(t, st, k, 1300);
        snprintf(what, sizeof what, "LFO delay \"%s\": note %.2f at 300 ms, %.2f at 800 ms, %.2f at 1300 ms (want 57, 69, 45)",
                 txt, a, b, c);
        check(!strcmp(txt, "500 ms") && fabs(a - 57) < 0.3 && fabs(b - 69) < 0.3 && fabs(c - 45) < 0.3, what);
        E->destroy(e);

        e = pitch_patch();
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(0.5 / 0.01) / 3); set(e, "pitch_lfo", 100);
        set(e, "lfo1_reset", 1); set(e, "lfo1_fade_in", knob_ms(1000));
        k = pitch_run(e, 0, 900, t, st);
        a = pitch_at(t, st, k, 250); b = pitch_at(t, st, k, 750);
        snprintf(what, sizeof what, "LFO fade in over 1 s: +%.2f st at 250 ms, +%.2f st at 750 ms (want about 3 and 9)",
                 a - 57, b - 57);
        check(fabs(a - 60) < 0.6 && fabs(b - 66) < 0.6, what);
        E->destroy(e);

        e = pitch_patch();
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(2 / 0.01) / 3); set(e, "pitch_lfo", 100);
        set(e, "lfo1_repeats", 1);
        k = pitch_run(e, 77, 1000, t, st);
        a = pitch_at(t, st, k, 120); b = pitch_at(t, st, k, 380); c = pitch_at(t, st, k, 800);
        snprintf(what, sizeof what, "LFO repeats 1 at 2 Hz: note %.2f, %.2f, then %.2f after its cycle (want 69, 45, 57)", a, b, c);
        check(fabs(a - 69) < 0.3 && fabs(b - 45) < 0.3 && fabs(c - 57) < 0.3, what);
        E->destroy(e);

        e = pitch_patch();
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(2 / 0.01) / 3); set(e, "pitch_lfo", 100);
        set(e, "lfo1_smooth", 100);
        k = pitch_run(e, 0, 1000, t, st);
        double lo = 1e9, hi = -1e9;
        for (int i = 0; i < k; i++) if (t[i] > 100) { lo = fmin(lo, st[i]); hi = fmax(hi, st[i]); }
        snprintf(what, sizeof what, "LFO smoothing 100%% (0.1 Hz) on a 2 Hz square of +-12 st: the pitch stays within "
                 "%.2f .. %.2f (want inside 57 +-2.5)", lo, hi);
        check(lo > 54.5 && hi < 59.5, what);
        E->destroy(e);
    }

    /* ranges and synced divisions with their timing, as the rate knob reads them */
    {
        void *e = E->create(NULL);
        static const struct { const char *k; double v; const char *want; } d[] = {
            {"lfo1_range", 0, "1.0 Hz"}, {"lfo1_range", 2, "100 Hz"}, {"lfo1_sync", 1, "1/256"},
            {"lfo1_timing", 1, "1/256 D"}, {"lfo1_rate", 50, "1/4 D"}, {"lfo1_timing", 2, "1/4 T"},
        };
        set(e, "lfo1_rate", 100);
        ok = 1;
        for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) {
            set(e, d[i].k, d[i].v);
            E->get_param(e, "lfo1_rate_display", txt, sizeof txt);
            if (strcmp(txt, d[i].want)) { printf("     %s = %g: \"%s\", want \"%s\"\n", d[i].k, d[i].v, txt, d[i].want); ok = 0; }
        }
        check(ok, "LFO rate text: Slow tops out at 1 Hz, Medium at 100 Hz; synced 16 bars .. 1/256, dotted, triplet");
        E->destroy(e);
    }

    /* slot 9 (the new slots), the constant source, controller inversion by the mod wheel */
    for (int wheel = 0; wheel < 2; wheel++) {
        void *e = pitch_patch();
        set(e, "mod9_src", MS_CONST); set(e, "mod9_dst", MD_PITCH); set(e, "mod9_amt", 50);
        set(e, "mod9_via", MS_WHEEL); set(e, "mod9_via_amt", -100); set(e, "mod1_amt", 0);   /* no wheel vibrato */
        midi3(e, 0xb0, 1, wheel ? 127 : 0);
        int k = pitch_run(e, 0, 300, t, st);
        double a = pitch_at(t, st, k, 200);
        snprintf(what, sizeof what, "slot 9: Constant -> Pitch 50%% via the mod wheel at -100%%, wheel %s: note %.2f (want %d)",
                 wheel ? "up" : "down", a, wheel ? 57 : 69);
        check(fabs(a - (wheel ? 57 : 69)) < 0.3, what);
        E->destroy(e);
    }

    /* expression (CC 11) as a source; LFO 1 rate +12 st from the matrix doubles a 1 Hz square vibrato */
    {
        void *e = pitch_patch();
        set(e, "mod10_src", MS_EXP1); set(e, "mod10_dst", MD_PITCH); set(e, "mod10_amt", -25);
        midi3(e, 0xb0, 11, 127);
        int k = pitch_run(e, 0, 300, t, st);
        double a = pitch_at(t, st, k, 200);
        snprintf(what, sizeof what, "Exp 1 (CC 11) full -> Pitch -25%%: note %.2f (want 51)", a);
        check(fabs(a - 51) < 0.3, what);
        E->destroy(e);

        e = pitch_patch();
        set(e, "lfo1_wave", LFO_SQUARE); set(e, "lfo1_rate", 100 * log10(1 / 0.01) / 3); set(e, "pitch_lfo", 100);
        set(e, "lfo1_reset", 1);
        set(e, "mod16_src", MS_CONST); set(e, "mod16_dst", MD_LFO1_RATE + MDL_SEMIS); set(e, "mod16_amt", 50);
        k = pitch_run(e, 0, 1000, t, st);
        a = pitch_at(t, st, k, 150);
        double b = pitch_at(t, st, k, 380), c = pitch_at(t, st, k, 650);
        snprintf(what, sizeof what, "matrix LFO 1 Rate (st) +12: the 1 Hz square runs at 2 Hz: notes %.2f, %.2f, %.2f (want 69, 45, 69)",
                 a, b, c);
        check(fabs(a - 69) < 0.3 && fabs(b - 45) < 0.3 && fabs(c - 69) < 0.3, what);
        E->destroy(e);
    }

    /* envelope times per voice: velocity -> amp attack (+25% of the knob = x10 at full velocity), and the time scale */
    {
        double ms[3];
        for (int k = 0; k < 3; k++) {
            void *e = patch(SAW, 100, 1000, 100, 100);
            if (k < 2) { set(e, "mod11_src", MS_VEL); set(e, "mod11_dst", MD_FENV_DELAY + MDE_COUNT * 1 + MDE_ATTACK);
                         set(e, "mod11_amt", 25); }
            else { set(e, "mod11_src", MS_CONST); set(e, "mod11_dst", MD_FENV_DELAY + MDE_COUNT * 1 + MDE_SCALE);
                   set(e, "mod11_amt", -50); }
            set(e, "amp_vel", 0);
            note(e, 1, 69, k == 0 ? 1 : 127);
            render_f(e, x, SR * 3 / 2);
            ms[k] = time_to(x, SR * 3 / 2, 0.95f) / 44.1;
            E->destroy(e);
        }
        snprintf(what, sizeof what, "matrix to amp attack (100 ms knob): 95%% after %.0f ms at velocity 1, %.0f ms at 127 "
                 "(want about 100 / 1000); time scale -50%%: %.0f ms (want about 10)", ms[0], ms[1], ms[2]);
        check(ms[0] > 85 && ms[0] < 115 && ms[1] > 850 && ms[1] < 1100 && ms[2] < 20, what);
    }
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
    svf_pair();
    routing();
    modulation();
    matrix();
    lfo_matrix();
    voices();
    effects();
    effects_tier3();
    envelope();
    keys();
    envelope_extras();
    state();
    glide_unison();
    voice_keyboard_options();
    noise_stereo_drift();
    demo_wav(argc > 1 ? argv[1] : "demo.wav");
    fx_wavs(argc > 1 ? argv[1] : "demo.wav");
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
