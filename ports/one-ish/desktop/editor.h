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
#include "parts.h"

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
       Z_OCT_PICK = 100045,   /* (2026-10-30) a click on the octave LEDs: that octave */
       Z_PANIC,               /* (2026-10-30) PANIC: every note off (as holding an OCTAVE button 2 s) */
       Z_RND_GO_ALL = 100050, /* (2026-10-30) RANDOMISE's and MUTATE's ALL halves: all three Synths */
       Z_RND_MUT_ALL,
       Z_RND_VOICE,           /* + 0..2: the RANDOM page's BOTH, MONO, POLY (RV_*) */
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
       Z_TOGETHER,               /* (2026-10-30) lit: a SYNTH button adds / removes that Synth (as Ctrl-click) */
       Z_UNDO, Z_REDO,           /* (2026-10-30) the console's UNDO / REDO (as Ctrl+Z / Ctrl+Y) */
       /* (2026-10-30, hub_gens.c) the GENERATIVE page's RANDOMISE, MUTATE and RESET (each: the Synth in focus, and
        * its ALL half: all three Synths); SYNC over the filters' ALL 3 (both cutoffs of every Synth to the focus's) */
       Z_GEN_RND = 670250, Z_GEN_RND_ALL, Z_GEN_MUT, Z_GEN_MUT_ALL, Z_GEN_RESET, Z_GEN_RESET_ALL, Z_SYNC_CUT,
       Z_HUB_BPM_DOWN, Z_HUB_BPM_UP,   /* (2026-10-30) the hub page's GEN TEMPO < and > (the Synth's own BPM) */
       Z_HUB_END = 670300 };
#define TEST_HUB 0x4F314842   /* VENDOR_SPECIFIC index: value = HT_ op (hub.c) */
enum { HT_ENGINE_BPM = 1,     /* the tempo the engine's Synths follow now (x 1000) */
       HT_OWN,                /* 1: TEMPO is OWN (RECEIVE MIDI CLOCK off) */
       HT_TEMPO,              /* the TEMPO setting (x 1000) */
       HT_RESTORE,            /* how many generators GEN OFF would bring back */
       HT_LINKED,             /* opt = a panel index: 1 when its LINK is lit */
       HT_VOL_MASTER,         /* 1: SYNTH OUTPUT's MASTER is lit */
       HT_SUB,                /* the screen's sub-page shown now */
       HT_TOGETHER,           /* 1: TOGETHER is lit */
       HT_HELP,               /* the help line now, into ptr (1 KB); returns its length */
       HT_GEN_ARMED,          /* (2026-10-30) the RESET half waiting for its second click (a Z_GEN_RESET*), else 0 */
       HT_RND_VOICING,        /* (2026-10-30) the RANDOM page's MONO / POLY / BOTH (RV_*) */
       HT_PEEK };             /* (2026-10-30) an engine readout: ptr in, its key; out, its text (1 KB) */
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
    RECT together, undo, redo;                /* (2026-10-30) the third row's TOGETHER, UNDO, REDO (hub.c) */
    RECT sync_cut;                            /* (2026-10-30) SYNC over the filters' ALL 3 (hub_gens.c) */
    RECT qm_hands, kb_hint;                   /* the controller's DEST (quickmod.c); the computer keyboard's hint */
    RECT panic;                               /* the controller's PANIC */
    RECT back, fwd;                           /* (2026-10-30, nav.c) the screen's BACK / FWD, either side of the name */
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
/* QUICK ASSIGN (quickmod.c): a mod source picked on the panel, then the knobs it should move clicked and turned for
 * their amounts; the matrix rows they make are the MATRIX page's own params */
