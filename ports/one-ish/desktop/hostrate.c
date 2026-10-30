/* Any host sample rate and float output for the desktop plugin: see hostrate.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "hostrate.h"
#include "resample.h"

#define BLOCK 128   /* the wrapper's DSP_BLOCK: it renders the engine in blocks of this, and so do we */
#define EVQ 512     /* host MIDI events waiting for their engine block */
#define IN_FIFO 16384   /* the audio input at 44.1 kHz, waiting for its engine block (a power of two, frames) */
#define IN_CHUNK 256    /* host input written into the converter at a time (its ring holds 1024 frames) */
#define HR_CHUNK 512    /* (2026-10-30) host frames converted at a time (the scratch for a missing output pair) */

void one_ish_render_float(void *inst, float *out_lr, int frames);     /* src/engine.c */
void one_ish_render_multi(void *inst, float *out_lr, float *const *syn_lr, int frames);   /* (2026-10-30) */
void one_ish_set_input(void *inst, const float *in_lr, int frames);   /* src/engine.c */

struct hostrate {
    const mpc_engine_t *api;   /* the engine's own table (src/engine.c) */
    void *eng;                 /* its instance */
    float tap[2 * BLOCK];      /* the float samples of the engine's last render(), for hr_process() */
    float syn_tap[HR_PAIRS][2 * BLOCK];   /* (2026-10-30) each Synth's own pair from the same render (multi-out) */
    int tap_n;
    int64_t sp_num, sp_den;    /* the next song_pos(): its engine block starts sp_num / sp_den s after the host block */
    int legacy;
    int pending, pending_fs;   /* a reset asked for (hr_set_rate, hr_restart), and the rate */
    int64_t host_pos;          /* host samples since the reset (the current block's first) */
    struct { int64_t t; uint8_t m[3]; } ev[EVQ];   /* queued MIDI, in time order: t = engine frame */
    int nev;
    float out[2][BLOCK];       /* the wrapper's own (int16-resolution) output, not used unless the tap is missing */
    resampler_t rs;
    resampler_t rs_syn[HR_PAIRS];   /* (2026-10-30) each Synth's pair, converted in step with MAIN (same rate, same frames) */
    float scratch[2][HR_CHUNK];     /* a pair the host gave no buffers for: converted into here, so it keeps in step */
    /* the audio input (hostrate.h): the host's rate to 44.1 kHz, then a FIFO indexed by engine frame */
    resampler_t in_rs;
    float in_fifo[2 * IN_FIFO];   /* engine frame e at (e & (IN_FIFO - 1)); the first in_lead frames are silence */
    int64_t in_w, in_r;           /* the next engine frame converted (written), the next one an engine block reads */
    int in_lead;                  /* the frames of silence the input starts behind (in_lead_for): it never runs dry */
    float in_blk[2 * BLOCK];      /* the next engine block's input, for shim_render() */
    int in_ready, in_live;        /* in_blk is this block's; it holds a sample other than 0 */
    long in_dry;                  /* engine frames that found no input yet (never expected; load_test reads it) */
};

/* ---- the engine table the wrapper drives: everything forwarded, render() also keeps the float samples ---- */
static __thread hostrate_t *last_created;

