/* The web build's plugin host: loads the same plugin as "Moo Won.dll" (the wrapper, the engine and the desktop
 * editor, compiled for the browser) and plays it, as Carla does on Windows.
 *
 * Threads (all share the module's memory):
 *   - the page (main browser thread, site/full.js): input, menus and dialogs, the canvas, MIDI keyboards, files;
 *   - the editor thread (here: editor_main): makes the plugin, opens its editor and runs the Win32 message loop
 *     (win32.c); every plugin call outside audio happens here;
 *   - the audio thread (audio_main): renders 128-frame blocks with processReplacing into a ring of float samples
 *     shared with the browser's sound thread (site/ring_worklet.js), which plays them and moves the read index on.
 * The plugin's own lock (wrapper/vst2_wrap.c) keeps the editor and audio threads out of each other's way, as on
 * Windows. The sound the user made is saved in the browser every few seconds and comes back on the next visit. */
#include <emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "win32_web.h"
#undef fopen

/* ---- the VST2 ABI, as wrapper/vst2_wrap.c writes it ---- */
typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
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
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
enum {
    effOpen = 0, effClose = 1, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effEditGetRect = 13,
    effEditOpen = 14, effEditClose = 15, effEditIdle = 19, effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25,
};
enum { audioMasterVersion = 1, audioMasterIdle = 3, audioMasterGetTime = 7, audioMasterGetSampleRate = 16,
       audioMasterGetBlockSize = 17, audioMasterGetVendorString = 32, audioMasterGetProductString = 33,
       audioMasterCanDo = 37, audioMasterUpdateDisplay = 42, audioMasterSizeWindow = 15 };
typedef struct { int16_t top, left, bottom, right; } ERect;
AEffect *VSTPluginMain(audioMasterCallback host);   /* desktop/editor.c */

#define BLOCK 128
#define RING 4096                   /* frames (a power of two) */
#define NMIDI 1024

/* what the page needs to know (site/full.js reads it at web_info(); byte offsets in the comments) */
typedef struct {
    int32_t ready;                  /* 0: 1 once the editor is open */
    pres_t *pres;                   /* 4 */
    float *ring;                    /* 8: RING frames, interleaved L R */
    int32_t ring_frames;            /* 12 */
    int32_t widx, ridx;             /* 16, 20: frames written / read so far (wrapping counters) */
    int32_t target;                 /* 24: frames to keep queued (the latency) */
    int32_t w, h;                   /* 28, 32: the panel's size in pixels */
    int32_t failed;                 /* 36: why it couldn't start (0: it did) */
    int32_t underruns;              /* 40: the sound thread counts blocks it had to leave silent */
    float render_ms;                /* 44: the audio thread's average time per block, for the page's CPU meter */
} info_t;
static info_t INFO;
static float RING_BUF[RING * 2];
static AEffect *fx;
static float rate = 44100.0f;

static uint32_t MIDIQ[NMIDI];       /* page -> audio thread: status | data1 << 8 | data2 << 16 */
static volatile int32_t midi_r, midi_w;