typedef struct {
    int src;                 /* the source picked (a matrix source option); 0: none */
    int dst, ctl;            /* while a knob is turned for its amount: its destination (MD_*) and panel index */
    int row[NSYNTHS];        /* each Synth in focus's matrix row for it (-1: none) */
    int drag_y, last_row, last_dst;   /* the connection made or turned last: a Ctrl-drag picks it again */
    float from;              /* the focused Synth's amount (%) when the drag began */
    int mark;                /* the MATRIX row "open in MATRIX" jumped to (-1: none) */
    int answer;              /* load_test: the menu choice taken instead of showing a menu (0: show it) */
    int back_screen, back_sub;   /* (2026-10-30) the page the screen showed before MOD FROM (SCR_QMOD) */
    char msg[220];           /* what the last action did, for the help line, and since when */
    DWORD msg_at;
    unsigned char drawn[NPARAMS];   /* a control drawn with a modulation ring (repainted when the matrix changes) */
} qm_ui_t;
/* (2026-10-30) the CABLES view (cables.c): the focused Synth's matrix rows drawn as patch cables over the panel */
enum { CB_MAX = 16, CB_PTS = 33, CB_JACKS = 48 };
typedef struct {
    short row, src, dst, ctl, on;    /* the matrix row, its source, destination (MD_*), the panel control (-1: a jack) */
    float x0, y0, x1, y1, kx, ky;    /* the ends (source, destination) and the amount knob, in pixels */
    float rad;                       /* the destination's ring (around its knob) */
    float pt[CB_PTS][2];             /* the cable as a line through these points */
    float amt;                       /* the amount drawn last (%), to repaint its knob when it changes */
} cb_cable_t;
typedef struct { short src, dst; float x, y; char label[28]; } cb_jack_t;   /* a source's jack (dst 0) or a destination's */
typedef struct {
    int on, sel, drag_y, rows[NSYNTHS];   /* shown; the cable selected (its row, -1); a drag on its knob: each Synth's row */
    float drag_from;
    unsigned sig, layer_sig;         /* what the cables show now / what the cached layer was drawn for */
    uint32_t *layer;                 /* the cables, cached (premultiplied ARGB, the window's size); NULL: none */
    int lw, lh, builds;              /* its size; how many times it was drawn (load_test) */
    int n, nj;
    cb_cable_t cab[CB_MAX];
    cb_jack_t jack[CB_JACKS];
    RECT label_at;                   /* the selected cable's label as drawn last (to repaint it) */
} cb_ui_t;
/* (2026-10-30, matrix_page.c) the MATRIX page's filters and the SETUP page's key range set by pressing a key */
typedef struct {
    int fsrc, fdst;          /* the MATRIX page shows only the rows with this source (or VIA) / destination; 0: any */
    int kr;                  /* the SETUP page waits for a key: 1 + 2 x Synth + (0 LOWEST, 1 HIGHEST); 0: not waiting */
    unsigned heard, heard0;  /* the host's MIDI (its thread writes): (note-ons heard << 8) | the last note; when armed */
    char msg[160];           /* what the last action did, for the help line, and since when */
    DWORD msg_at;
} mx_ui_t;
#define RND_HIST 32   /* sounds the randomiser's UNDO / REDO keep (the oldest goes) */
enum { MLOG = 64 };
/* (2026-10-30, nav.c) the centre screen's page history for BACK / FWD: a page and the sub-page it was left on */
enum { NAV_HIST = 16 };
typedef struct { short screen, sub; } nav_pos_t;
typedef struct {
    nav_pos_t back[NAV_HIST], fwd[NAV_HIST], cur;   /* back[nb - 1]: the page before; cur: the one shown now */
    int nb, nf, ready;
    int ov_synth;            /* the GENERATORS overview shows this Synth (-1: the one in focus) */
    unsigned ticks;          /* the overview's repaints asked for (nav_tick) */
} nav_ui_t;
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
    qm_ui_t qm;                                /* QUICK ASSIGN (quickmod.c) */
    /* GEN OFF (hub.c): the generators it switched off (their own param indexes), to bring back; the program then */
    short gen_was[64];
    int gen_was_n, gen_was_prog;
    float tempo_from;                          /* a TEMPO drag: the BPM it started at */
    int vol_master;                            /* SYNTH OUTPUT's MASTER lit: VOLUME is master_level (never saved) */
    int together;                              /* TOGETHER lit: SYNTH buttons add / remove Synths (never saved) */
    nav_ui_t nav;                              /* (2026-10-30) BACK / FWD, the overview's Synth (nav.c) */
    int rnd_voicing;                           /* (2026-10-30) the RANDOM page's BOTH / MONO / POLY (RV_*) */
    int no_link;                               /* (2026-10-30) the randomiser's edits: no LINK to the other Synths */
    int gens_arm;                              /* (2026-10-30) RESET (hub_gens.c): the half clicked once (0: none) */
    unsigned gens_arm_t;                       /* ... and when (GetTickCount): a second click within 4 s resets */
    cb_ui_t cb;                                /* (2026-10-30) the CABLES view (cables.c) */
    mx_ui_t mx;                                /* (2026-10-30) the MATRIX page's filters, KEY RANGE by a key (matrix_page.c) */
    parts_ui_t pa;                             /* (2026-10-30) effect / matrix presets, names, libraries (parts.c) */
} ed_t;
enum { M_NONE, M_PARAM, M_KEY, M_PITCH, M_MODW, M_PAD, M_ENC, M_RIBBON, M_RND_AMOUNT, M_SEQ, M_OCT, M_LIB, M_RND_WILD,
       M_QMOD, M_TEMPO,     /* M_TEMPO: a drag on the TEMPO readout (hub.c) */
       M_CABLE };           /* (2026-10-30) a drag on a cable's amount knob (cables.c) */

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
void ed_focus(ed_t *e, int c, int add);      /* the panel shows Synth c (add: into / out of the group edited together) */
/* editor_draw.c */
void paint(ed_t *e, HDC out);
void help_line(ed_t *e, char *out, int n);   /* the screen's help line now */
void leds_geom(int i, int *x0, int *y0, int *row, RECT *button);
void knob_geom(int i, float *cx, float *cy, float *r, int *label_y);   /* where a panel knob sits */
/* quickmod.c: QUICK ASSIGN. A DEST button (on the LFOs, the envelopes and NOISE), or Alt-click / right-click on a
 * source (also the wheels, the X/Y pad, the ribbon, the keyboard) picks a mod source; a click on a knob then connects
 * it in the focused Synths' matrix and the drag sets the amount. Rings on the knobs show what is modulated. */