static void *shim_create(const char *data_dir) {
    hostrate_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->api = mpc_engine();
    h->eng = h->api ? h->api->create(data_dir) : NULL;
    if (!h->eng) { free(h); return NULL; }
    rs_init();
    rs_reset(&h->rs, RS_RATE);
    for (int c = 0; c < HR_PAIRS; c++) rs_reset(&h->rs_syn[c], RS_RATE);
    rs_reset_io(&h->in_rs, RS_RATE, RS_RATE);
    memset(h->in_fifo, 0, sizeof h->in_fifo);   /* (calloc'd: written once, so the audio thread never maps its pages) */
    h->in_lead = h->in_w = BLOCK;
    h->pending_fs = RS_RATE;
    last_created = h;
    return h;
}
static void shim_destroy(void *inst) {
    hostrate_t *h = inst;
    h->api->destroy(h->eng);
    free(h);
}
static void shim_midi(void *inst, const uint8_t *m, int len) { hostrate_t *h = inst; h->api->midi(h->eng, m, len); }
static void shim_set_param(void *inst, const char *k, const char *v) { hostrate_t *h = inst; h->api->set_param(h->eng, k, v); }
static int shim_get_param(void *inst, const char *k, char *buf, int n) {
    hostrate_t *h = inst;
    return h->api->get_param(h->eng, k, buf, n);
}
static void shim_render(void *inst, int16_t *out, int frames) {
    hostrate_t *h = inst;
    if (frames > BLOCK) frames = BLOCK;   /* (the wrapper never asks for more) */
    if (h->in_ready) {   /* this engine block's audio input (a silent one is not sent: the engine then hears silence) */
        if (h->in_live) one_ish_set_input(h->eng, h->in_blk, frames);
        h->in_ready = 0;
    }
    float *const pairs[HR_PAIRS] = {h->syn_tap[0], h->syn_tap[1], h->syn_tap[2]};
    one_ish_render_multi(h->eng, h->tap, pairs, frames);   /* MAIN as one_ish_render_float(), and the Synths' pairs */
    for (int i = 0; i < 2 * frames; i++) out[i] = (int16_t)(h->tap[i] * 32767.0f);   /* exactly the engine's render() */
    h->tap_n = frames;
}
static void shim_song_pos(void *inst, double ppq, double bpm, int playing, int frames) {
    hostrate_t *h = inst;
    if (ppq >= 0.0 && bpm > 0.0 && h->sp_den) {   /* the host's position at its block start, moved to this engine block */
        double at = ppq + (double)h->sp_num / h->sp_den * bpm / 60.0;
        ppq = at > 0.0 ? at : 0.0;   /* (a fraction of a sample before 0 at the start: -1 would mean "no position") */
    }
    h->api->song_pos(h->eng, ppq, bpm, playing, frames);
}
static const mpc_engine_t SHIM = {shim_create, shim_destroy, shim_midi, shim_set_param, shim_get_param, shim_render,
                                  NULL, shim_song_pos};

const mpc_engine_t *one_ish_desktop_engine(void) {
    const mpc_engine_t *api = mpc_engine();
    return api && api->song_pos && !api->process ? &SHIM : NULL;
}
int one_ish_desktop_changed(void *inst, char *key, int len) {
    hostrate_t *h = inst;
    return mpc_engine_changed ? mpc_engine_changed(h->eng, key, len) : 0;
}

hostrate_t *hr_last_created(void) {
    hostrate_t *h = last_created;
    last_created = NULL;
    return h;
}

/* the input's lead over the engine, in engine frames: a whole engine block (an engine block is rendered as soon as the
 * output needs its first frame, so it may want input up to 127 frames past the host block's end), and at other rates
 * two more for the converters' rounding (hostrate.h) */
static int in_lead_for(int fs) { return fs == RS_RATE || !rs_rate_ok(fs) ? BLOCK : BLOCK + 2; }

/* ---- control (any thread) ---- */
int hr_on(const hostrate_t *h) { return !h->legacy; }
double hr_input_delay(const hostrate_t *h) {
    if (h->legacy) return -1.0;
    int fs = __atomic_load_n(&h->pending_fs, __ATOMIC_RELAXED);
    if (!rs_rate_ok(fs)) fs = RS_RATE;
    return (in_lead_for(fs) + rs_delay_io(fs, RS_RATE)) * (double)fs / RS_RATE + rs_delay_for(fs);
}
long hr_input_dry(const hostrate_t *h) { return h->in_dry; }
int hr_peek_view(hostrate_t *h, int synth, char *buf, int n) {
    char key[16];
    if (synth) snprintf(key, sizeof key, "s%d_phys_view", synth + 1);
    else snprintf(key, sizeof key, "phys_view");
    return h && h->eng ? h->api->get_param(h->eng, key, buf, n) : 0;
}
int hr_peek(hostrate_t *h, const char *key, char *buf, int n) {   /* a readout (load_test: "bpm", 2026-10-30) */
    return h && h->eng ? h->api->get_param(h->eng, key, buf, n) : 0;
}
/* (2026-10-30) the same for another view the engine makes safe from any thread ("rain_view", "markov_view") */
int hr_peek_named(hostrate_t *h, int synth, const char *name, char *buf, int n) {
    char key[32];
    if (synth) snprintf(key, sizeof key, "s%d_%s", synth + 1, name);
    else snprintf(key, sizeof key, "%s", name);
    return h && h->eng ? h->api->get_param(h->eng, key, buf, n) : 0;
}
int hr_latency(const hostrate_t *h) { return h->legacy ? 0 : rs_delay_for(__atomic_load_n(&h->pending_fs, __ATOMIC_RELAXED)); }

