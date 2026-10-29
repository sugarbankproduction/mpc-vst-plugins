/* load_test's host sample rate checks (desktop/hostrate.c): the DLL at 44.1, 48 and 96 kHz in 64, 512 and 1000-frame
 * blocks. An A4 on a plain triangle must be 440 Hz within 1 cent and as loud at every rate; no NaN, no denormal, no
 * jump at a block boundary; the latency reported is the converter's; at 44.1 kHz the float output, quantised as the
 * engine does, is exactly the old 16-bit path's (TEST_LEGACY); a note sent mid-block starts at its own time, not the
 * block's; an arpeggio synced to the host's song position lands on the same timeline spots at 48 kHz as at 44.1;
 * and what a block costs at each rate.
 * The audio input (input_tests, 2026-10-29): the DLL declares two inputs; a band-limited pulse fed in at 44.1, 48 and
 * 96 kHz in 64, 512 and 1000-frame blocks (and in place, in == out) comes out of the EXT channel through an open SVF
 * where the input path's delay says (TEST_INPUT, checked against its own formula), its shape intact, the input never
 * running dry; at EXT level 0 nothing of it is heard; and a vocoder (when the type list has one) answers it. */
#define TEST_INPUT 0x4F31494E    /* as hostrate.h: the audio input's delay (x 1000), or its dry frames */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define TEST_LEGACY 0x4F31534C   /* as hostrate.h: effVendorSpecific index for the old 16-bit path */

typedef struct AEffect AEffect;
typedef intptr_t (*master_t)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} midi_ev_t;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} time_info_t;

static int fails;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/* the host: its rate, its sample clock and transport (120 bpm from ppq 0 at sample 0) */
static double h_fs = 44100;
static int64_t h_pos;
static int h_playing;
static time_info_t h_ti;
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)p; (void)o;
    if (op == 1) return 2400;   /* audioMasterVersion */
    if (op != 7) return 0;      /* audioMasterGetTime */
    memset(&h_ti, 0, sizeof h_ti);
    h_ti.samplePos = (double)h_pos;
    h_ti.sampleRate = h_fs;
    h_ti.tempo = 120.0;
    h_ti.ppqPos = h_pos / h_fs * 2.0;
    h_ti.flags = (h_playing ? 2 : 0) | 1 << 9 | 1 << 10;
    return (intptr_t)&h_ti;
}

static AEffect *open_fx(AEffect *(*entry)(master_t)) {
    AEffect *fx = entry(host);
    if (fx) fx->dispatcher(fx, 0, 0, 0, NULL, 0);   /* effOpen */
    return fx;
}
static void close_fx(AEffect *fx) {
    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 1, 0, 0, NULL, 0);
}
static void set_rate(AEffect *fx, int fs, int block) {   /* as a host does: suspend, rate, block size, resume */
    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 10, 0, 0, NULL, (float)fs);
    fx->dispatcher(fx, 11, 0, block, NULL, 0);
    fx->dispatcher(fx, 12, 0, 1, NULL, 0);
    h_fs = fs;
    h_pos = 0;
}
static void set_patch(AEffect *fx, const char *state) {   /* a whole sound: the defaults plus these values */
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s", state);
    fx->dispatcher(fx, 24, 0, (intptr_t)strlen(buf) + 1, buf, 0);   /* effSetChunk */
}
static void midi(AEffect *fx, int status, int d1, int d2, int delta) {
    midi_ev_t ev = {.type = 1, .byteSize = sizeof ev, .deltaFrames = delta, .midiData = {status, d1, d2}};
    struct { int32_t n; intptr_t reserved; void *ev[2]; } list = {1, 0, {&ev, NULL}};
    fx->dispatcher(fx, 25, 0, 0, &list, 0);
}