#define Z_QM 660000        /* hit codes: a DEST button, Z_QM + its area */
#define Z_QM_END 660300    /* (DEST buttons Z_QM + 0..31; the MOD FROM page's buttons Z_QM + 100 and up) */
#define TEST_QM 0x4F31514D /* VENDOR_SPECIFIC index: load_test's quick assign operations (QMT_*, quickmod.c) */
enum { QMT_SOURCE = 1, QMT_ANSWER, QMT_DRAWN, QMT_DEST_XY, QMT_HELP, QMT_DST_OF,
       QMT_PAGE_XY,          /* (2026-10-30) the MOD FROM page's button Z_QM + opt: its middle in pixels (-1: none) */
       QMT_LINKS };          /* the picked source's connections in the focused Synth (-1: none picked) */
enum { SCR_QMOD = -31 };     /* the centre screen while a source is picked: MOD FROM <source> (quickmod.c) */
int qm_hit(ed_t *e, POINT pt);                  /* a DEST button under pt (Z_QM + area), or -1 */
int qm_press(ed_t *e, int i, int x, int y);     /* a left click (i: hit()'s); 1: quick assign took it */
void qm_drag(ed_t *e, int x, int y, int fine);  /* M_QMOD: the amount follows the mouse */
void qm_release(ed_t *e);
int qm_right(ed_t *e, int i, int x, int y);     /* a right click: 1 taken, 2 show the control's own options, 0 not */
int qm_key(ed_t *e, int vk);                    /* Esc: quick assign done (1: taken) */
void qm_tick(ed_t *e);                          /* the window's timer: an old message goes */
void qm_draw(HDC dc, ed_t *e, const RECT *clip);   /* the rings, the DEST buttons, the source picked */
int qm_value_text(ed_t *e, int i, char *out, int n);   /* the amount under the knob being turned (1) */
int qm_help(ed_t *e, char *out, int n);         /* the help line while quick assign is on (1) */
void qm_links_text(ed_t *e, int i, char *out, int n);   /* appended to a control's help: what modulates it */
const char *qm_zone_help(ed_t *e, int z);
int qm_marked(ed_t *e);                         /* the MATRIX row to mark (-1: none) */
void qm_matrix_changed(ed_t *e, int i);         /* param i (a panel index) changed: rings to repaint if it is the matrix's */
intptr_t qm_test(ed_t *e, intptr_t op, char *buf, float opt);
const char *qm_src_name(int s);                 /* (2026-10-30, for cables.c) a source's name (a matrix option) */
const char *qm_dst_name(int d);                 /* ... a destination's (MD_*) */
int qm_dst_of(int i);                           /* ... panel index i's destination (-1: none) */
unsigned qm_family(int s);                      /* ... the colour of its family (0xAARRGGBB) */
int qm_is_gen_src(int s);                       /* ... one of the generators' */
void qm_remove_row(ed_t *e, int row);           /* ... the focused Synth's row emptied (and in the Synths with it): undo */
void qm_pick(ed_t *e, int s);                   /* ... quick assign from source s (as its DEST button) */
int qm_subs(ed_t *e);                           /* the MOD FROM page's sub-pages */
void qm_page_title(ed_t *e, char *out, int n);  /* its title line */
void qm_page_draw(HDC dc, ed_t *e);             /* its connections and buttons, under the title line */
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
RECT seq_title_rect(int out);   /* (2026-10-30) the SEQUENCER title strip's PLAY ALL (0) and OUTPUT (1) */
int seq_title_param(int i);     /* i (a panel index) is one of them */
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
#define SETTINGS_MIDI_KEEP 16   /* (2026-10-30: the channels and MIDI OUT's settings) */
int settings_midi_keep(ed_t *e, float *keep);  /* MIDI I/O FROM PRESET off: a load keeps the channels (keep[SETTINGS_MIDI_KEEP]) */
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
RECT hub_follow_rect(void);                     /* (2026-10-30) the hub page's FOLLOW TEMPO button */
RECT hub_keys_rect(int k);                      /* (2026-10-30) generator k's KEYS button on the hub page */
RECT hub_gen_rect(int part);                    /* ... GEN TEMPO (0), its < (1), BPM (2), > (3); FOLLOW KEYS (4) */
void hub_follow_text(ed_t *e, char *out, int n);   /* ... and the text beside it (how much faster) */
int hub_hit(ed_t *e, POINT pt);                /* the hub page's zones (and KEY, SCALE, their LINKs: params) */
int hub_hit_console(POINT pt);                 /* the console's third row */
int hub_press(ed_t *e, int z);                 /* 1: z was the hub's */
int hub_on(ed_t *e, int row, int c);           /* HUB[row] on in Synth c */
int hub_restore_n(ed_t *e);                    /* generators GEN OFF would bring back (0: it switches off) */
void hub_all_off(ed_t *e);                     /* GEN OFF / ALL OFF: every generator off in all 3 Synths, or back */
void hub_panic_stop(ed_t *e);                  /* PANIC: every sequence stopped, every generator off (2026-10-30) */
void tempo_text(ed_t *e, char *out, int n);    /* the readout: "120.0" */
int tempo_host_bpm(ed_t *e, double *bpm);      /* the host's tempo (1: it gives one) */
void tempo_press(ed_t *e, int y);              /* a click on the readout: a drag (M_TEMPO) starts */
void tempo_drag(ed_t *e, int y, int shift);    /* M_TEMPO */
void tempo_release(ed_t *e);
int tempo_wheel(ed_t *e, int z, int dir, int fine);   /* 1: used */
const char *hub_zone_help(ed_t *e, int z);
void hub_help(ed_t *e, char *out, int n);
intptr_t hub_test(ed_t *e, intptr_t op, char *buf, float opt);
/* nav.c (2026-10-30): the screen's BACK / FWD (a history of the pages shown), the generator tabs (on the hub and every
 * generator page: one per generator page, a dot when it is on in the Synth in focus, and ALL), and the GENERATORS
 * overview (SCR_GENALL: every generator that is on, drawn small and moving; a click opens its page) */