static intptr_t host_cb(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt) {
    switch (op) {
    case audioMasterVersion: return 2400;
    case audioMasterGetSampleRate: return (intptr_t)rate;
    case audioMasterGetBlockSize: return BLOCK;
    case audioMasterGetVendorString: if (ptr) strcpy(ptr, "Moo Won web"); return 1;
    case audioMasterGetProductString: if (ptr) strcpy(ptr, "Moo Won web"); return 1;
    case audioMasterUpdateDisplay: return 1;
    case audioMasterSizeWindow: return 1;
    case audioMasterGetTime: return 0;   /* no song position: the plugin's clock runs free */
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE void web_midi(int s, int d1, int d2) {   /* (the page) */
    int32_t w = __atomic_load_n(&midi_w, __ATOMIC_RELAXED), n = (w + 1) % NMIDI;
    if (n == __atomic_load_n(&midi_r, __ATOMIC_ACQUIRE)) return;   /* full: dropped */
    MIDIQ[w] = (uint32_t)(s & 0xFF) | (uint32_t)(d1 & 0x7F) << 8 | (uint32_t)(d2 & 0x7F) << 16;
    __atomic_store_n(&midi_w, n, __ATOMIC_RELEASE);
}

static void *audio_main(void *arg) {
    static float outl[BLOCK], outr[BLOCK], inl[BLOCK], inr[BLOCK];
    static float *outs[8], *ins[2] = {inl, inr};
    static float spare[6][BLOCK];
    outs[0] = outl;
    outs[1] = outr;
    for (int k = 2; k < 8; k++) outs[k] = spare[k - 2];
    static struct { int32_t n; intptr_t reserved; VstEvent *ev[64]; } evs;
    static VstMidiEvent mev[64];
    double avg = 0;
    for (;;) {
        /* (wrapping counters: unsigned, so the difference stays right across the wrap, after about 12 hours) */
        uint32_t w = (uint32_t)INFO.widx, r = (uint32_t)__atomic_load_n(&INFO.ridx, __ATOMIC_ACQUIRE);
        uint32_t target = (uint32_t)INFO.target;
        if (w - r >= target) {   /* enough queued: wait for the sound thread to take a block */
            emscripten_futex_wait((void *)&INFO.ridx, (uint32_t)r, 20.0);
            continue;
        }
        evs.n = 0;
        while (evs.n < 64 && midi_r != __atomic_load_n(&midi_w, __ATOMIC_ACQUIRE)) {
            uint32_t m = MIDIQ[midi_r];
            __atomic_store_n(&midi_r, (midi_r + 1) % NMIDI, __ATOMIC_RELEASE);
            VstMidiEvent *e = &mev[evs.n];
            memset(e, 0, sizeof *e);
            e->type = 1;   /* kVstMidiType */
            e->byteSize = sizeof *e;
            e->midiData[0] = m & 0xFF;
            e->midiData[1] = m >> 8 & 0x7F;
            e->midiData[2] = m >> 16 & 0x7F;
            evs.ev[evs.n++] = (VstEvent *)e;
        }
        double t0 = emscripten_get_now();
        if (evs.n) fx->dispatcher(fx, effProcessEvents, 0, 0, &evs, 0);
        fx->processReplacing(fx, ins, outs, BLOCK);
        avg += (emscripten_get_now() - t0 - avg) * 0.02;
        INFO.render_ms = (float)avg;
        float *dst = RING_BUF;
        for (int k = 0; k < BLOCK; k++) {
            uint32_t at = (w + (uint32_t)k) & (RING - 1);
            dst[2 * at] = outl[k];
            dst[2 * at + 1] = outr[k];
        }
        __atomic_store_n(&INFO.widx, (int32_t)(w + BLOCK), __ATOMIC_RELEASE);
    }
    return NULL;
}

/* ---- the sound the user made: saved in the browser, back on the next visit ---- */
static const char *STATE_FILE = "/Users/web/AppData/Roaming/Moo Won/web_state.bin";
static uint32_t state_hash;
static uint32_t fnv(const unsigned char *p, intptr_t n) {
    uint32_t h = 2166136261u;
    for (intptr_t k = 0; k < n; k++) h = (h ^ p[k]) * 16777619u;
    return h;
}
static void state_save(HWND w, UINT m, UINT_PTR id, DWORD t) {
    void *chunk = NULL;
    intptr_t n = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0);
    if (n <= 0 || !chunk) return;
    uint32_t h = fnv(chunk, n);
    if (h == state_hash) return;
    state_hash = h;
    mkdir("/Users/web/AppData/Roaming/Moo Won", 0777);
    char tmp[256];   /* written whole beside it, then put in its place: never a half-written sound */
    snprintf(tmp, sizeof tmp, "%s.new", STATE_FILE);
    FILE *f = fopen(tmp, "wb");
    if (!f) { state_hash = 0; return; }
    int ok = fwrite(chunk, 1, (size_t)n, f) == (size_t)n;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, STATE_FILE)) { remove(tmp); state_hash = 0; return; }
    web_files_changed();
}
static void state_load(void) {
    FILE *f = fopen(STATE_FILE, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        fx->dispatcher(fx, effSetChunk, 0, n, buf, 0);
        state_hash = fnv((unsigned char *)buf, n);
    }
    free(buf);
    fclose(f);
}

void web_files_changed(void) {
    MAIN_THREAD_ASYNC_EM_ASM({ if (Module.mwSync) Module.mwSync(); });
}

static void *editor_main(void *arg) {
    fx = VSTPluginMain(host_cb);
    if (!fx) { INFO.failed = 1; return NULL; }
    if (fx->numOutputs > 8) { INFO.failed = 4; return NULL; }   /* (audio_main has room for 8) */
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, rate);
    fx->dispatcher(fx, effSetBlockSize, 0, BLOCK, NULL, 0);
    state_load();
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0);
    pthread_t at;
    pthread_attr_t aa;
    pthread_attr_init(&aa);
    pthread_attr_setstacksize(&aa, 1 << 20);
    if (pthread_create(&at, &aa, audio_main, NULL)) { INFO.failed = 2; return NULL; }
    ERect *r = NULL;
    fx->dispatcher(fx, effEditGetRect, 0, 0, &r, 0);
    int w = r ? r->right - r->left : 1200, h = r ? r->bottom - r->top : 600;
    HWND root = win32_root(w, h);
    if (!root || !fx->dispatcher(fx, effEditOpen, 0, 0, root, 0)) { INFO.failed = 3; return NULL; }
    SetTimer(root, 0x5A5E, 3000, state_save);
    INFO.pres = win32_pres();
    INFO.w = w;
    INFO.h = h;
    __atomic_store_n(&INFO.ready, 1, __ATOMIC_RELEASE);
    win32_run();
    return NULL;
}

EMSCRIPTEN_KEEPALIVE info_t *web_info(void) { return &INFO; }

/* the page starts it once the saved files are back: the sound card's rate, the panel's scale, frames of latency */
EMSCRIPTEN_KEEPALIVE int web_start(float sample_rate, float scale, int latency) {
    static int started;
    if (started) return 0;
    started = 1;
    rate = sample_rate > 0 ? sample_rate : 44100.0f;
    INFO.ring = RING_BUF;
    INFO.ring_frames = RING;
    INFO.target = latency >= BLOCK && latency <= RING - BLOCK ? latency : 1024;
    char s[32];
    snprintf(s, sizeof s, "%.3f", scale);
    setenv("ONEISH_EDITOR_SCALE", s, 1);
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 8 << 20);   /* (the editor keeps some big buffers on its stack) */
    return pthread_create(&t, &a, editor_main, NULL) == 0;
}

int main(void) { return 0; }
