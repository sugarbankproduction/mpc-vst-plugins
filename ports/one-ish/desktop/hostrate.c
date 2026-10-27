/* Any host sample rate and float output for the desktop plugin: see hostrate.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "hostrate.h"
#include "resample.h"

#define BLOCK 128   /* the wrapper's DSP_BLOCK: it renders the engine in blocks of this, and so do we */
#define EVQ 512     /* host MIDI events waiting for their engine block */

void one_ish_render_float(void *inst, float *out_lr, int frames);   /* src/engine.c */

struct hostrate {
    const mpc_engine_t *api;   /* the engine's own table (src/engine.c) */
    void *eng;                 /* its instance */
    float tap[2 * BLOCK];      /* the float samples of the engine's last render(), for hr_process() */
    int tap_n;
    int64_t sp_num, sp_den;    /* the next song_pos(): its engine block starts sp_num / sp_den s after the host block */
    int legacy;
    int pending, pending_fs;   /* a reset asked for (hr_set_rate, hr_restart), and the rate */
    int64_t host_pos;          /* host samples since the reset (the current block's first) */
    struct { int64_t t; uint8_t m[3]; } ev[EVQ];   /* queued MIDI, in time order: t = engine frame */
    int nev;
    float out[2][BLOCK];       /* the wrapper's own (int16-resolution) output, not used unless the tap is missing */
    resampler_t rs;
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
    one_ish_render_float(h->eng, h->tap, frames);
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

/* ---- control (any thread) ---- */
int hr_on(const hostrate_t *h) { return !h->legacy; }
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
    h->host_pos = 0;
}

/* one engine block through the wrapper (its housekeeping, MIDI, song position, engine lock), into the converter */
static void engine_block(hostrate_t *h, AEffect *fx, hr_render_t render) {
    int64_t f = h->rs.frames;
    send_due(h, fx, f + BLOCK / 2);
    h->sp_num = f * h->rs.fs - h->host_pos * RS_RATE;   /* this block's start minus the host block's, in seconds ... */
    h->sp_den = (int64_t)RS_RATE * h->rs.fs;             /* ... times 44100 * fs */
    h->tap_n = 0;
    float *o[2] = {h->out[0], h->out[1]};
    render(fx, NULL, o, BLOCK);   /* the wrapper's FIFO is empty at every call (whole blocks only): one render() */
    if (h->tap_n != BLOCK)        /* never expected; the wrapper's own samples rather than a gap */
        for (int i = 0; i < BLOCK; i++) { h->tap[2 * i] = h->out[0][i]; h->tap[2 * i + 1] = h->out[1][i]; }
    rs_write(&h->rs, h->tap, BLOCK);
}

void hr_process(hostrate_t *h, AEffect *fx, hr_render_t render, float **out, int32_t n, int add) {
    if (__atomic_exchange_n(&h->pending, 0, __ATOMIC_ACQUIRE)) restart(h, fx);
    int done = 0;
    while (done < n) {
        done += rs_out(&h->rs, out[0] + done, out[1] + done, n - done, add);
        if (done < n) engine_block(h, fx, render);
    }
    h->host_pos += n;
}
