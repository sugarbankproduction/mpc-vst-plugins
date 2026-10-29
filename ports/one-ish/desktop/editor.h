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
#include "settings.h"

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
       Z_RND_GO, Z_RND_MUTATE, Z_RND_AMOUNT, Z_RND_UNDO, Z_RND_REDO, Z_EUCLID, Z_TAB_SEQ, Z_FOCUS1, Z_FOCUS2, Z_FOCUS3,
       Z_TAB_SETUP, Z_TAB_SETTINGS, Z_RND_ALL, Z_MUTE1, Z_MUTE2, Z_MUTE3, Z_SOLO1, Z_SOLO2, Z_SOLO3,
       Z_RND_WILD, Z_RND_ALSO,   /* Z_RND_ALSO + 0..2: the ARPEGGIATOR, GENERATIVE and SEQUENCER tick buttons */
       Z_RND_LOCK = 100060,   /* Z_RND_LOCK + section */
       Z_PAGE = 100100,     /* Z_PAGE + p */
       Z_ABS = 700000 };    /* the SETUP page's cells: Z_ABS + a param's own index (any Synth's: not the focus's) */
enum { SCR_SETUP = -11, NSYNTHS = 3 };   /* the centre screen's SETUP page (the three Synths side by side) */
/* (2026-10-30, hub.c) the GENERATIVE hub page, and the hit codes of it and of the console's third row */
enum { SCR_GEN = -21 };
enum { Z_HUB_ON = 670000,        /* + row: a generator's ON (HUB[row]) */
       Z_HUB_OPEN = 670100,      /* + row: its name: its MORE page */
       Z_HUB_ALLOFF = 670200, Z_TAB_GEN, Z_GEN_OFF, Z_TEMPO, Z_TEMPO_HOST, Z_TEMPO_OWN,
       Z_VOL_MASTER,             /* SYNTH OUTPUT's MASTER: its VOLUME knob is master_level while lit */
       Z_TEMPO_DOWN, Z_TEMPO_UP, /* the TEMPO readout's < and >: 1 BPM (Shift 0.1), OWN */
       Z_HUB_END = 670300 };
#define TEST_HUB 0x4F314842   /* VENDOR_SPECIFIC index: value = HT_ op (hub.c) */
enum { HT_ENGINE_BPM = 1,     /* the tempo the engine's Synths follow now (x 1000) */
       HT_OWN,                /* 1: TEMPO is OWN (RECEIVE MIDI CLOCK off) */
       HT_TEMPO,              /* the TEMPO setting (x 1000) */
       HT_RESTORE,            /* how many generators GEN OFF would bring back */
       HT_LINKED,             /* opt = a panel index: 1 when its LINK is lit */
       HT_VOL_MASTER,         /* 1: SYNTH OUTPUT's MASTER is lit */
       HT_SUB };              /* the screen's sub-page shown now */
enum { RIBBON_CC = 16, RIBBON_TOUCH_CC = 17 };   /* the ribbon's MIDI: position 0..127; touch 127, release 0 */
/* the X/Y pad's: X (left 0 .. right 127), Y (bottom 0 .. top 127), touch 127 / release 0; Y also sends aftertouch */
enum { PAD_X_CC = 2, PAD_Y_CC = 3, PAD_TOUCH_CC = 18 };
enum { VENDOR_SPECIFIC = 50 };                   /* effVendorSpecific */
#define TEST_MIDI_LOG 0x4F314D4C   /* VENDOR_SPECIFIC index: copy the MIDI the window passed to the engine (tests) */
#define TEST_CORNERS 0x4F31434E    /* VENDOR_SPECIFIC index: square_corners()'s result for this window (tests) */
#define TEST_FOCUS 0x4F314643      /* VENDOR_SPECIFIC index: value = the Synths in focus (bits), opt = the one shown;
                                    * value -1 only asks: returns focus + 4 * mask */
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
    RECT focus[NSYNTHS], tab_setup;           /* PANEL FOCUS: SYNTH 1-3, and the SETUP page's button */
    RECT compare, snapshot;                   /* COMPARE and SNAPSHOT, under PREV / NEXT (library.c) */
    RECT tab_settings;                        /* the SETTINGS page's button (settings.c) */
    RECT oct_leds, oct_down, oct_up, pitch, modw, pad, keys, ribbon, euclid;   /* euclid: GENERATIVE's ring */
    RECT more[NMORES + 1];                    /* the modules' MORE triangles (MORES) */
    RECT tab_gen, gen_off, tempo, tempo_host, tempo_own;   /* the console's third row (hub.c, 2026-10-30) */
    RECT vol_master;                          /* SYNTH OUTPUT's MASTER button (hub.c) */
} layout_t;
extern layout_t L;

