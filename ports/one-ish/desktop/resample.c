/* The engine-to-host rate converter, and the host-to-engine one for the audio input (resample.h). */
#include <math.h>
#include <pthread.h>
#include <string.h>
#include "resample.h"

#define MASK (RS_RING - 1)
#define TABLE (RS_TAPS * RS_PHASES)   /* kernel points from -RS_TAPS/2 to +RS_TAPS/2 */
#define TINY 1e-30f                   /* smaller is set to 0: no denormals reach the filter or the host */

/* the kernel, RS_PHASES points per sample, and the same points in rows: row p holds the RS_TAPS points one output
 * sample uses when its position is p / RS_PHASES past an engine frame (oldest frame first), so the filter reads two
 * neighbouring rows straight through and interpolates between them */
static float K[TABLE + 1], ROWS[(RS_PHASES + 1) * RS_TAPS];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 200 && term > sum * 1e-17; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/* sinc with its first zero one sample out (cutoff at half the lower rate), under a Kaiser window */
static void build_table(void) {
    const double half = RS_TAPS / 2.0, norm = bessel_i0(RS_BETA);
    for (int m = 0; m <= TABLE; m++) {
        double u = (double)m / RS_PHASES - half, w = 1.0 - (u / half) * (u / half);
        double sinc = fabs(u) < 1e-12 ? 1.0 : sin(M_PI * u) / (M_PI * u);
        double k = w > 0.0 ? sinc * bessel_i0(RS_BETA * sqrt(w)) / norm : 0.0;
        K[m] = fabs(k) < TINY ? 0.0f : (float)k;
    }
    for (int p = 0; p <= RS_PHASES; p++)
        for (int k = 0; k < RS_TAPS; k++) ROWS[p * RS_TAPS + k] = K[p + (RS_TAPS - 1 - k) * RS_PHASES];
}

void rs_init(void) { pthread_once(&once, build_table); }

int rs_rate_ok(int fs) { return fs >= RS_MIN_RATE && fs <= RS_MAX_RATE; }

/* the kernel's half width in input frames, and its stretch: 1 when the output rate is the higher, else in / out */
static int half_for(int in_fs, int fs, double *scale) {
    double s = fs >= in_fs ? 1.0 : (double)in_fs / fs;
    if (scale) *scale = s;
    return s == 1.0 ? RS_TAPS / 2 : (int)ceil(s * RS_TAPS / 2.0) + 1;
}

int rs_delay_io(int in_fs, int fs) {
    if (fs == in_fs || !rs_rate_ok(fs) || !rs_rate_ok(in_fs)) return 0;
    return (int)ceil(half_for(in_fs, fs, NULL) * (double)fs / in_fs - 1e-9);
}
int rs_delay_for(int fs) { return rs_delay_io(RS_RATE, fs); }

void rs_reset_io(resampler_t *r, int in_fs, int fs) {
    if (!rs_rate_ok(in_fs)) in_fs = RS_RATE;
    if (!rs_rate_ok(fs)) fs = in_fs;
    r->in_fs = in_fs;
    r->fs = fs;
    r->pass = fs == in_fs;
    r->half = r->pass ? 0 : half_for(in_fs, fs, &r->scale);
    if (r->pass) r->scale = 1.0;
    r->delay = rs_delay_io(in_fs, fs);
    int64_t num = -(int64_t)r->delay * in_fs;   /* output sample 0 sits at input frame -delay * in_fs / fs */
    r->pos = num >= 0 ? num / fs : -((-num + fs - 1) / fs);
    r->rem = num - r->pos * fs;
    r->frames = 0;
    r->quiet = RS_RING;   /* (the silent history before frame 0) */
    memset(r->ring, 0, sizeof r->ring);   /* before frame 0: silence */
}
void rs_reset(resampler_t *r, int fs) { rs_reset_io(r, RS_RATE, rs_rate_ok(fs) ? fs : RS_RATE); }

int64_t rs_need(const resampler_t *r) { return r->pos + r->half + 1; }

void rs_write(resampler_t *r, const float *lr, int frames) {
    for (int i = 0; i < frames; i++, r->frames++) {
        int s = (int)(r->frames & MASK);
        float a = lr[2 * i], b = lr[2 * i + 1];
        if (fabsf(a) < TINY) a = 0.0f;
        if (fabsf(b) < TINY) b = 0.0f;
        r->quiet = a == 0.0f && b == 0.0f ? r->quiet + 1 : 0;   /* (int64: never overflows) */
        r->ring[2 * s] = r->ring[2 * (s + RS_RING)] = a;
        r->ring[2 * s + 1] = r->ring[2 * (s + RS_RING) + 1] = b;
    }
}

/* one output sample at engine position pos + rem / fs: sum over engine frames j of x[j] * K((position - j) / scale) */
static void filter(const resampler_t *r, float *yl, float *yr) {
    const int taps = 2 * r->half;
    const float *x = &r->ring[2 * ((r->pos - r->half + 1) & MASK)];   /* frame pos - half + 1 onwards, contiguous */
    const double frac = (double)r->rem / r->fs;
    float al = 0.0f, ar = 0.0f;
    if (r->scale == 1.0) {   /* from 44.1 kHz up: taps whole samples apart, one row (half == RS_TAPS / 2) */
        double idx = frac * RS_PHASES;
        int p = (int)idx;
        float f = (float)(idx - p);
        const float *a = &ROWS[p * RS_TAPS], *b = a + RS_TAPS;
        for (int k = 0; k < taps; k++) {
            float c = a[k] + f * (b[k] - a[k]);
            al += c * x[2 * k];
            ar += c * x[2 * k + 1];
        }
    } else {   /* below 44.1 kHz: the kernel stretched by scale (and scaled down by it, for unity gain) */
        const double step = RS_PHASES / r->scale;
        double idx = ((frac + r->half - 1) / r->scale + RS_TAPS / 2.0) * RS_PHASES;
        for (int k = 0; k < taps; k++, idx -= step) {
            if (idx < 0.0 || idx >= TABLE) continue;
            int m = (int)idx;
            float c = K[m] + (float)(idx - m) * (K[m + 1] - K[m]);
            al += c * x[2 * k];
            ar += c * x[2 * k + 1];
        }
        al *= (float)(1.0 / r->scale);
        ar *= (float)(1.0 / r->scale);
    }
    *yl = fabsf(al) < TINY ? 0.0f : al;
    *yr = fabsf(ar) < TINY ? 0.0f : ar;
}

int rs_out(resampler_t *r, float *l, float *rt, int n, int add) {
    int k = 0;
    for (; k < n && rs_need(r) <= r->frames; k++) {
        float yl, yr;
        if ((r->pass ? r->pos : r->pos - r->half + 1) >= r->frames - r->quiet) {   /* every frame it reads is 0 */
            yl = yr = 0.0f;
        } else if (r->pass) {
            const float *x = &r->ring[2 * (r->pos & MASK)];
            yl = x[0];
            yr = x[1];
        } else {
            filter(r, &yl, &yr);
        }
        if (add) { l[k] += yl; rt[k] += yr; }
        else { l[k] = yl; rt[k] = yr; }
        r->rem += r->in_fs;   /* the next position: in_fs / fs input frames on */
        if (r->rem >= r->fs) {
            int64_t q = r->rem / r->fs;
            r->pos += q;
            r->rem -= q * r->fs;
        }
    }
    return k;
}
