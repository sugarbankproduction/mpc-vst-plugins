/* Shared between the desktop editor's two halves: editor.c (layout, input, MIDI from the window, the VST2 side) and
 * editor_draw.c (drawing). See editor.c for the overview. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include "params.h"
#include "layout.h"
#include "patches.h"
#include "seq_page.h"

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

enum { effSetProgram = 2, effGetProgram = 3, effGetParamLabel = 6, effGetParamDisplay = 7, effSetSampleRate = 10,
       effMainsChanged = 12, effEditGetRect = 13,
       effEditOpen = 14, effEditClose = 15, effEditIdle = 19, effProcessEvents = 25, effGetProgramNameIndexed = 29 };
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42, audioMasterBeginEdit = 43, audioMasterEndEdit = 44 };
#define effFlagsHasEditor 1

AEffect *one_ish_wrapped_main(audioMasterCallback host);   /* wrapper/vst2_wrap.c's VSTPluginMain, renamed */

/* ---- the panel, scaled to fit the screen (layout.h is in base units) ---- */
/* what the centre screen shows: the mod matrix, the list of MORE pages, the randomiser, or one MORE page (0..NPAGES-1) */
enum { SCR_MATRIX = -1, SCR_INDEX = -2, SCR_RANDOM = -3 };
enum { ROWS = 8, SLOTS = 2 * ROWS, KEYS_WHITE = 36, KEY_BASE = 36 };   /* 61 keys, C2..C7 at octave 0 */
enum { Z_KEYS = 100000, Z_PITCH, Z_MODW, Z_PAD, Z_OCT_DOWN, Z_OCT_UP, Z_PREV, Z_NEXT, Z_NAME, Z_ENCODER,
       Z_TAB_MATRIX, Z_TAB_MORE, Z_TAB_ANALOG, Z_SUB_PREV, Z_SUB_NEXT, Z_RIBBON, Z_TAB_RANDOM,
       Z_RND_GO, Z_RND_MUTATE, Z_RND_AMOUNT, Z_RND_UNDO, Z_RND_REDO, Z_EUCLID, Z_TAB_SEQ, Z_RND_LOCK = 100060,   /* Z_RND_LOCK + section */
       Z_PAGE = 100100 };   /* Z_PAGE + p */
enum { RIBBON_CC = 16, RIBBON_TOUCH_CC = 17 };   /* the ribbon's MIDI: position 0..127; touch 127, release 0 */
enum { VENDOR_SPECIFIC = 50 };                   /* effVendorSpecific */
#define TEST_MIDI_LOG 0x4F314D4C   /* VENDOR_SPECIFIC index: copy the MIDI the window passed to the engine (tests) */
extern const short MXC[MX_COLS + 2];   /* the matrix columns: #, then each MX_ column, in base units from the left */

typedef struct {
    int ready, w, h;
    float s;                                  /* scale: base units to pixels */
    RECT ctl[NPARAMS];
    int kind[NPARAMS], size[NPARAMS];
    const char *label[NPARAMS];
    const char *const *leds[NPARAMS];
    RECT screen, name, prev, next, tabline, head, row[ROWS], help, sub_prev, sub_next;
    RECT tab_matrix, tab_more, tab_analog, tab_random, tab_seq, encoder, bprev, bnext, brand;
    RECT seq_leds;                            /* the SEQUENCER module's page LEDs (seq_page.c) */
    RECT oct_leds, oct_down, oct_up, pitch, modw, pad, keys, ribbon, euclid;   /* euclid: GENERATIVE's ring */
    RECT more[NMORES + 1];                    /* the modules' MORE triangles (MORES) */
} layout_t;
extern layout_t L;

static inline int sc(float v) { return (int)lroundf(v * L.s); }
static inline RECT zone(const short z[4]) { RECT r = {sc(z[0]), sc(z[1]), sc(z[0] + z[2]), sc(z[1] + z[3])}; return r; }
static inline int rw(const RECT *r) { return r->right - r->left; }
static inline int rh(const RECT *r) { return r->bottom - r->top; }

/* ---- per instance ---- */
#define MQ 256   /* UI MIDI queue: a power of two */
#define RND_HIST 32   /* sounds the randomiser's UNDO / REDO keep (the oldest goes) */
enum { MLOG = 64 };
typedef struct {
    AEffect *fx;
    audioMasterCallback host;
    HWND wnd;
    int hover, drag, drag_y, drag_x, drag_horiz, mode, screen, sub;
    int hit_screen;                            /* the last hit() found its param on the screen, at hit_rect */
    RECT hit_rect;
    float drag_from;
    float shown[NPARAMS];
    int shown_prog;
    int oct, key, enc_y, enc_moved;            /* octave shift -2..2; the key held with the mouse (-1: none) */
    float bend, wheel, pad_x, pad_y, ribbon;   /* -1..1, 0..1, 0..1, 0..1, 0..1 */
    int pad_on, ribbon_on, ribbon_cc;          /* ribbon_cc: the position last sent (-1: none since the touch) */
    unsigned char mq[MQ][3];                   /* UI -> audio MIDI, single producer / single consumer */
    unsigned mq_w, mq_r;
    unsigned char mlog[MLOG][3];               /* the last MIDI the audio thread passed on, for load_test */
    unsigned mlog_n;
    /* the randomiser page (src/randomize.h): mutate amount 0..1, locked sections (bit per RS_*), its own random
     * stream, and a history of sounds for UNDO / REDO (hist_pos: the one playing now) */
    float rnd_amount;
    unsigned rnd_locks;
    uint32_t rnd_seed;
    int rnd_count, mut_count, hist_n, hist_pos;
    float hist[RND_HIST][NPARAMS];
    char hist_name[RND_HIST][40];
    patches_t pt;                              /* user patches and the browser (patches.c) */
    struct hostrate *hr;                       /* the host's rate and float output (hostrate.c); NULL: none */
    seq_ui_t sq;                               /* the step sequencer's page (seq_page.c) */
} ed_t;
enum { M_NONE, M_PARAM, M_KEY, M_PITCH, M_MODW, M_PAD, M_ENC, M_RIBBON, M_RND_AMOUNT, M_SEQ };