/* a MORE page's slots: ROWS rows in two columns, less the rows its picture takes at the bottom (PIC_ROWS); item j of
 * page p sits in slot page_slot(p, j) of slot_rects (column by column) */
static inline int page_rows(int p) { return p >= 0 && p < NPAGES && PAGES[p].pic ? ROWS - PIC_ROWS : ROWS; }
static inline int page_slots(int p) { return 2 * page_rows(p); }
static inline int page_slot(int p, int j) {
    int k = j % page_slots(p), r = page_rows(p);
    return (k / r) * ROWS + k % r;
}

static inline int sc(float v) { return (int)lroundf(v * L.s); }
static inline RECT zone(const short z[4]) { RECT r = {sc(z[0]), sc(z[1]), sc(z[0] + z[2]), sc(z[1] + z[3])}; return r; }
static inline int rw(const RECT *r) { return r->right - r->left; }
static inline int rh(const RECT *r) { return r->bottom - r->top; }

/* ---- per instance ---- */
#define MQ 256   /* UI MIDI queue: a power of two */
enum { RATE_MAX = 8 };   /* rate LEDs (rate_leds.c) */
typedef struct {
    unsigned lit;              /* bit per RATES entry: lit now (as last painted) */
    double read_at;            /* when the rates were last read from the params (seconds) */
    float beats[RATE_MAX];     /* a cycle or step in beats (synced, or at a free BPM) ... */
    float bpm[RATE_MAX];       /* ... at this free BPM (0: the host tempo) */
    float hz[RATE_MAX];        /* or a free rate in Hz (beats 0) */
    unsigned char on[RATE_MAX];
    int seq_step;              /* the sequencer's step last seen running (-1: stopped) and when it changed */
    double seq_at;
} rate_ui_t;
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
    unsigned char qk_note[256], qk_down[128];  /* the computer keyboard: the note each key plays (0: none), held count */
    unsigned oct_t0;                           /* an OCTAVE button held since (GetTickCount) ... */
    int oct_stage;                             /* ... and what holding it has done: 1 the default octave, 2 panic */
    /* PANEL FOCUS (multitimbral): the panel, the MORE pages, the matrix and the step editor show `focus` (0-2) and edit
     * every Synth in focus_mask (bit per Synth; focus always in it). abs: the index in hand is a param's own (the SETUP
     * page's cells), not a panel index to map to the focus. hist_focus: the Synth the randomiser's history is of. */
    int focus, abs, hist_focus;
    unsigned focus_mask;
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
    int rnd_all;             /* ALL 3 SYNTHS: RANDOMISE and MUTATE change every Synth (Shift+R: once) */
    float rnd_wild;          /* WILD 0..1: how strange RANDOMISE and MUTATE get (0: the tame randomiser) */
    unsigned rnd_also;       /* the tick buttons: bit RS_ARP / RS_GEN / RS_SEQ, that section randomised too */
    float hist[RND_HIST][NPARAMS];
    char hist_name[RND_HIST][40];
    patches_t pt;                              /* user patches and the browser (patches.c) */
    struct hostrate *hr;                       /* the host's rate and float output (hostrate.c); NULL: none */
    int corners, corner_tries;                 /* square_corners(): 1 done, 0 not (yet), -1 failed; attempts */
    seq_ui_t sq;                               /* the step sequencer's page (seq_page.c) */
    rate_ui_t rl;                              /* the rate and tempo LEDs (rate_leds.c) */
    struct lib *lib;                           /* undo, compare, snapshots, tags, sets (library.c; NULL: none) */
    HWND idle;                                 /* a hidden window whose timer loads MIDI program changes */
    settings_ui_t st;                          /* the SETTINGS page (settings.c) */
    /* GEN OFF (hub.c): the generators it switched off (their own param indexes), to bring back; the program then */
    short gen_was[64];
    int gen_was_n, gen_was_prog;
    float tempo_from;                          /* a TEMPO drag: the BPM it started at */
    int vol_master;                            /* SYNTH OUTPUT's MASTER lit: VOLUME is master_level (never saved) */
} ed_t;
enum { M_NONE, M_PARAM, M_KEY, M_PITCH, M_MODW, M_PAD, M_ENC, M_RIBBON, M_RND_AMOUNT, M_SEQ, M_OCT, M_LIB, M_RND_WILD,
       M_TEMPO };   /* M_TEMPO: a drag on the TEMPO readout (hub.c) */

#define MAX_INSTANCES 32
extern HFONT font_title, font_label, font_value, font_tiny, font_screen, font_cell, font_big, font_brand;