enum { MAXN = 96000 * 3 };
static float L[MAXN], R[MAXN];
static long nonfinite, subnormal;
static double block_us;   /* time spent in processReplacing by the last render() */
/* n frames from `at` in host blocks of `block` (the last one shorter), counting NaN / inf and denormals */
static void render(AEffect *fx, int at, int n, int block) {
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (int i = at; i < at + n; i += block) {
        int k = at + n - i < block ? at + n - i : block;
        float *out[2] = {L + i, R + i};
        fx->processReplacing(fx, NULL, out, k);
        h_pos += k;
    }
    QueryPerformanceCounter(&t1);
    block_us = (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart;
    for (int i = at; i < at + n; i++) {
        if (!isfinite(L[i]) || !isfinite(R[i])) nonfinite++;
        if (fpclassify(L[i]) == FP_SUBNORMAL || fpclassify(R[i]) == FP_SUBNORMAL) subnormal++;
    }
}

/* the pitch from rising zero crossings (interpolated) over x[from..to) */
static double pitch(const float *x, int from, int to, double fs) {
    double first = -1, last = -1;
    int count = 0;
    for (int i = from + 1; i < to; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    return count > 1 ? (count - 1) * fs / (last - first) : 0;
}

static int onsets(const float *x, int n, double thr, int gap, double *at, int max) {   /* starts after >= gap quiet */
    int k = 0, quiet = gap;
    for (int i = 1; i < n && k < max; i++) {
        if (fabsf(x[i]) < thr) { quiet++; continue; }
        if (quiet >= gap) at[k++] = i - 1 + (thr - fabsf(x[i - 1])) / (fabsf(x[i]) - fabsf(x[i - 1]));
        quiet = 0;
    }
    return k;
}

static int expected_latency(int fs) { return fs == 44100 ? 0 : (int)ceil(36.0 * fs / 44100 - 1e-9); }

static void tone_tests(AEffect *fx) {
    static const int rates[] = {44100, 48000, 96000}, blocks[] = {64, 512, 1000};
    char what[256];
    double rms44 = 0;
    set_patch(fx, "v=1;o1_wave=0");   /* a plain triangle: no drift, no vibrato, no effects */
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            int fs = rates[a], bl = blocks[b], n = (int)(1.5 * fs);
            set_rate(fx, fs, bl);
            snprintf(what, sizeof what, "%d Hz: latency reported %d (expected %d)", fs, fx->initialDelay, expected_latency(fs));
            if (b == 0) check(fx->initialDelay == expected_latency(fs), what);
            midi(fx, 0x90, 69, 100, 0);
            render(fx, 0, n, bl);
            midi(fx, 0x80, 69, 0, 0);
            double hz = pitch(L, (int)(0.3 * fs), (int)(1.4 * fs), fs), cents = 1200 * log2(hz / 440.0), e = 0;
            for (int i = (int)(0.3 * fs); i < (int)(1.4 * fs); i++) e += L[i] * L[i];
            double rms = sqrt(e / (1.1 * fs));
            if (a == 0 && b == 0) rms44 = rms;
            double db = 20 * log10(rms / rms44), jump_in = 0, jump_edge = 0;   /* biggest step inside blocks / across edges */
            for (int i = (int)(0.3 * fs); i < (int)(1.4 * fs); i++) {
                double d = fabs(L[i] - L[i - 1]);
                if (i % bl == 0) { if (d > jump_edge) jump_edge = d; }
                else if (d > jump_in) jump_in = d;
            }
            snprintf(what, sizeof what, "%6d Hz, %4d-frame blocks: A4 at %.4f Hz (%+.3f cents), level %+.3f dB against "
                     "44.1 kHz, largest step across a block edge %.5f (inside %.5f)", fs, bl, hz, cents, db, jump_edge, jump_in);
            check(fabs(cents) < 1.0 && fabs(db) < 0.05 && jump_edge <= jump_in * 1.01, what);
            render(fx, 0, fs, bl);   /* the release */
        }
}

/* 44.1 kHz: the new float output against the old 16-bit path, sample by sample: the same DLL's (TEST_LEGACY) or,
 * given one, an older build's own output */
static void exact_test(AEffect *(*entry)(master_t), AEffect *(*old_entry)(master_t)) {
    static const int blocks[] = {64, 512, 1000}, progs[] = {0, 5, 11};
    static float A[2][MAXN];
    char what[200];
    for (int b = 0; b < 3; b++)
        for (int p = 0; p < 3; p++) {
            int n = 2 * 44100, bad = 0, finer = 0;
            for (int run = 0; run < 2; run++) {   /* run 0: the old path (TEST_LEGACY), run 1: the new one */
                AEffect *fx = open_fx(run == 0 && old_entry ? old_entry : entry);
                if (!fx) { check(0, "a second instance opens"); return; }
                if (!old_entry) fx->dispatcher(fx, 50, TEST_LEGACY, run == 0, NULL, 0);
                set_rate(fx, 44100, blocks[b]);
                fx->dispatcher(fx, 2, 0, progs[p], NULL, 0);
                static const int chord[] = {48, 55, 60, 64};
                for (int k = 0; k < 4; k++) midi(fx, 0x90, chord[k], 100, 0);
                render(fx, 0, n / 2, blocks[b]);
                for (int k = 0; k < 4; k++) midi(fx, 0x80, chord[k], 0, 0);
                render(fx, n / 2, n - n / 2, blocks[b]);
                memcpy(A[run], L, sizeof(float) * n);
                close_fx(fx);
            }
            for (int i = 0; i < n; i++) {
                int old = (int)lrintf(A[0][i] * 32768.0f), now = (int16_t)(A[1][i] * 32767.0f);
                bad += old != now;
                finer += A[1][i] != A[0][i];
            }
            snprintf(what, sizeof what, "44.1 kHz, %4d-frame blocks, program %2d: float output quantised as the engine does "
                     "equals the %s 16-bit output in %d of %d samples (%d samples finer than 16 bits)",
                     blocks[b], progs[p], old_entry ? "older DLL's" : "old path's", n - bad, n, finer);
            check(bad == 0 && finer > 0, what);
        }
}

/* a note sent at frame 700 of a 1000-frame block starts 700 frames later than one sent at frame 0 (to within half an
 * engine block, 64 engine frames, and the engine's 16-frame control step), not at the block start */
static void midi_time_test(AEffect *fx, int fs) {
    char what[200];
    double at[2][4];
    set_patch(fx, "v=1;o1_wave=0;amp_a=0;amp_s=100;amp_r=0;o1_reset=1");
    set_rate(fx, fs, 1000);
    for (int k = 0; k < 2; k++) {
        render(fx, 0, 3000, 1000);
        midi(fx, 0x90, 84, 100, k ? 700 : 0);
        render(fx, 0, 3000, 1000);
        midi(fx, 0x80, 84, 0, 0);
        if (onsets(L, 3000, 0.02, 500, at[k], 4) < 1) at[k][0] = -1e6;
    }
    double d = at[1][0] - at[0][0], tol = (64.0 + 16.0) * fs / 44100 + 1;
    snprintf(what, sizeof what, "%d Hz: a note at frame 700 of a 1000-frame block starts %.1f frames after one at frame 0 "
             "(expected 700 +- %.0f)", fs, d, tol);
    check(fabs(d - 700) <= tol, what);
}

/* an arpeggio on the host's song position (1/16 at 120 bpm): each step at 48 kHz lands where it does at 44.1 kHz,
 * moved by the rate and the reported latency */
static void arp_test(AEffect *fx) {
    static double on44[64], on48[64];
    static const int blocks48[] = {512, 1000};
    char what[256];
    set_patch(fx, "v=1;o1_wave=0;amp_a=0;amp_s=100;amp_r=0;o1_reset=1;arp_on=1;arp_div=4");
    h_playing = 1;
    set_rate(fx, 44100, 512);
    midi(fx, 0x90, 84, 100, 0);
    render(fx, 0, 2 * 44100, 512);
    midi(fx, 0x80, 84, 0, 0);
    int n44 = onsets(L, 2 * 44100, 0.02, 400, on44, 64);
    double lo = 1e9, hi = -1e9;   /* each step's start against the grid (step k at k * 5512.5 frames) */
    for (int k = 1; k < n44; k++) {
        double d = on44[k] - k * 5512.5;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    /* a step is due on its exact sample; its voice starts sounding at the engine's next 16-frame control step */
    snprintf(what, sizeof what, "44.1 kHz: %d arp steps, each %.1f to %.1f frames after its 1/16 grid line", n44, lo, hi);
    check(n44 > 10 && lo >= 0 && hi - lo <= 17, what);
    render(fx, 0, 44100, 512);
    for (int b = 0; b < 2; b++) {
        set_rate(fx, 48000, blocks48[b]);
        midi(fx, 0x90, 84, 100, 0);
        render(fx, 0, 2 * 48000, blocks48[b]);
        midi(fx, 0x80, 84, 0, 0);
        int n48 = onsets(L, 2 * 48000, 0.02, 400, on48, 64), matched = 0;
        double worst = 0;
        for (int k = 1; k < n44; k++) {   /* (step 0 starts with the key itself) */
            double want = on44[k] * 48000.0 / 44100 + fx->initialDelay, best = 1e9;
            for (int j = 0; j < n48; j++) if (fabs(on48[j] - want) < fabs(best)) best = on48[j] - want;
            if (fabs(best) < 50) matched++;
            if (fabs(best) > worst) worst = fabs(best);
        }
        snprintf(what, sizeof what, "48 kHz, %4d-frame blocks: %d of %d arp steps where 44.1 kHz puts them (+ latency %d), "
                 "worst %.2f samples off", blocks48[b], matched, n44 - 1, fx->initialDelay, worst);
        check(matched == n44 - 1 && worst <= 2.0, what);
        render(fx, 0, 48000, blocks48[b]);
    }
    h_playing = 0;
}

/* ---- the audio input ---- */
static float IL[MAXN], IR[MAXN];
/* n frames from `at` in host blocks of `block` with IL / IR as the input (inplace: the input copied into the output
 * buffers and processed there, as some hosts do) */
static void render_in(AEffect *fx, int at, int n, int block, int inplace) {
    for (int i = at; i < at + n; i += block) {
        int k = at + n - i < block ? at + n - i : block;
        float *out[2] = {L + i, R + i}, *in[2] = {IL + i, IR + i};
        if (inplace) {
            memcpy(L + i, IL + i, sizeof(float) * k);
            memcpy(R + i, IR + i, sizeof(float) * k);
            fx->processReplacing(fx, out, out, k);
        } else {
            fx->processReplacing(fx, in, out, k);
        }
        h_pos += k;
    }
    for (int i = at; i < at + n; i++) {
        if (!isfinite(L[i]) || !isfinite(R[i])) nonfinite++;
        if (fpclassify(L[i]) == FP_SUBNORMAL || fpclassify(R[i]) == FP_SUBNORMAL) subnormal++;
    }
}

/* the input path's delay as hostrate.h works it out: the FIFO's lead plus the input converter's delay (engine frames)
 * at the host's rate, plus the output converter's latency */
static double expected_input_delay(int fs) {
    if (fs == 44100) return 128;
    int half = fs > 44100 ? (int)ceil(36.0 * fs / 44100) + 1 : 36;   /* the input converter's half width, host samples */
    int d = (int)ceil(half * 44100.0 / fs - 1e-9);
    return (130 + d) * (double)fs / 44100 + expected_latency(fs);
}

/* where y holds x's shape best (normalised cross-correlation over lags 0..maxlag, parabolic peak) */
static double best_lag(const float *x, const float *y, int from, int to, int maxlag, double *corr) {
    double best = -1, bl = 0, c[3] = {0};
    double ex = 0;
    for (int i = from; i < to; i++) ex += (double)x[i] * x[i];
    for (int lag = 0; lag <= maxlag; lag++) {
        double s = 0, ey = 0;
        for (int i = from; i < to; i++) { s += (double)x[i] * y[i + lag]; ey += (double)y[i + lag] * y[i + lag]; }
        double r = ey > 0 && ex > 0 ? s / sqrt(ex * ey) : 0;
        if (r > best) { best = r; bl = lag; }
    }
    for (int k = -1; k <= 1; k++) {
        int lag = (int)bl + k;
        double s = 0;
        if (lag >= 0) for (int i = from; i < to; i++) s += (double)x[i] * y[i + lag];
        c[k + 1] = s;
    }
    double den = c[0] - 2 * c[1] + c[2];
    *corr = best;
    return bl + (den != 0 ? 0.5 * (c[0] - c[2]) / den : 0);
}

/* a band-limited pulse (a Blackman-windowed sinc, 6 kHz, 3 ms wide) at sample t0, peak amp */
static void pulse_in(int fs, int n, int t0, float amp) {
    memset(IL, 0, sizeof(float) * n);
    int w = (int)(0.003 * fs);
    for (int i = -w / 2; i <= w / 2; i++) {
        double x = 2 * 6000.0 / fs * i, s = i ? sin(M_PI * x) / (M_PI * x) : 1.0, u = (double)(i + w / 2) / w;
        double win = 0.42 - 0.5 * cos(2 * M_PI * u) + 0.08 * cos(4 * M_PI * u);
        if (t0 + i >= 0 && t0 + i < n) IL[t0 + i] = (float)(amp * s * win);
    }
    memcpy(IR, IL, sizeof(float) * n);
}

static void input_tests(AEffect *fx) {
    static const int rates[] = {44100, 48000, 96000}, blocks[] = {64, 512, 1000};
    char what[300];
    snprintf(what, sizeof what, "the plugin declares %d audio inputs and %d outputs", fx->numInputs, fx->numOutputs);
    check(fx->numInputs == 2 && fx->numOutputs == 2, what);
    /* EXT alone into an open SVF: no oscillators, no effects, the amp envelope full on */
    static const char *const PATCH = "v=1;o1_level=0;mfx_on=0;modfx_on=0;amp_a=0;amp_s=100;volume=100;ext_route=1;"
                                     "flt_mix=0;ext_level=%d";
    char patch[256];
    long dry0 = (long)fx->dispatcher(fx, 50, TEST_INPUT, 1, NULL, 0);
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 4; b++) {
            int fs = rates[a], bl = blocks[b < 3 ? b : 1], inplace = b == 3, n = fs / 2, t0 = fs / 5;
            if (inplace && fs != 48000) continue;
            set_rate(fx, fs, bl);
            snprintf(patch, sizeof patch, PATCH, 100);
            set_patch(fx, patch);
            double want = fx->dispatcher(fx, 50, TEST_INPUT, 0, NULL, 0) / 1000.0, formula = expected_input_delay(fs);
            midi(fx, 0x90, 60, 127, 0);
            pulse_in(fs, n, t0, 0.3f);
            render_in(fx, 0, n, bl, inplace);
            midi(fx, 0x80, 60, 0, 0);
            double corr, lag = best_lag(IL, L, t0 - fs / 500, t0 + fs / 500, 700 * fs / 44100, &corr);
            /* the open SVF adds a little delay of its own over the pulse's band: about 0.16 engine samples */
            double svf = 0.16 * fs / 44100.0;
            snprintf(what, sizeof what, "input %6d Hz, %4d-frame blocks%s: the pulse comes out %.2f samples later "
                     "(the input path %.3f, its formula %.3f, + the SVF's %.2f), its shape %.4f alike", fs, bl,
                     inplace ? " in place" : "", lag, want, formula, svf, corr);
            check(fabs(want - formula) < 0.002 && fabs(lag - want - svf) < 0.25 && corr > 0.99, what);
            render(fx, 0, fs / 4, bl);   /* the release */
        }
    long dry = (long)fx->dispatcher(fx, 50, TEST_INPUT, 1, NULL, 0) - dry0;
    snprintf(what, sizeof what, "the input never ran dry at any rate or block size (%ld engine frames did)", dry);
    check(dry == 0, what);
    {   /* level 0: nothing of the input is heard */
        int fs = 48000, n = fs / 2;
        set_rate(fx, fs, 512);
        snprintf(patch, sizeof patch, PATCH, 0);
        set_patch(fx, patch);
        midi(fx, 0x90, 60, 127, 0);
        pulse_in(fs, n, fs / 5, 0.3f);
        render_in(fx, 0, n, 512, 0);
        midi(fx, 0x80, 60, 0, 0);
        double peak = 0;
        for (int i = 0; i < n; i++) peak = fmax(peak, fabs(L[i]));
        snprintf(what, sizeof what, "input at EXT level 0: output peak %.2g", peak);
        check(peak < 1e-4, what);
        render(fx, 0, fs / 4, 512);
    }
    {   /* the 16 band vocoder (modfx_type 23) as the synth effect, a chord as its carrier: the input plays it */
        int fs = 48000, n = fs;
        double rms[2];
        for (int k = 0; k < 2; k++) {
            set_rate(fx, fs, 512);
            set_patch(fx, "v=1;mfx_on=0;modfx_on=1;modfx_type=23;modfx_mix=100;amp_a=0;amp_s=100");
            for (int t = 0; t < n; t++) IL[t] = IR[t] = k ? (float)(0.3 * sin(2 * M_PI * 700.0 * t / fs)) : 0.0f;
            for (int m = 0; m < 3; m++) midi(fx, 0x90, 48 + 7 * m, 100, 0);
            render_in(fx, 0, n, 512, 0);
            for (int m = 0; m < 3; m++) midi(fx, 0x80, 48 + 7 * m, 0, 0);
            double e = 0;
            for (int i = n / 2; i < n; i++) e += (double)L[i] * L[i];
            rms[k] = sqrt(e / (n / 2));
            render(fx, 0, fs / 2, 512);
        }
        snprintf(what, sizeof what, "48 kHz: the 16 band vocoder plays a chord with the input (rms %.4f), silent "
                 "without it (rms %.2g)", rms[1], rms[0]);
        check(rms[1] > 0.01 && rms[0] == 0, what);
    }
}

static void bench(AEffect *fx) {
    static const int rates[] = {44100, 48000, 96000};
    fx->dispatcher(fx, 2, 0, 0, NULL, 0);   /* program 0, a four-note chord */
    for (int a = 0; a < 3; a++) {
        int fs = rates[a], n = 512 * (fs / 512);
        set_rate(fx, fs, 512);
        static const int chord[] = {48, 55, 60, 64};
        for (int k = 0; k < 4; k++) midi(fx, 0x90, chord[k], 100, 0);
        render(fx, 0, 512 * 8, 512);
        render(fx, 0, n, 512);
        double per = block_us / (n / 512), real = 512.0 / fs * 1e6;
        printf("     CPU %6d Hz: %.1f us per 512-frame block, chord of 4 (%.2f%% of real time)\n", fs, per, 100 * per / real);
        for (int k = 0; k < 4; k++) midi(fx, 0x80, chord[k], 0, 0);
        render(fx, 0, fs, 512);
    }
}

int rate_tests(void *entry_fn, void *old_entry_fn) {
    AEffect *(*entry)(master_t) = (AEffect * (*)(master_t)) entry_fn;
    AEffect *fx = open_fx(entry);
    if (!fx) { check(0, "rate tests: an instance opens"); return fails; }
    nonfinite = subnormal = 0;
    tone_tests(fx);
    midi_time_test(fx, 48000);
    midi_time_test(fx, 44100);
    arp_test(fx);
    input_tests(fx);
    char what[160];
    snprintf(what, sizeof what, "every sample above was finite (%ld not) and none denormal (%ld)", nonfinite, subnormal);
    check(nonfinite == 0 && subnormal == 0, what);
    bench(fx);
    close_fx(fx);
    exact_test(entry, NULL);
    if (old_entry_fn) exact_test(entry, (AEffect * (*)(master_t)) old_entry_fn);
    return fails;
}