enum { SCR_GENALL = -41 };
enum { Z_NAV_BACK = 680000, Z_NAV_FWD,
       Z_NAV_ALL,                /* the hub page's ALL RUNNING button: the overview */
       Z_NAV_SYN = 680010,       /* + c: the overview's SYNTH 1 / 2 / 3 */
       Z_NAV_TAB = 680100,       /* + k: tab k (0 the hub, 1 .. NHUB_TAB the generator pages, then ALL) */
       Z_NAV_TILE = 680200,      /* + row: the overview's tile of HUB[row] */
       Z_NAV_END = 680300 };
#define TEST_NAV 0x4F314E56   /* VENDOR_SPECIFIC index: value = NT_ op (nav.c) */
enum { NT_BACK_N = 1,         /* how many pages BACK can go to */
       NT_FWD_N,              /* ... and FWD */
       NT_TABS,               /* 1: the screen shows the generator tabs */
       NT_TILES,              /* the overview's tiles now (0: not shown) */
       NT_TILE_XY,            /* opt = a tile: its middle in pixels (x + 65536 y; -1: none) */
       NT_TILE_ROW,           /* opt = a tile: its HUB row (-1: none) */
       NT_TAB_XY,             /* opt = a tab: its middle in pixels (-1: none) */
       NT_TAB_DOT,            /* opt = a tab: 1 when its dot is lit */
       NT_OV_SYNTH,           /* the Synth the overview shows */
       NT_TICKS,              /* the overview's repaints asked for so far */
       NT_XY };               /* opt = a zone (Z_NAV_BACK, Z_NAV_FWD, Z_NAV_ALL, Z_NAV_SYN + c): its middle (-1: none) */