/* editor.c */
int fp(ed_t *e, int i);          /* panel index i (Synth 1's key) -> the focused Synth's own index (shared params: i) */
int base_of(int i);              /* any index -> the panel's (Synth 1's key) */
int synth_of(int i);             /* the Synth an index belongs to (0-2; a shared param 0) */
int shared_param(int i);         /* one value for all three Synths (the master effects, tuning, settings) */
int synth_param(int i, int c);   /* panel index i -> Synth c's own index (shared: i) */
float ed_get(ed_t *e, int i);    /* the focused Synth's value of panel index i (normalized) */
int setup_rows(int sub, int *keys);           /* the SETUP page's rows on sub-page sub: panel indexes (-1: none) */
RECT setup_cell(int row, int col);           /* row 0..ROWS-1; col -1 the label, 0..2 a Synth, 3 a shared value */
extern const char *const SETUP_LABEL[];
void param_text(ed_t *e, int i, char *out, int n);
int option_of(ed_t *e, int i);
float real_of(ed_t *e, int i);
int page_items(ed_t *e, int p, int *items);   /* the items of page p shown now (ITEMS indexes); how many */
int screen_subs(ed_t *e);                     /* how many sub-pages the screen has now (1 or more) */
void slot_rects(int slot, RECT *name, RECT *ctl);   /* a page's slot 0..SLOTS-1: its name and its control */
RECT matrix_cell(int r, int col);             /* the matrix on the screen: row r, column 0 (#) .. MX_COLS */
RECT rnd_rect(int z);                         /* the randomiser page: where control z (Z_RND_*) sits */
RECT rnd_col(int col);                        /* the randomiser page: its three columns of buttons (0-2) */
RECT focus_part(int c, int part);             /* SYNTH c's button: its cap (0), M (1) and S (2) halves */
int synth_muted(ed_t *e, int c);              /* SYNTH c off (its ON) / soloed (never saved) */
int synth_soloed(ed_t *e, int c);
static inline int note_of_white(int j, int base) {   /* white key j of the keyboard, from note `base` (a C) */
    static const int s[] = {0, 2, 4, 5, 7, 9, 11};
    return base + 12 * (j / 7) + s[j % 7];
}
void ed_edit(ed_t *e, int i, float v);       /* a one-shot edit of panel index i (as a click: host told, undo) */
void ed_edit_abs(ed_t *e, int i, float v);   /* the same for a param's own index (any Synth's) */
void ed_show(ed_t *e, int screen, int sub);  /* the centre screen shows this page */
/* editor_draw.c */
void paint(ed_t *e, HDC out);
void leds_geom(int i, int *x0, int *y0, int *row, RECT *button);
/* more_pics.c: a MORE page's picture (an envelope, an LFO, the filters, the mixer) in rect a of the screen */
void pics_draw(HDC dc, ed_t *e, int page, RECT a);
void pics_tick(ed_t *e);   /* the rate timer: a moving picture (the balls' box) repaints, only its own area */
/* rate_leds.c: the LEDs that flash at the LFO, arpeggiator and sequencer rates and on the beat */
#define RATE_TIMER_MS 15
void rate_leds_tick(ed_t *e);                        /* the UI timer: what is lit now; repaints the LEDs that changed */
void rate_leds_draw(ed_t *e, const RECT *clip);      /* paint: the LEDs inside clip */
void rate_leds_static(HDC dc);                       /* the static layer: their labels */
RECT rate_led_rect(int k);                           /* RATES[k]'s LED, with its glow */
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
int patches_into_pos(ed_t *e, int pos);              /* a preset or patch into the focused Synth only */
int patches_save_synth(ed_t *e, const char *name, int overwrite);   /* the focused Synth alone as a patch */
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
int patches_zone_rect(ed_t *e, int z, RECT *r);     /* where the PATCHES page's zone z is (0: not shown) */
/* library.c: undo, COMPARE, snapshots, tags, performance sets (library.h has the rest) */
int lib_screen(int s);                         /* one of the library's pages (PATCHES included) */
int lib_hit(ed_t *e, POINT pt);
int lib_press(ed_t *e, int z);
void lib_release(ed_t *e);
int lib_subs(ed_t *e);
void lib_open(ed_t *e, int scr);
void lib_draw(HDC dc, ed_t *e);                /* the page under the patch name, with its tabs */
void lib_help(ed_t *e, char *out, int n);
const char *lib_zone_help(int z);
int lib_home_line(ed_t *e, char *out, int n);  /* the tags and tempo under the patch name (0: none) */
int lib_notes_help(ed_t *e, char *out, int n); /* SHOW NOTES (0: off) */
int lib_comparing(ed_t *e);
void lib_tick(ed_t *e);
void lib_idle(ed_t *e);
void lib_midi_events(ed_t *e, const void *events);   /* the host's MIDI (any thread): program and bank change */
intptr_t lib_test(ed_t *e, intptr_t op, char *buf, float opt);
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
void seq_load(ed_t *e, const sq_seq_t *q);   /* a whole sequence into the focused Synth's sequencer (the randomiser's lanes) */
int seq_random_notes(ed_t *e, int mutate, float amount, float wild, int key, int scale, int len, uint32_t *seed,
                     sq_seq_t *q);          /* the randomiser's SEQUENCER tick: new notes into q (1) or kept (0) */
