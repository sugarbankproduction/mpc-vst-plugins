/* Sample-rate conversion from the engine's 44.1 kHz to the host's rate (desktop only; hostrate.c drives it).
 *
 * A band-limited interpolator: each output sample is the engine signal at its exact position, rebuilt from RS_TAPS
 * engine samples around it with a Kaiser-windowed sinc (beta RS_BETA). The kernel is tabulated at RS_PHASES points
 * per sample and interpolated linearly between them, so any rate works (no fixed ratio). Its cutoff sits at the lower
 * of the two Nyquist frequencies. From 44.1 kHz up (desktop/resample_test.c measures it at 48 to 192 kHz): flat to
 * 20 kHz within 0.0001 dB, 20 kHz's image (24.1 kHz) 103 dB down, and a sine anywhere up to 20 kHz comes out with
 * everything else 99 dB below it. Below 44.1 kHz the kernel is stretched to the host's band (flat to 0.4 x the host
 * rate). 44.1 kHz is a plain copy.
 *
 * Positions are exact: output sample h (counted from the last reset) is the engine signal at engine frame
 * (h - delay) * 44100 / fs, kept as a whole frame plus a remainder in 1/fs steps, so the pitch never drifts. `delay`
 * (host samples, reported to the host as latency) is the kernel's half width, so the output never needs engine frames
 * from after its own moment: engine frame e is heard at host sample e * fs / 44100 + delay.
 * Everything is in the struct (no allocation); rs_reset() and rs_out() never lock or allocate. */
#pragma once
#include <stdint.h>

#define RS_RATE 44100      /* the engine's rate */
#ifndef RS_TAPS
#define RS_TAPS 72         /* kernel length, in samples of the lower rate */
#endif
#ifndef RS_PHASES
#define RS_PHASES 1024     /* kernel table points per sample */
#endif
#ifndef RS_BETA
#define RS_BETA 9.6        /* Kaiser window shape: the stopband about 100 dB down */
#endif
#define RS_RING 1024       /* engine frames of history (a power of two, more than the widest kernel) */
#define RS_MIN_RATE 8000   /* host rates the converter takes */
#define RS_MAX_RATE 768000

typedef struct {
    int fs;              /* host rate, Hz */
    int pass;            /* fs == 44100: a copy, no filter, no delay */
    int half;            /* engine frames used either side of the position */
    int delay;           /* latency in host samples */
    double scale;        /* kernel stretch, in engine samples: 1 from 44.1 kHz up, 44100 / fs below */
    int64_t pos;         /* the next output sample's position: engine frame pos ... */
    int64_t rem;         /* ... plus rem / fs (0 <= rem < fs) */
    int64_t frames;      /* engine frames written since the reset (the next one written is frame `frames`) */
    float ring[4 * RS_RING];   /* L R interleaved, written twice (slot and slot + RS_RING) so a read never wraps */
} resampler_t;

void rs_init(void);                         /* the kernel table, once (call before rs_reset, off the audio thread) */
int rs_rate_ok(int fs);                     /* a host rate the converter takes */
int rs_delay_for(int fs);                   /* the latency rs_reset(fs) will have, in host samples */
void rs_reset(resampler_t *r, int fs);      /* a new rate, or a restart: history silent, counters at 0 */
int64_t rs_need(const resampler_t *r);      /* engine frames that must be written before the next output sample */
void rs_write(resampler_t *r, const float *lr, int frames);   /* append engine frames (interleaved L R) */
/* up to n output samples into l / rt (added to what is there when add is set); stops early when it needs more engine
 * frames (rs_need() > frames written). Returns how many it made. */
int rs_out(resampler_t *r, float *l, float *rt, int n, int add);
