/* Shared between the desktop editor's two halves: editor.c (layout, input, MIDI from the window, the VST2 side) and
 * editor_draw.c (drawing). See editor.c for the overview. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include "params.h"
#include "layout.h"

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
typedef intptr_t (*dispatcher_t)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
typedef void (*process_t)(AEffect *, float **, float **, int32_t);
struct AEffect {
    int32_t magic;
    dispatcher_t dispatcher;
    process_t process;
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    process_t processReplacing;
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct { int16_t top, left, bottom, right; } ERect;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;

enum { effSetProgram = 2, effGetProgram = 3, effGetParamLabel = 6, effGetParamDisplay = 7, effEditGetRect = 13,
       effEditOpen = 14, effEditClose = 15, effEditIdle = 19, effProcessEvents = 25, effGetProgramNameIndexed = 29 };
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42, audioMasterBeginEdit = 43, audioMasterEndEdit = 44 };
#define effFlagsHasEditor 1

AEffect *one_ish_wrapped_main(audioMasterCallback host);   /* wrapper/vst2_wrap.c's VSTPluginMain, renamed */

/* ---- the panel, scaled to fit the screen (layout.h is in base units) ---- */
enum { W_NONE, W_PANEL, W_MATRIX, W_MORE };
enum { ROWS = 8, MORE_PER_PAGE = 2 * ROWS, KEYS_WHITE = 36, KEY_BASE = 36 };   /* 61 keys, C2..C7 at octave 0 */
#define MORE_PAGES (NOTHER > 0 ? (NOTHER + MORE_PER_PAGE - 1) / MORE_PER_PAGE : 1)
enum { Z_KEYS = 100000, Z_PITCH, Z_MODW, Z_PAD, Z_OCT_DOWN, Z_OCT_UP, Z_PREV, Z_NEXT, Z_NAME, Z_ENCODER,
       Z_TAB_MATRIX, Z_TAB_MORE };

typedef struct {
    int ready, w, h;
    float s;                                  /* scale: base units to pixels */
    RECT ctl[NPARAMS];
    int kind[NPARAMS], size[NPARAMS], where[NPARAMS];
    const char *label[NPARAMS];
    const char *const *leds[NPARAMS];
    RECT screen, name, prev, next, tabline, head, row[ROWS], help;
    RECT tab_matrix, tab_more, encoder, bprev, bnext, brand;
    RECT oct_leds, oct_down, oct_up, pitch, modw, pad, keys;
} layout_t;
extern layout_t L;

static inline int sc(float v) { return (int)lroundf(v * L.s); }
static inline RECT zone(const short z[4]) { RECT r = {sc(z[0]), sc(z[1]), sc(z[0] + z[2]), sc(z[1] + z[3])}; return r; }
static inline int rw(const RECT *r) { return r->right - r->left; }
static inline int rh(const RECT *r) { return r->bottom - r->top; }

/* ---- per instance ---- */
#define MQ 256   /* UI MIDI queue: a power of two */
typedef struct {
    AEffect *fx;
    audioMasterCallback host;
    HWND wnd;
    int hover, drag, drag_y, drag_x, mode, page, more_page;
    float drag_from;
    float shown[NPARAMS];
    int shown_prog;
    int oct, key, enc_y, enc_moved;            /* octave shift -2..2; the key held with the mouse (-1: none) */
    float bend, wheel, pad_x, pad_y;           /* -1..1, 0..1, 0..1, 0..1 */
    int pad_on;
    unsigned char mq[MQ][3];                   /* UI -> audio MIDI, single producer / single consumer */
    unsigned mq_w, mq_r;
} ed_t;
enum { M_NONE, M_PARAM, M_KEY, M_PITCH, M_MODW, M_PAD, M_ENC };

#define MAX_INSTANCES 32
extern HFONT font_title, font_label, font_value, font_tiny, font_screen, font_big, font_brand;

/* editor.c */
void param_text(ed_t *e, int i, char *out, int n);
int option_of(ed_t *e, int i);
float real_of(ed_t *e, int i);
void more_rects(int slot, RECT *name, RECT *ctl);
static inline int note_of_white(int j, int base) {   /* white key j of the keyboard, from note `base` (a C) */
    static const int s[] = {0, 2, 4, 5, 7, 9, 11};
    return base + 12 * (j / 7) + s[j % 7];
}
/* editor_draw.c */
void paint(ed_t *e, HDC out);
void leds_geom(int i, int *x0, int *y0, int *row, RECT *button);