const char *seq_message(ed_t *e);
RECT seq_help_rect(void);
intptr_t seq_test(ed_t *e, intptr_t op, char *buf);
/* editor.c: fn for every instance of this process (the settings are global) */
void ed_each(void (*fn)(ed_t *));
/* settings.c: the SETTINGS page and the settings file (settings.h) */
void settings_init(ed_t *e);
int settings_path(char *out, int n);
int settings_autoload(void);
int settings_bank_preview(void);               /* SHOW BANK PREVIEW (the SET page, library.c) */
int settings_cue_from(ed_t *e, int pos);       /* PRESET AUTOLOAD off: browsing points instead of loading */
int settings_cue_step(ed_t *e, int to);
int settings_cue_title(ed_t *e, char *out, int n);
int settings_cue_load(ed_t *e);
int settings_midi_keep(ed_t *e, float *keep);  /* MIDI I/O FROM PRESET off: a load keeps the channels (keep[4]) */
void settings_midi_seen(ed_t *e, const void *events);   /* the host's MIDI (its thread): the last CC, for LEARN */
int settings_learn_pick(ed_t *e, int i);       /* a control clicked while LEARN waits for one (its own index) */
void settings_tick(ed_t *e);                   /* the window's timer */
void settings_midi_put(ed_t *e, const float *keep);
void settings_open(ed_t *e);
void settings_title(ed_t *e, char *out, int n);
int settings_subs(ed_t *e);
int settings_hit(ed_t *e, POINT pt);
int settings_press(ed_t *e, int z);
int settings_wheel(ed_t *e, int z, int dir, int fine);
void settings_draw(HDC dc, ed_t *e);
const char *settings_zone_help(ed_t *e, int z);
void settings_help(ed_t *e, char *out, int n);
const char *settings_message(ed_t *e);
intptr_t settings_test(ed_t *e, intptr_t op, char *buf, float opt);
int settings_tempo(float *bpm);                /* the TEMPO setting into bpm; 1: OWN (RECEIVE MIDI CLOCK off) */
void settings_tempo_set(ed_t *e, int own, float bpm, int save);   /* both (save 0: while a drag goes on) */
/* hub.c (2026-10-30): the GENERATIVE hub page, GEN OFF, the console's TEMPO readout, the LINKs across the Synths */
int link_lit(ed_t *e, int i);                  /* panel index i has a LINK, lit: edits go to all three Synths */
int link_param(int i);                         /* the LINK param of panel index i (-1: none) */
int link_relative(ed_t *e, int i);             /* lit and relative (ALL 3: the cutoffs move by the same amount) */
int panel_source(ed_t *e, int i);              /* what panel control i moves now (VOLUME: master_level, MASTER lit) */
int master_param(void);                        /* master_level's index (-1: none) */
RECT hub_rect(int z);                          /* where hub zone z sits (the page's or the console's) */
RECT hub_link_rect(int row);                   /* the hub page's KEY (0) / SCALE (1) row: its LINK button */
RECT hub_value_rect(int row);                  /* ... and its value */
int hub_hit(ed_t *e, POINT pt);                /* the hub page's zones (and KEY, SCALE, their LINKs: params) */
int hub_hit_console(POINT pt);                 /* the console's third row */
int hub_press(ed_t *e, int z);                 /* 1: z was the hub's */
int hub_on(ed_t *e, int row, int c);           /* HUB[row] on in Synth c */
int hub_restore_n(ed_t *e);                    /* generators GEN OFF would bring back (0: it switches off) */
void hub_all_off(ed_t *e);                     /* GEN OFF / ALL OFF: every generator off in all 3 Synths, or back */
void tempo_text(ed_t *e, char *out, int n);    /* the readout: "120.0" */
int tempo_host_bpm(ed_t *e, double *bpm);      /* the host's tempo (1: it gives one) */
void tempo_press(ed_t *e, int y);              /* a click on the readout: a drag (M_TEMPO) starts */
void tempo_drag(ed_t *e, int y, int shift);    /* M_TEMPO */
void tempo_release(ed_t *e);
int tempo_wheel(ed_t *e, int z, int dir, int fine);   /* 1: used */
const char *hub_zone_help(ed_t *e, int z);
void hub_help(ed_t *e, char *out, int n);
intptr_t hub_test(ed_t *e, intptr_t op, float opt);