#define MAX_INSTANCES 32
extern HFONT font_title, font_label, font_value, font_tiny, font_screen, font_cell, font_big, font_brand;

/* editor.c */
void param_text(ed_t *e, int i, char *out, int n);
int option_of(ed_t *e, int i);
float real_of(ed_t *e, int i);
int page_items(ed_t *e, int p, int *items);   /* the items of page p shown now (ITEMS indexes); how many */
int screen_subs(ed_t *e);                     /* how many sub-pages the screen has now (1 or more) */
void slot_rects(int slot, RECT *name, RECT *ctl);   /* a page's slot 0..SLOTS-1: its name and its control */
RECT matrix_cell(int r, int col);             /* the matrix on the screen: row r, column 0 (#) .. MX_COLS */
RECT rnd_rect(int z);                         /* the randomiser page: where control z (Z_RND_*) sits */
static inline int note_of_white(int j, int base) {   /* white key j of the keyboard, from note `base` (a C) */
    static const int s[] = {0, 2, 4, 5, 7, 9, 11};
    return base + 12 * (j / 7) + s[j % 7];
}
/* editor_draw.c */
void paint(ed_t *e, HDC out);
void leds_geom(int i, int *x0, int *y0, int *row, RECT *button);
/* editor.c: the wrapper's own dispatcher (no editor hooks), for patches.c */
intptr_t wrapped_call(AEffect *fx, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt);
/* patches.c: user patches and the PATCHES page (patches.h) */
int patch_name_ok(const char *s, char *why, int n);
int patch_dir(char *out, int n, int create);
void patches_init(ed_t *e);
void patches_free(ed_t *e);
void patches_rescan(ed_t *e);
int patches_tick(ed_t *e);
int patches_edited_now(ed_t *e);
int patches_total(ed_t *e);
int patches_pos(ed_t *e);
int patches_load_pos(ed_t *e, int pos);
int patches_load_user(ed_t *e, const char *name);
void patches_step(ed_t *e, int d);
int patches_save(ed_t *e);
int patches_save_as(ed_t *e, const char *name, int overwrite);
int patches_rename(ed_t *e, const char *to, int overwrite);
int patches_delete(ed_t *e);
int patches_import(ed_t *e, const char *src, int overwrite);
void patches_import_name(const char *src, char *out);
int patches_export(ed_t *e, const char *dst);
void patches_title(ed_t *e, char *out, int n);
void patches_open(ed_t *e);
void patches_toggle(ed_t *e);
int patches_subs(ed_t *e);
int patches_hit(ed_t *e, POINT pt);
int patches_press(ed_t *e, int z);
void patches_draw(HDC dc, ed_t *e);
void patches_help(ed_t *e, char *out, int n);
const char *patches_message(ed_t *e);
const char *patches_zone_help(int z);
void patches_program_set(ed_t *e, int before);
intptr_t patches_get_chunk(ed_t *e, void *ptr);
void patches_chunk_set(ed_t *e, const void *chunk, intptr_t len);
intptr_t patches_test(ed_t *e, intptr_t op, char *buf, float opt);
/* seq_page.c: the step sequencer's page (seq_page.h) */
void seq_init(ed_t *e);
int seq_read(ed_t *e);
int seq_tick(ed_t *e, int params_changed);
void seq_open(ed_t *e, int page);
int seq_zone(int z);
int seq_hit(ed_t *e, POINT pt);
int seq_hit_panel(ed_t *e, POINT pt);
int seq_press(ed_t *e, int z, int x, int y, int shift);
void seq_drag(ed_t *e, int x, int y, int shift);
void seq_release(ed_t *e);
void seq_right(ed_t *e, int z);
void seq_double(ed_t *e, int z);
int seq_wheel(ed_t *e, int z, int dir, int shift);
int seq_key(ed_t *e, int vk, int shift);
void seq_draw(HDC dc, ed_t *e);
void seq_panel_draw(HDC dc, ed_t *e);
const char *seq_zone_help(ed_t *e, int z);
void seq_help(ed_t *e, char *out, int n);
const char *seq_message(ed_t *e);
RECT seq_help_rect(void);
intptr_t seq_test(ed_t *e, intptr_t op, char *buf);