void nav_note(ed_t *e);                        /* the page shown now goes into the history if it changed */
int nav_can(ed_t *e, int fwd);                 /* BACK (0) / FWD (1) has a page to go to */
void nav_go(ed_t *e, int fwd);                 /* BACK / FWD */
int nav_tabs_shown(ed_t *e);                   /* the screen shows the generator tabs (the hub, a generator page) */
int nav_gen_param(int i);                      /* panel index i is a generator's ON (a tab's dot shows it) */
int nav_hit(ed_t *e, POINT pt);                /* the tabs, the overview, the hub's ALL RUNNING (-1: none) */
int nav_press(ed_t *e, int z);                 /* 1: z was nav's */
void nav_tabs_draw(HDC dc, ed_t *e);           /* the tabs, on the column-heads line (its own drawing surface) */
void nav_hub_draw(HDC dc, ed_t *e);            /* the hub page's ALL RUNNING */
void nav_overview_draw(HDC dc, ed_t *e);       /* SCR_GENALL, all of it but the help line */
void nav_top_draw(HDC dc, ed_t *e);            /* BACK / FWD */
void nav_tick(ed_t *e);                        /* the rate timer: the overview's tiles move */
const char *nav_zone_help(ed_t *e, int z);
void nav_help(ed_t *e, char *out, int n);      /* the overview's help line */
intptr_t nav_test(ed_t *e, intptr_t op, char *buf, float opt);
/* more_pics.c: a page's moving picture drawn small in tile a (no text; part: PHYS 1 the balls, 2 the particles, 0 both) */
int pics_tile(HDC dc, ed_t *e, int page, int part, RECT a);   /* 0: the page has no picture */
/* editor_draw.c: the sub-page arrows and "n / m" at the title line's right end (also the library's pages) */
void sub_pager_draw(HDC dc, ed_t *e, const char *n);
/* (2026-10-30, hub_gens.c) the GENERATIVE page's RANDOMISE / MUTATE / RESET and the filters' SYNC */
RECT gens_rect(int z);                         /* where Z_GEN_* / Z_SYNC_CUT sits (empty: not on this layout) */
int gens_hit(ed_t *e, POINT pt);               /* the hub page's own buttons, or -1 */
int gens_press(ed_t *e, int z);                /* 1: z was one of them */
void gens_draw(HDC dc, ed_t *e);               /* on the hub page */
const char *gens_help(ed_t *e, int z);
void gens_reset(ed_t *e, int all);             /* every generator's settings to its default (and off): one undo step */
void sync_cutoffs(ed_t *e);                    /* SYNC: both cutoffs of all three Synths to the focus's */
void ed_rnd_gens(ed_t *e, int mutate, int all);   /* (editor.c) the randomiser on the GENERATIVE section only */
int gen_param(int i);                          /* a generator's setting (Synth 1's index; not KEY / SCALE, not shared) */
void screen_button(HDC dc, RECT r, const char *label, int lit, int hot, HFONT f);   /* (editor_draw.c) */
void panel_button(HDC dc, const RECT *r, const char *label, int lit, int hot);
/* cables.c (2026-10-30): the CABLES view. CABLES on the MATRIX page's title line (a plain click; again, CABLES OFF in
 * the jack bay, or Esc: hidden) dims the panel a little and draws each active matrix row of the Synth in focus as a
 * patch cable from its source (a panel spot or a jack) to the control it moves, with an amount knob in its middle. */
#define Z_CB 690000
enum { Z_CB_BTN = Z_CB, Z_CB_OFF, Z_CB_X, Z_CB_KNOB = Z_CB + 100, Z_CB_WIRE = Z_CB + 200, Z_CB_JACK = Z_CB + 300,
       Z_CB_END = Z_CB + 400 };