int hr_set_rate(hostrate_t *h, float rate) {
    int fs = isfinite(rate) ? (int)lroundf(rate) : RS_RATE;
    if (!rs_rate_ok(fs)) fs = RS_RATE;
    __atomic_store_n(&h->pending_fs, fs, __ATOMIC_RELAXED);
    __atomic_store_n(&h->pending, 1, __ATOMIC_RELEASE);
    return hr_latency(h);
}
void hr_restart(hostrate_t *h) { __atomic_store_n(&h->pending, 1, __ATOMIC_RELEASE); }
void hr_set_legacy(hostrate_t *h, int on) { h->legacy = on != 0; }

/* ---- MIDI: queued with its engine frame, given to the wrapper before the engine block nearest it ---- */
static void send(AEffect *fx, const uint8_t (*m)[3], int n) {   /* through the wrapper, as a host's events */
    VstMidiEvent ev[64];
    struct { int32_t n; intptr_t reserved; VstMidiEvent *ev[64]; } list;
    while (n > 0) {
        int k = n < 64 ? n : 64;
        for (int i = 0; i < k; i++) {
            memset(&ev[i], 0, sizeof ev[i]);
            ev[i].type = 1;
            ev[i].byteSize = sizeof ev[i];
            memcpy(ev[i].midiData, m[i], 3);
            list.ev[i] = &ev[i];
        }
        list.n = k;
        list.reserved = 0;
        wrapped_call(fx, effProcessEvents, 0, 0, &list, 0);
        m += k;
        n -= k;
    }
}

static void send_due(hostrate_t *h, AEffect *fx, int64_t before) {   /* the queued events with t < before */
    uint8_t m[64][3];
    int k = 0;
    while (k < h->nev && h->ev[k].t < before) k++;
    for (int done = 0; done < k;) {
        int c = k - done < 64 ? k - done : 64;
        for (int i = 0; i < c; i++) memcpy(m[i], h->ev[done + i].m, 3);
        send(fx, (const uint8_t (*)[3])m, c);
        done += c;
    }
    memmove(h->ev, h->ev + k, sizeof h->ev[0] * (h->nev - k));
    h->nev -= k;
}

int hr_events(hostrate_t *h, AEffect *fx, const void *vst_events) {
    if (h->legacy) return 0;
    const struct { int32_t n; intptr_t reserved; VstMidiEvent *ev[]; } *list = vst_events;
    for (int i = 0; i < list->n; i++) {
        const VstMidiEvent *e = list->ev[i];
        if (e->type != 1) continue;   /* (the wrapper ignores anything but MIDI too) */
        if (h->nev == EVQ) send_due(h, fx, INT64_MAX);   /* full (no blocks processed): the oldest go now */
        int64_t d = e->deltaFrames > 0 ? e->deltaFrames : 0, fs = h->rs.fs;
        int64_t t = ((h->host_pos + d) * RS_RATE + fs / 2) / fs;   /* the engine frame on the same timeline spot */
        int k = h->nev++;
        while (k > 0 && h->ev[k - 1].t > t) { h->ev[k] = h->ev[k - 1]; k--; }   /* stable: after equal times */
        h->ev[k].t = t;
        memcpy(h->ev[k].m, e->midiData, 3);
    }
    return 1;
}

/* ---- the audio ---- */
static void restart(hostrate_t *h, AEffect *fx) {
    send_due(h, fx, INT64_MAX);   /* nothing held across a reset (a note-off must not be lost) */
    rs_reset(&h->rs, __atomic_load_n(&h->pending_fs, __ATOMIC_RELAXED));
    for (int c = 0; c < HR_PAIRS; c++) rs_reset(&h->rs_syn[c], h->rs.fs);
    rs_reset_io(&h->in_rs, h->rs.fs, RS_RATE);
    h->in_lead = in_lead_for(h->rs.fs);
    memset(h->in_fifo, 0, sizeof(float) * 2 * h->in_lead);   /* the lead: silence */
    h->in_w = h->in_lead;
    h->in_r = 0;
    h->in_ready = 0;
    h->host_pos = 0;
}

/* ---- the audio input: the host block's input converted to 44.1 kHz into the FIFO, all of it before any output is
 * written (so a host that processes in place, in == out, still works); missing channels, NaN and inf are silence ---- */
