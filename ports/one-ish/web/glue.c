/* The web build's way into the engine (web/build.sh): the browser's sound thread (site/worklet.js) calls these.
 * One engine, rendered 128 frames at a time at 44.1 kHz (the page asks the browser for a 44.1 kHz AudioContext, so no
 * rate converter is needed), float stereo, no host clock (the engine runs free on its own tempo). Factory presets come
 * from the generated params.h, loaded as the VST wrapper does (wrapper/vst2_wrap.c set_program): every listed value,
 * in file order. */
#include <stdint.h>
#include <emscripten/emscripten.h>
#include "engine.h"
#include "params.h"

void one_ish_render_float(void *inst, float *out_lr, int frames);   /* src/synths.h */

#define BLOCK 128
static void *g_inst;
static float g_out[2 * BLOCK];
static char g_buf[256];

EMSCRIPTEN_KEEPALIVE int mw_create(void) {
    if (!g_inst) g_inst = mpc_engine()->create(NULL);
    return g_inst != NULL;
}

EMSCRIPTEN_KEEPALIVE float *mw_out(void) { return g_out; }   /* interleaved L R, BLOCK frames */

EMSCRIPTEN_KEEPALIVE void mw_render(void) {
    mpc_engine()->song_pos(g_inst, -1.0, 0.0, 0, BLOCK);
    one_ish_render_float(g_inst, g_out, BLOCK);
}

EMSCRIPTEN_KEEPALIVE void mw_midi(int status, int d1, int d2) {
    uint8_t m[3] = {(uint8_t)status, (uint8_t)d1, (uint8_t)d2};
    int hi = status & 0xF0;
    mpc_engine()->midi(g_inst, m, hi == 0xC0 || hi == 0xD0 ? 2 : 3);
}

EMSCRIPTEN_KEEPALIVE int mw_presets(void) { return NPRESETS; }

EMSCRIPTEN_KEEPALIVE const char *mw_preset_name(int i) { return i >= 0 && i < NPRESETS ? PRESETS[i].name : ""; }

EMSCRIPTEN_KEEPALIVE void mw_preset(int i) {
    if (i < 0 || i >= NPRESETS) return;
    for (int k = 0; k < PRESETS[i].n; k++)
        mpc_engine()->set_param(g_inst, PARAMS[PRESETS[i].values[k].param].key, PRESETS[i].values[k].value);
}

/* a parameter by key (for the page's few knobs): set it, or read its display text */
EMSCRIPTEN_KEEPALIVE char *mw_buf(void) { return g_buf; }
EMSCRIPTEN_KEEPALIVE void mw_set(void) {   /* g_buf holds "key\0value" */
    const char *val = g_buf;
    while (*val) val++;
    mpc_engine()->set_param(g_inst, g_buf, val + 1);
}