#define TEST_CB 0x4F314342   /* VENDOR_SPECIFIC index: value = CBT_ op (cables.c) */
enum { CBT_ON = 1,           /* 1: shown */
       CBT_N,                /* how many cables */
       CBT_ROW,              /* opt = a cable: its matrix row (-1: none) */
       CBT_END_XY,           /* opt = 2 x cable + end (0 source, 1 destination): that end in pixels (x + 65536 y) */
       CBT_KNOB_XY,          /* opt = a cable: its amount knob's middle */
       CBT_X_XY,             /* the selected cable's X button (-1: none) */
       CBT_SEL,              /* the selected cable's row (-1: none) */
       CBT_BTN_XY,           /* the CABLES button (-1: not shown) */
       CBT_CTL_XY,           /* opt = a panel index: where its cable would end (its knob's middle) */
       CBT_BUILDS,           /* how many times the cached layer was drawn */
       CBT_JACK_XY,          /* opt = a source: its jack (-1: none) */
       CBT_WIRE_XY,          /* opt = a cable: a point on the cable a quarter of the way along */
       CBT_OFF_XY };         /* the jack bay's CABLES OFF */
int cb_hit(ed_t *e, POINT pt);                  /* a cable's part under pt (Z_CB_*), or -1 */
int cb_press(ed_t *e, int z, int x, int y);     /* a left click (z: hit()'s); 1: taken */
void cb_drag(ed_t *e, int y, int fine);         /* M_CABLE: the amount follows the mouse */
void cb_release(ed_t *e);
int cb_key(ed_t *e, int vk);                    /* Esc: hidden (1: taken) */
void cb_tick(ed_t *e);                          /* the window's timer: a changed matrix redraws the cables */
void cb_draw(HDC dc, ed_t *e, const RECT *clip);   /* over everything else, when shown */
RECT cb_button_rect(void);                      /* the CABLES button on the MATRIX page's title line */
int cb_help(ed_t *e, char *out, int n);         /* the help line while the cables show (1) */
void cb_free(ed_t *e);                          /* the window closes: the cached layer goes */
void cb_hover(ed_t *e, int z);                  /* the mouse moved onto or off zone z: it repaints */
void cb_toggle(ed_t *e);
intptr_t cb_test(ed_t *e, intptr_t op, char *buf, float opt);
/* matrix_page.c (2026-10-30): the MATRIX page's DELETE per row (the rows below move up), its filters by source and by
 * destination (SOURCE / DEST lists on the title line, CLEAR; EDIT MODULATIONS on quick assign's MOD FROM page opens
 * it filtered by that source), a destination picked in two steps (its section, then the parameter); the SETUP page's
 * LOW / HIGH buttons: click one, then press a key (the window's keyboard, the computer's keys or MIDI) */
enum { Z_MX = 697000,                      /* (after CABLES 690000-690399 and PARTS 695000-695999, before Z_ABS) */
       Z_MX_FSRC, Z_MX_FDST, Z_MX_CLEAR,   /* the MATRIX page's title line: SOURCE, DEST, CLEAR */
       Z_MX_DEL = 697010,                  /* + r: the DELETE of the screen's row r (0 .. ROWS-1) */
       Z_MX_KEY = 697030,                  /* + 2 x Synth + (0 LOW, 1 HIGH): the SETUP page's buttons */
       Z_MX_END = 697100 };
#define TEST_MX 0x4F314D58   /* VENDOR_SPECIFIC index: value = MXT_ op (matrix_page.c) */
enum { MXT_XY = 1,            /* opt = a zone (Z_MX_*): its middle in pixels (x + 65536 y; -1: not shown) */
       MXT_VIEW,              /* the MATRIX page's rows shown (all sub-pages): their numbers (1-16) into ptr; how many */
       MXT_FSRC, MXT_FDST,    /* the filters (0: none) */
       MXT_WAIT,              /* the SETUP page's wait for a key (mx_ui_t.kr) */
       MXT_CELL_XY,           /* opt = screen row r x 16 + column k (MX_*): that matrix cell's middle in pixels */
       MXT_SECTION };         /* opt = a destination (MD_*): its section in the two-step list (-1: none) */