static void input_block(hostrate_t *h, float **in, int32_t n) {
    const int have = in && in[0] && in[1];
    float buf[2 * IN_CHUNK], l[64], r[64];
    for (int32_t at = 0; at < n;) {
        int k = n - at < IN_CHUNK ? (int)(n - at) : IN_CHUNK;
        for (int i = 0; i < k; i++) {
            float a = have ? in[0][at + i] : 0.0f, b = have ? in[1][at + i] : 0.0f;
            buf[2 * i] = isfinite(a) ? a : 0.0f;
            buf[2 * i + 1] = isfinite(b) ? b : 0.0f;
        }
        rs_write(&h->in_rs, buf, k);
        at += k;
        for (int m; (m = rs_out(&h->in_rs, l, r, 64, 0)) > 0;)
            for (int i = 0; i < m; i++, h->in_w++) {
                if (h->in_w < h->in_r || h->in_w - h->in_r >= IN_FIFO) continue;   /* (too late, or no room: dropped) */
                float *f = &h->in_fifo[2 * (h->in_w & (IN_FIFO - 1))];
                f[0] = l[i];
                f[1] = r[i];
            }
    }
}

/* the next engine block's 128 frames of input from the FIFO, into in_blk for shim_render() */
static void input_take(hostrate_t *h) {
    int live = 0;
    for (int i = 0; i < BLOCK; i++) {
        int64_t e = h->in_r + i;
        float a = 0.0f, b = 0.0f;
        if (e < h->in_w) {
            const float *f = &h->in_fifo[2 * (e & (IN_FIFO - 1))];
            a = f[0];
            b = f[1];
        } else {
            h->in_dry++;
        }
        h->in_blk[2 * i] = a;
        h->in_blk[2 * i + 1] = b;
        live |= a != 0.0f || b != 0.0f;
    }
    h->in_r += BLOCK;
    h->in_live = live;
    h->in_ready = 1;
}

/* one engine block through the wrapper (its housekeeping, MIDI, song position, engine lock), into the converter */
static void engine_block(hostrate_t *h, AEffect *fx, hr_render_t render) {
    int64_t f = h->rs.frames;
    send_due(h, fx, f + BLOCK / 2);
    h->sp_num = f * h->rs.fs - h->host_pos * RS_RATE;   /* this block's start minus the host block's, in seconds ... */
    h->sp_den = (int64_t)RS_RATE * h->rs.fs;             /* ... times 44100 * fs */
    h->tap_n = 0;
    input_take(h);
    float *o[2] = {h->out[0], h->out[1]};
    render(fx, NULL, o, BLOCK);   /* the wrapper's FIFO is empty at every call (whole blocks only): one render() */
    if (h->tap_n != BLOCK) {      /* never expected; the wrapper's own samples rather than a gap (on MAIN) */
        for (int i = 0; i < BLOCK; i++) { h->tap[2 * i] = h->out[0][i]; h->tap[2 * i + 1] = h->out[1][i]; }
        memset(h->syn_tap, 0, sizeof h->syn_tap);
    }
    rs_write(&h->rs, h->tap, BLOCK);
    for (int c = 0; c < HR_PAIRS; c++) rs_write(&h->rs_syn[c], h->syn_tap[c], BLOCK);
}

/* (2026-10-30) out[0], out[1]: MAIN; out[2 + 2c], out[3 + 2c] (when nout reaches them): Synth c's own pair. Every
 * pair's converter has the same rate and the same engine frames, so each gives as many samples as MAIN's; a pair the
 * host gave no buffers for is converted into the scratch, so it stays in step. A pair whose Synth is not on it is
 * silence, which the converter turns out without filtering (resample.h: quiet), so it costs next to nothing. */
void hr_process(hostrate_t *h, AEffect *fx, hr_render_t render, float **in, float **out, int nout, int32_t n,
                int add) {
    if (__atomic_exchange_n(&h->pending, 0, __ATOMIC_ACQUIRE)) restart(h, fx);
    input_block(h, in, n);
    int done = 0;
    while (done < n) {
        int want = n - done < HR_CHUNK ? (int)(n - done) : HR_CHUNK;
        int m = rs_out(&h->rs, out[0] + done, out[1] + done, want, add);
        for (int c = 0; c < HR_PAIRS && m > 0; c++) {
            int have = nout >= 4 + 2 * c && out[2 + 2 * c] && out[3 + 2 * c];
            float *l = have ? out[2 + 2 * c] + done : h->scratch[0], *r = have ? out[3 + 2 * c] + done : h->scratch[1];
            rs_out(&h->rs_syn[c], l, r, m, have && add);
        }
        done += m;
        if (done < n) engine_block(h, fx, render);
    }
    h->host_pos += n;
}