int mx_view(ed_t *e, int *rows);                /* the MATRIX page's rows shown, in order (no filter: all NMATRIX) */
int mx_filtering(ed_t *e);
int mx_sub_of(ed_t *e, int row);                /* the MATRIX sub-page showing row (a filter that hides it goes) */
void mx_edit_mods(ed_t *e, int src);            /* EDIT MODULATIONS: the MATRIX page filtered by that source */
int mx_is_dst_list(int i);                      /* param i is a destination list (a matrix row's, the ribbon's) */
void mx_pick_dst(ed_t *e, int i, RECT r);       /* its two-step list under r: a section, then the parameter */
int mx_hit(ed_t *e, POINT pt);                  /* the MATRIX and SETUP pages' own buttons, or -1 */
int mx_press(ed_t *e, int z);                   /* 1: z was one of them */
RECT mx_rect(ed_t *e, int z);                   /* where it is (empty: not shown) */
RECT mx_title_rect(void);                       /* the MATRIX title line's text, left of its buttons */
const char *mx_zone_help(ed_t *e, int z);
int mx_help(ed_t *e, char *out, int n);         /* the help line while waiting for a key, or just after (1) */
void mx_key_heard(ed_t *e, int note);           /* a note-on from the window (its keyboard, the computer's keys) */
void mx_midi_seen(ed_t *e, const void *events); /* the host's MIDI (its thread): a note-on, lock-free */
int mx_key(ed_t *e, int vk);                    /* Esc: the wait for a key cancelled (1: taken) */
void mx_tick(ed_t *e);                          /* the window's timer: a host note heard, a message that ran out */
void mx_setup_draw(HDC dc, ed_t *e, int row, int key);   /* the SETUP page's LOW / HIGH buttons on that row (editor_draw.c) */
RECT mx_setup_value(int row, int c);            /* a key row's cell, less its button */
int mx_setup_key(int key);                      /* the SETUP row's param (Synth 1's) is key_lo (0) / key_hi (1), else -1 */
intptr_t mx_test(ed_t *e, intptr_t op, char *buf, float opt);
/* parts.c (2026-10-30): effect and matrix presets (the library's PARTS page), the Synths' names, user libraries,
 * DUPLICATE NAMES (parts.h) */
int parts_zone_rect(ed_t *e, int z, RECT *r);  /* where PARTS page zone z is (0: not shown) */
int parts_hit(ed_t *e, POINT pt);              /* the PARTS page */
int parts_subs(ed_t *e);
void parts_open(ed_t *e, int slot);            /* the PARTS page on slot (-1: as it was) */
void parts_draw(HDC dc, ed_t *e);
void parts_help(ed_t *e, char *out, int n);
int parts_press(ed_t *e, int z);               /* 1: z was parts.c's */
const char *parts_zone_help(ed_t *e, int z);
int parts_more_page(int p);                    /* MORE page p is an effect's: its slot (PS_), else -1 */
RECT parts_more_rect(void);                    /* its PRESETS button, at the right of the note line */
void parts_more_draw(HDC dc, ed_t *e);
int parts_name(ed_t *e, int c, char *out, int n);   /* Synth c's name (1), or "" (0) */
void parts_label(ed_t *e, int c, char *out, int n); /* "SYNTH 1" or its name, for the SYNTH buttons */
int parts_names_line(ed_t *e, char *out, int n);    /* "S1 Bass  S2 Pad" for the home line (0: no names) */
void parts_names_clear(ed_t *e);               /* a factory preset loaded: no names */
RECT parts_name_head(int c);                   /* the SETUP page's head of Synth c */
void parts_tick(ed_t *e);                      /* the window's timer: names that changed repaint */
int parts_lib_hit(ed_t *e, POINT pt);          /* the SETTINGS page's LIBRARY rows (-1: none) */
void parts_lib_draw(HDC dc, ed_t *e);
intptr_t parts_test(ed_t *e, intptr_t op, char *buf, float opt);
int space_dir(const char *name, char *out, int n, int create);   /* user library `name`'s folder (1 it exists / made) */
/* settings.c: the library in use and DUPLICATE NAMES */
int settings_space(char *out, int n);
int settings_space_set(const char *name);
int settings_import_dup(void);
int settings_import_dup_set(int v);
/* patches.c (2026-10-30) */
int patch_home_dir(char *out, int n, int create);   /* the default library's folder (patch_dir: the one in use) */
int patches_import_named(ed_t *e, const char *src, const char *as, int overwrite);
int patches_import_choice(ed_t *e, const char *file);   /* IMPORT with DUPLICATE NAMES' choice */
int patches_free_name(const char *name, char *out);     /* "Name 2", "Name 3" ...: one no patch has */
int patches_factory_state(int p, char *out, int n, const char **name);   /* factory preset p as a state (0: none) */
