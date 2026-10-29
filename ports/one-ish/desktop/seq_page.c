/* The step sequencer's page for the desktop editor (see seq_page.h): the STEP EDITOR on the centre screen (the SEQ
 * button), the SEQUENCER module's page LEDs, and sequences as .oneseq files. UI thread only.
 *
 * The step editor shows one page of 16 steps (the page buttons 1-4 on its title line; steps past LENGTH are dimmed)
 * in two views. NOTES: a row per note lane, each note a box whose height is its velocity and width its gate, with its
 * name, a tick per ratchet and an arrow when tied; rest steps are hatched. MODS: a row per modulation lane, each step's
 * offset a bar up or down from the middle. Under the grid, the selected note's (or lane's) values and the buttons.
 *   NOTES: click a cell to select it (drag a note up/down for velocity, sideways for its gate); double-click an empty
 *   cell for a new note (the last one's values); right-click deletes; the wheel moves a note by a semitone (SHIFT: an
 *   octave). With REC lit, the keyboard (on screen or MIDI) enters notes into the selected step and moves on.
 *   MODS: click or drag across a row to set offsets; right-click removes one; DEST, CTRL and AMT set the lane.
 *   Step numbers: click selects the step, SHIFT + click marks the end of the range DUPLICATE copies, right-click toggles
 *   its rest. Arrow keys move the selection, Delete deletes (SHIFT + Delete: the whole step).
 *   (2026-10-30, plain clicks for all of it) RANGE (the title line's left end, NOTES and MODS): lit, the next step number
 *   clicked marks the range's end (as SHIFT + click); lit with a range marked, a click clears it. A click on the selected
 *   empty note cell puts a note there (as a double-click). REST, DELETE and ALL (DEL STEP) are the right-click's and
 *   SHIFT + Delete's buttons.
 *   (2026-10-29) GEN (the third view): the sequence generator (src/seqgen.h) for the Synth in focus: STYLE, RHYTHM,
 *   DENSITY, RANGE, VARIATION, ACCENT, FILLS and MODS on the left, its pattern on the right, KEY / SCALE / LENGTH / SEED
 *   in the values row, GENERATE and RE-ROLL; each is one undo step.
 *   (2026-10-29) ALL: the values row moves every note of the step together (NOTE) or gives them one VEL, GATE or RATCH,
 *   and DELETE becomes DEL STEP. The title line's RTZ returns to step 1; AUTO (step edit auto-scroll, on by default)
 *   makes the page shown follow the step playing. SAVE asks for performance notes, saved in the file and shown on LOAD.
 * The engine owns the sequence: every edit is a command sent to it (wrapper/vst2_wrap.c's effSetChunk with "seqcmd",
 * src/stepseq_run.h sq_command), applied between two audio blocks; the page reads the sequence back from the plugin's
 * state (effGetChunk, its "seq=" text) after its own edits, and on its timer while REC or STEP MOD can change it. The
 * playing step comes from the "seq_pos" readout.
 *
 * Sequence files: <name>.oneseq in Documents\ONE-ish\Sequences (or %APPDATA%\ONE-ish\Sequences; the environment
 * variable ONEISH_SEQ_DIR overrides both), plain text:
 *     ONE-ish sequence 1
 *     name=Walking Bass
 *     notes=Play the low C with the wheel half up   (2026-10-29: performance notes, one line; may be empty)
 *     seq_length=16       the length, division and timing (the sequence's shape); the other settings stay as they are
 *     seq_div=4
 *     seq_timing=0
 *     seq=<text>           the steps (src/stepseq.h) */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
void lib_touch(ed_t *e, const char *label);   /* (library.h: GENERATE is one undo step) */
void lib_commit(ed_t *e);
void lib_hold(ed_t *e, int d);
#include <commdlg.h>
#include <shlobj.h>
#pragma GCC diagnostic push   /* (only some of its drawing functions are used here) */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "gfx.h"
#include "skin.h"
#pragma GCC diagnostic pop

#define EXT ".oneseq"
static const char HEADER[] = "ONE-ish sequence";
enum { Z_CELL = Z_SEQ_BASE, Z_STEPNUM = Z_SEQ_BASE + 200, Z_PAGEBTN = Z_SEQ_BASE + 220, Z_VIEW = Z_SEQ_BASE + 230,
       Z_FIELD = Z_SEQ_BASE + 240, Z_BTN = Z_SEQ_BASE + 260, Z_LANE = Z_SEQ_BASE + 280, Z_PLED = Z_SEQ_BASE + 300,
       Z_SEQ_END = Z_SEQ_BASE + 400 };
enum { NFIELD = 4, NBTN = 8, LABEL_W = 24 };
/* MODS: B_REST is DEL LANE, B_TIE and B_ALL are unused; with ALL lit, B_DELETE is DEL STEP */
enum { B_REST, B_TIE, B_ALL, B_DELETE, B_CLEAR, B_DUP, B_SAVE, B_LOAD };
enum { Z_RTZ = Z_SEQ_BASE + 234, Z_AUTO = Z_SEQ_BASE + 235 };   /* the title line's RTZ and AUTO (2026-10-29) */
enum { Z_RANGE = Z_SEQ_BASE + 236 };   /* (2026-10-30) RANGE: the next step number ends DUP's range */
/* the GEN view (2026-10-29): its option rows, its values row (KEY, SCALE, LENGTH, SEED), GENERATE and RE-ROLL */
enum { Z_GEN_ROW = Z_SEQ_BASE + 320, Z_GEN_FIELD = Z_SEQ_BASE + 340, Z_GEN_GO = Z_SEQ_BASE + 350,
       Z_GEN_REROLL = Z_SEQ_BASE + 351 };
enum { V_NOTES, V_MODS, V_GEN, NVIEWS };
enum { GEN_ROWS = 8, GEN_LABEL_W = 58, GEN_CTL_R = 240, GEN_PREV_L = 248 };   /* its rows; x of its parts */
static RECT gen_row_rect(int row);
static RECT gen_btn_rect(int which);
#define C_LCD 0xFF96D7E8
#define C_DIM 0xFF5A8291
#define T_LCD RGB(150, 215, 232)
#define T_DIM RGB(90, 130, 145)
#define T_HOT RGB(255, 255, 255)
#define T_DARK RGB(11, 22, 33)

static int P_DST = -1, P_SRC = -1;   /* the matrix's destination and source lists (mod1_dst / mod1_src): names, counts */
static int P_KEY = -1, P_SCALE = -1; /* the Synth's KEY and SCALE (key_root / key_scale; the focus's own through fp) */
static int param_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
static int ndest(void) { return P_DST >= 0 ? PARAMS[P_DST].nopts : 1; }
static int nsrc(void) { return P_SRC >= 0 ? PARAMS[P_SRC].nopts : 1; }
static const char *dest_name(int d) { return P_DST >= 0 && d >= 0 && d < ndest() ? PARAMS[P_DST].opts[d] : "?"; }
static const char *src_name(int c) { return c == 0 ? "None" : P_SRC >= 0 && c < nsrc() ? PARAMS[P_SRC].opts[c] : "?"; }

static void redraw(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }
static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->sq.msg, sizeof e->sq.msg, fmt, ap);
    va_end(ap);
    e->sq.msg_at = GetTickCount();
    redraw(e);
}
const char *seq_message(ed_t *e) { return e->sq.msg[0] && GetTickCount() - e->sq.msg_at < 6000 ? e->sq.msg : NULL; }

void seq_init(ed_t *e) {
    if (P_DST < 0) { P_DST = param_index("mod1_dst"); P_SRC = param_index("mod1_src"); }
    if (P_KEY < 0) { P_KEY = param_index("key_root"); P_SCALE = param_index("key_scale"); }
    for (int c = 0; c < SQ_GEN_SYNTHS; c++) { seqgen_defaults(&e->sq.gen[c]); e->sq.gen_seed[c] = 0; }
    sq_clear(&e->sq.q);
    e->sq.len = 16;
    e->sq.range_to = e->sq.play = -1;
    e->sq.last_note = 60;
    e->sq.last_vel = 100;
    e->sq.last_gate = 50;
    e->sq.dirty = 1;
    e->sq.autoscroll = 1;
}

/* ---- talking to the engine ---- */
static int seq_len(ed_t *e) { return SEQ_P[3] >= 0 ? (int)lroundf(real_of(e, SEQ_P[3])) : 16; }
static int on_of(ed_t *e, int k) { return SEQ_P[k] >= 0 && option_of(e, SEQ_P[k]) == 1; }

/* the sequence from the plugin's state; 1 when it differs from the copy shown */
int seq_read(ed_t *e) {
    void *chunk = NULL;
    sq_seq_t q;
    sq_clear(&q);
    intptr_t len = wrapped_call(e->fx, effGetChunk, 0, 0, &chunk, 0);
    char key[16];   /* the focused Synth's sequence: ";seq=" (Synth 1), ";s2_seq=", ";s3_seq=" */
    if (e->focus) snprintf(key, sizeof key, ";s%d_seq=", e->focus + 1);
    else snprintf(key, sizeof key, ";seq=");
    const char *p = len > 0 && chunk ? strstr((const char *)chunk, key) : NULL;
    if (p) sq_decode(p + strlen(key), &q, ndest(), nsrc());
    else if (!strncmp(e->sq.readout, "Too long", 8)) q = e->sq.q;   /* not in the state (no room): keep what we know */
    int n = seq_len(e), changed = memcmp(&q, &e->sq.q, sizeof q) != 0 || n != e->sq.len;
    e->sq.q = q;
    e->sq.len = n;
    e->sq.dirty = 0;
    return changed;
}

/* one or more commands (src/stepseq_run.h sq_command), then the result read back */
static void seq_send(ed_t *e, const char *fmt, ...) {
    static char b[SQ_TEXT_MAX + 512];
    int n = e->focus ? snprintf(b, sizeof b, "seqcmd%d\n", e->focus + 1)   /* to the focused Synth's sequencer */
                     : snprintf(b, sizeof b, "seqcmd\n");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + n, sizeof b - n, fmt, ap);
    va_end(ap);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(b) + 1, b, 0);
    e->pt.forced_edit = 1;   /* the sound (its sequence) is no longer the patch as saved */
    seq_read(e);
    redraw(e);
}
/* a whole sequence (the randomiser's modulation lanes), as a .oneseq file's load does */
void seq_load(ed_t *e, const sq_seq_t *q) {
    static char enc[SQ_TEXT_MAX];
    sq_encode(q, enc, sizeof enc);
    seq_send(e, "load %s", enc);
}
static void send_note(ed_t *e, int s, int l) {
    const sq_note_t *n = &e->sq.q.n[s][l];
    if (!sq_has(n)) seq_send(e, "note %d %d -1 0 0 0 0", s, l);
    else seq_send(e, "note %d %d %d %d %d %d %d", s, l, n->note, n->vel, n->gate, sq_ratchet(n), !!(n->flags & SQ_TIE));
}
static void select_cell(ed_t *e, int step, int lane) {
    int len = e->sq.len < 1 ? 1 : e->sq.len;
    e->sq.step = step < 0 ? 0 : step >= SQ_STEPS ? SQ_STEPS - 1 : step;
    if (e->sq.view) e->sq.mlane = lane < 0 ? 0 : lane >= SQ_ML ? SQ_ML - 1 : lane;
    else e->sq.lane = lane < 0 ? 0 : lane >= SQ_NL ? SQ_NL - 1 : lane;
    e->sq.page = e->sq.step / SQ_PAGE;
    (void)len;
    seq_send(e, "sel %d %d", e->sq.step, e->sq.lane);
}

/* the timer (WM_TIMER, every 50 ms): the readout always (the page LEDs follow the playing step); the sequence while
 * the step editor is shown and REC or STEP MOD can change it, or when something else changed. 1: redraw. */
int seq_tick(ed_t *e, int params_changed) {
    char ro[64] = "";
    int redo = 0;
    if (SEQ_P[4] >= 0) e->fx->dispatcher(e->fx, effGetParamDisplay, fp(e, SEQ_P[4]), 0, ro, 0);
    if (strcmp(ro, e->sq.readout)) {
        snprintf(e->sq.readout, sizeof e->sq.readout, "%s", ro);
        int a = 0, b = 0;
        e->sq.play = sscanf(ro, "Step %d/%d", &a, &b) == 2 ? a - 1 : -1;
        if (sscanf(ro, "Edit %d/%d", &a, &b) == 2 && a - 1 != e->sq.step && on_of(e, 2)) {   /* step recording moved on */
            e->sq.step = a - 1;
            e->sq.page = e->sq.step / SQ_PAGE;
            e->sq.dirty = 1;
        }
        redo = 1;
    }
    if (e->sq.autoscroll && e->sq.play >= 0 && e->sq.play / SQ_PAGE != e->sq.page) {   /* AUTO: the playing page */
        e->sq.page = e->sq.play / SQ_PAGE;
        redo = 1;
    }
    if (params_changed) e->sq.dirty = 1;
    if (e->screen == SCR_SEQ && (e->sq.dirty || on_of(e, 2) || on_of(e, 5))) redo |= seq_read(e);
    else if (e->sq.len != seq_len(e)) { e->sq.len = seq_len(e); redo = 1; }
    if (e->sq.msg[0] && !seq_message(e)) { e->sq.msg[0] = 0; redo = 1; }
    return redo;
}

void seq_open(ed_t *e, int page) {
    if (page >= 0) e->sq.page = page;
    e->screen = SCR_SEQ;
    e->sub = 0;
    seq_read(e);
    redraw(e);
}

/* ---- geometry: the screen's rows (editor.c layout(): the title line, the head, 8 rows, the help area) ---- */
static int grid_x0(void) { return L.row[0].left + sc(LABEL_W); }
static float cell_w(void) { return (L.row[0].right - grid_x0()) / (float)SQ_PAGE; }
static RECT cell_rect(int col, int row) {
    float x = grid_x0() + col * cell_w();
    RECT r = {(int)x, L.row[row].top, (int)(x + cell_w()), L.row[row].bottom};
    return r;
}
static RECT stepnum_rect(int col) {
    float x = grid_x0() + col * cell_w();
    RECT r = {(int)x, L.head.top, (int)(x + cell_w()), L.head.bottom};
    return r;
}
static RECT lane_rect(int row) {
    RECT r = {L.row[row].left, L.row[row].top, grid_x0() - sc(2), L.row[row].bottom};
    return r;
}
static RECT pagebtn_rect(int p) {
    int w = sc(22), x = L.tabline.right - (SQ_STEPS / SQ_PAGE - p) * (w + sc(3));
    RECT r = {x, L.tabline.top, x + w, L.tabline.bottom};
    return r;
}
static RECT view_rect(int v) {   /* NOTES, MODS, GEN */
    int x = L.tabline.left + sc(56) + v * sc(48);
    RECT r = {x, L.tabline.top, x + sc(44), L.tabline.bottom};
    return r;
}
static RECT range_rect(void) {   /* RANGE, where the title line says STEP EDIT in the GEN view */
    RECT r = {L.tabline.left, L.tabline.top, L.tabline.left + sc(52), L.tabline.bottom};
    return r;
}
static int range_lit(ed_t *e) { return e->sq.range_arm || e->sq.range_to >= 0; }
static RECT title_btn_rect(int z) {   /* RTZ and AUTO, after the views */
    int x = L.tabline.left + sc(202) + (z == Z_AUTO ? sc(32) : 0);
    RECT r = {x, L.tabline.top, x + (z == Z_AUTO ? sc(36) : sc(28)), L.tabline.bottom};
    return r;
}
static RECT field_rect(ed_t *e, int f) {   /* the values row: a label then four fields */
    static const short NOTE_X[NFIELD + 1] = {70, 150, 222, 300, 384}, MOD_X[NFIELD + 1] = {50, 158, 258, 312, 384},
                       GEN_X[NFIELD + 1] = {50, 112, 236, 306, 384};   /* GEN: KEY, SCALE, LENGTH, SEED */
    const short *x = e->sq.view == V_GEN ? GEN_X : e->sq.view ? MOD_X : NOTE_X;
    RECT r = {L.help.left + sc(x[f]), L.help.top, L.help.left + sc(x[f + 1]) - sc(4), L.help.top + sc(20)};
    return r;
}
static RECT btn_rect(int b) {
    float w = (rw(&L.help) - sc(4) * (NBTN - 1)) / (float)NBTN;
    int x = L.help.left + (int)(b * (w + sc(4))), y = L.help.top + sc(24);
    RECT r = {x, y, x + (int)w, y + sc(19)};
    return r;
}
RECT seq_help_rect(void) {
    RECT r = L.help;
    r.top += sc(48);
    return r;
}
static RECT pled_rect(int p) {   /* the SEQUENCER module's page LEDs: four in a row, numbered */
    float w = rw(&L.seq_leds) / 4.0f;
    RECT r = {L.seq_leds.left + (int)(p * w), L.seq_leds.top, L.seq_leds.left + (int)((p + 1) * w), L.seq_leds.top + sc(28)};
    return r;
}

/* ---- hits ---- */
int seq_hit(ed_t *e, POINT pt) {
    for (int p = 0; p < SQ_STEPS / SQ_PAGE; p++) { RECT r = pagebtn_rect(p); if (PtInRect(&r, pt)) return Z_PAGEBTN + p; }
    for (int v = 0; v < NVIEWS; v++) { RECT r = view_rect(v); if (PtInRect(&r, pt)) return Z_VIEW + v; }
    for (int z = Z_RTZ; z <= Z_AUTO; z++) { RECT r = title_btn_rect(z); if (PtInRect(&r, pt)) return z; }
    if (e->sq.view != V_GEN) { RECT r = range_rect(); if (PtInRect(&r, pt)) return Z_RANGE; }
    if (e->sq.view == V_GEN) {   /* the generator's rows, values, GENERATE / RE-ROLL, and CLEAR .. LOAD */
        for (int row = 0; row < GEN_ROWS; row++) { RECT r = gen_row_rect(row); if (PtInRect(&r, pt)) return Z_GEN_ROW + row; }
        for (int f = 0; f < NFIELD; f++) { RECT r = field_rect(e, f); if (PtInRect(&r, pt)) return Z_GEN_FIELD + f; }
        for (int k = 0; k < 2; k++) { RECT r = gen_btn_rect(k); if (PtInRect(&r, pt)) return k ? Z_GEN_REROLL : Z_GEN_GO; }
        for (int b = B_CLEAR; b < NBTN; b++) { RECT r = btn_rect(b); if (PtInRect(&r, pt)) return Z_BTN + b; }
        return -1;
    }
    for (int c = 0; c < SQ_PAGE; c++) {
        RECT r = stepnum_rect(c);
        if (PtInRect(&r, pt)) return Z_STEPNUM + c;
        for (int row = 0; row < ROWS; row++) { r = cell_rect(c, row); if (PtInRect(&r, pt)) return Z_CELL + c * ROWS + row; }
    }
    for (int row = 0; row < ROWS; row++) { RECT r = lane_rect(row); if (PtInRect(&r, pt)) return Z_LANE + row; }
    for (int f = 0; f < NFIELD; f++) { RECT r = field_rect(e, f); if (PtInRect(&r, pt)) return Z_FIELD + f; }
    for (int b = 0; b < NBTN; b++) {
        RECT r = btn_rect(b);
        if (e->sq.view && (b == B_TIE || b == B_ALL)) continue;
        if (PtInRect(&r, pt)) return Z_BTN + b;
    }
    return -1;
}
int seq_hit_panel(ed_t *e, POINT pt) {
    (void)e;
    if (rw(&L.seq_leds) <= 0) return -1;
    for (int p = 0; p < 4; p++) { RECT r = pled_rect(p); if (PtInRect(&r, pt)) return Z_PLED + p; }
    return -1;
}
int seq_zone(int z) { return z >= Z_SEQ_BASE && z < Z_SEQ_END; }

/* ---- values of the selection ---- */
static sq_note_t *sel_note(ed_t *e) { return &e->sq.q.n[e->sq.step][e->sq.lane]; }
static sq_mlane_t *sel_mlane(ed_t *e) { return &e->sq.q.m[e->sq.mlane]; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void new_note(ed_t *e, int s, int l, int note) {
    sq_set_note(&e->sq.q, s, l, clampi(note, 0, 127), e->sq.last_vel, e->sq.last_gate, 1, 0);
    send_note(e, s, l);
}
static void remember(ed_t *e, const sq_note_t *n) {   /* a new note gets the values last edited */
    e->sq.last_note = n->note;
    e->sq.last_vel = n->vel;
    if (n->gate != e->sq.last_gate) seq_send(e, "gate %d", n->gate);   /* (the keys' notes too: the engine's) */
    e->sq.last_gate = n->gate;
}
/* field f of the selection moved by d steps (the wheel, a drag); NOTES: note, velocity, gate, ratchet; MODS: amount
 * and the step's offset (DEST and CTRL are lists) */
static void field_step(ed_t *e, int f, int d) {
    if (!e->sq.view && e->sq.all) {   /* ALL: every note of the step: moved together, or given the one value */
        const sq_note_t *n = sel_note(e);
        for (int l = 0; l < SQ_NL && !sq_has(n); l++) n = &e->sq.q.n[e->sq.step][l];   /* (the selected lane, or the first) */
        if (!sq_has(n)) return;
        int s = e->sq.step, rat = clampi(sq_ratchet(n) + (d > 0 ? 1 : -1), 1, 4);
        if (f == 0) seq_send(e, "stepnotes %d %d 0 0 0", s, d);
        if (f == 1) seq_send(e, "stepnotes %d 0 %d 0 0", s, clampi(n->vel + d, 1, 127));
        if (f == 2) seq_send(e, "stepnotes %d 0 0 %d 0", s, clampi(n->gate + d, 1, 100));
        if (f == 3) seq_send(e, "stepnotes %d 0 0 0 %d", s, rat);
        return;
    }
    if (!e->sq.view) {
        sq_note_t *n = sel_note(e);
        if (!sq_has(n)) { if (f == 0) new_note(e, e->sq.step, e->sq.lane, e->sq.last_note + d); return; }
        int note = n->note, vel = n->vel, gate = n->gate, rat = sq_ratchet(n);
        if (f == 0) note = clampi(note + d, 0, 127);
        if (f == 1) vel = clampi(vel + d, 1, 127);
        if (f == 2) gate = clampi(gate + d, 1, 100);
        if (f == 3) rat = clampi(rat + (d > 0 ? 1 : -1), 1, 4);
        sq_set_note(&e->sq.q, e->sq.step, e->sq.lane, note, vel, gate, rat, n->flags & SQ_TIE);
        send_note(e, e->sq.step, e->sq.lane);   /* (first: remember() may read the sequence back) */
        remember(e, n);
        return;
    }
    sq_mlane_t *m = sel_mlane(e);
    if (!m->dest) return;
    if (f == 2) seq_send(e, "lane %d %d %d %d", e->sq.mlane, m->dest, m->ctl, clampi(m->amt + d, 0, 100));
    if (f == 3) {
        int v = m->off[e->sq.step] == SQ_NO_OFF ? 0 : m->off[e->sq.step];
        seq_send(e, "off %d %d %d", e->sq.mlane, e->sq.step, clampi(v + d, -100, 100));
    }
}
static int pick_list(ed_t *e, RECT r, int param, int cur) {   /* a popup of a param's options (long lists: columns) */
    if (param < 0) return -1;
    HMENU m = CreatePopupMenu();
    for (int k = 0; k < PARAMS[param].nopts; k++)
        AppendMenuA(m, MF_STRING | (k == cur ? MF_CHECKED : 0) | (k && k % 30 == 0 ? MF_MENUBARBREAK : 0), (UINT_PTR)(k + 1),
                    k == 0 && param == P_SRC ? "None" : k == 0 && param == P_DST ? "Off (no lane)" : PARAMS[param].opts[k]);
    POINT pt = {r.left, r.bottom};
    ClientToScreen(e->wnd, &pt);
    int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    DestroyMenu(m);
    return c - 1;
}

/* ---- files ---- */
static int known_dir(int csidl, char *out, int n) {
    char base[MAX_PATH];
    if (SHGetFolderPathA(NULL, csidl, NULL, 0, base) != S_OK) return 0;
    return snprintf(out, n, "%s\\ONE-ish\\Sequences", base) < n;
}
static int is_dir(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
/* the sequence folder (made when `create`), as patches.c finds the patch folder */
static int seq_dir(char *out, int n, int create) {
    char d[MAX_PATH], a[MAX_PATH];
    DWORD k = GetEnvironmentVariableA("ONEISH_SEQ_DIR", d, sizeof d);
    if (k > 0 && k < sizeof d) {
        if (!GetFullPathNameA(d, n, out, NULL)) return 0;
        return !create || is_dir(out) || SHCreateDirectoryExA(NULL, out, NULL) == ERROR_SUCCESS;
    }
    int hd = known_dir(CSIDL_PERSONAL, d, sizeof d), ha = known_dir(CSIDL_APPDATA, a, sizeof a);
    const char *pick = hd && is_dir(d) ? d : ha && is_dir(a) ? a : NULL;
    if (!pick && create) {
        if (hd && (SHCreateDirectoryExA(NULL, d, NULL) == ERROR_SUCCESS || is_dir(d))) pick = d;
        else if (ha && (SHCreateDirectoryExA(NULL, a, NULL) == ERROR_SUCCESS || is_dir(a))) pick = a;
    }
    if (!pick) pick = hd ? d : ha ? a : NULL;
    return pick && snprintf(out, n, "%s", pick) < n;
}
static int name_ok(const char *s) {
    if (!*s || strlen(s) > 48 || strpbrk(s, "\\/:*?\"<>|")) return 0;
    for (const char *c = s; *c; c++) if ((unsigned char)*c < 32) return 0;
    return 1;
}
static void base_name(const char *path, char *out, int n) {   /* "C:\x\My Line.oneseq" -> "My Line" */
    const char *b = strrchr(path, '\\');
    b = b ? b + 1 : path;
    snprintf(out, n, "%s", b);
    char *dot = strrchr(out, '.');
    if (dot && !_stricmp(dot, EXT)) *dot = 0;
}
static int write_seq(ed_t *e, const char *path, const char *name) {
    char text[SQ_TEXT_MAX + 512], st[SQ_TEXT_MAX];
    seq_read(e);
    sq_encode(&e->sq.q, st, sizeof st);
    snprintf(text, sizeof text, "%s 1\nname=%s\nnotes=%s\nseq_length=%d\nseq_div=%d\nseq_timing=%d\nseq=%s\n", HEADER,
             name, e->sq.notes, e->sq.len, SEQ_P[6] >= 0 ? option_of(e, SEQ_P[6]) : 4,
             SEQ_P[7] >= 0 ? option_of(e, SEQ_P[7]) : 0, st);
    char tmp[MAX_PATH + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileA(tmp);
    return ok;
}
/* a value of one of the sequence's settings, set as a knob would (the host is told) */
static void set_option(ed_t *e, int i, int opt) {
    if (i < 0) return;
    float v = PARAMS[i].nopts > 1 ? (float)opt / (PARAMS[i].nopts - 1)
                                  : (opt - PARAMS[i].min) / (PARAMS[i].max - PARAMS[i].min);
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    i = fp(e, i);   /* the focused Synth's */
    if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
    e->fx->setParameter(e->fx, i, v);
    if (e->host) e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
    if (e->host) e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
}
static int read_seq(ed_t *e, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char text[SQ_TEXT_MAX + 4096];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    const char *t = !strncmp(text, "\xEF\xBB\xBF", 3) ? text + 3 : text;
    while (*t && isspace((unsigned char)*t)) t++;
    if (strncmp(t, HEADER, sizeof HEADER - 1)) return -1;
    int len = -1, div = -1, timing = -1;
    const char *seq = NULL;
    char notes[SQ_NOTES_MAX + 1] = "";   /* (a file without notes: none) */
    for (const char *l = t; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL) {
        if (!strncmp(l, "notes=", 6)) {
            int k = 0;
            for (const char *c = l + 6; *c && *c != '\n' && *c != '\r' && k < SQ_NOTES_MAX; c++) notes[k++] = *c;
            notes[k] = 0;
        }
        else if (!strncmp(l, "seq_length=", 11)) len = atoi(l + 11);
        else if (!strncmp(l, "seq_div=", 8)) div = atoi(l + 8);
        else if (!strncmp(l, "seq_timing=", 11)) timing = atoi(l + 11);
        else if (!strncmp(l, "seq=", 4)) seq = l + 4;
    }
    sq_seq_t q;
    if (!seq || !sq_decode(seq, &q, ndest(), nsrc())) return -1;
    char enc[SQ_TEXT_MAX];
    sq_encode(&q, enc, sizeof enc);
    if (len >= 1 && len <= SQ_STEPS) set_option(e, SEQ_P[3], len);
    if (div >= 0 && SEQ_P[6] >= 0 && div < PARAMS[SEQ_P[6]].nopts) set_option(e, SEQ_P[6], div);
    if (timing >= 0 && SEQ_P[7] >= 0 && timing < PARAMS[SEQ_P[7]].nopts) set_option(e, SEQ_P[7], timing);
    seq_send(e, "load %s", enc);
    snprintf(e->sq.notes, sizeof e->sq.notes, "%s", notes);
    return 1;
}
static int save_named(ed_t *e, const char *name) {
    char dir[MAX_PATH], path[MAX_PATH + 64];
    if (!name_ok(name)) { say(e, "\"%s\" can't be a file name.", name); return 0; }
    if (!seq_dir(dir, sizeof dir, 1)) { say(e, "No folder for sequences (Documents or AppData)."); return 0; }
    snprintf(path, sizeof path, "%s\\%s" EXT, dir, name);
    if (!write_seq(e, path, name)) { say(e, "Couldn't save \"%s\" (Windows error %lu).", name, GetLastError()); return 0; }
    say(e, "Saved the sequence as \"%s\" (%s).", name, dir);
    return 1;
}
static int load_path(ed_t *e, const char *path) {
    char name[64];
    base_name(path, name, sizeof name);
    int r = read_seq(e, path);
    if (r > 0 && e->sq.notes[0]) say(e, "Loaded the sequence \"%s\". Notes: %s", name, e->sq.notes);
    else if (r > 0) say(e, "Loaded the sequence \"%s\".", name);
    else say(e, r < 0 ? "\"%s\" is not a ONE-ish sequence (or it is damaged)." : "Couldn't open \"%s\".", name);
    return r > 0;
}
static int file_dialog(ed_t *e, int save, char *path, int n) {
    char dir[MAX_PATH];
    seq_dir(dir, sizeof dir, save);
    OPENFILENAMEA o = {0};
    o.lStructSize = sizeof o;
    o.hwndOwner = e->wnd;
    o.lpstrFilter = "ONE-ish sequences (*.oneseq)\0*.oneseq\0All files\0*.*\0";
    o.lpstrFile = path;
    o.nMaxFile = (DWORD)n;
    o.lpstrInitialDir = dir;
    o.lpstrDefExt = "oneseq";
    o.lpstrTitle = save ? "Save the sequence" : "Load a sequence";
    o.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o);
}

/* ---- PERFORMANCE NOTES (2026-10-29): a line of text saved with a sequence, asked for on SAVE ---- */
enum { ID_NOTES_EDIT = 101 };
static INT_PTR CALLBACK notes_proc(HWND d, UINT m, WPARAM wp, LPARAM lp) {
    char *text = (char *)GetWindowLongPtrA(d, DWLP_USER);
    if (m == WM_INITDIALOG) {
        SetWindowLongPtrA(d, DWLP_USER, lp);
        SetDlgItemTextA(d, ID_NOTES_EDIT, (const char *)lp);
        SendDlgItemMessageA(d, ID_NOTES_EDIT, EM_LIMITTEXT, SQ_NOTES_MAX, 0);
        SendDlgItemMessageA(d, ID_NOTES_EDIT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(d, ID_NOTES_EDIT));
        return FALSE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDOK && text) {
        GetDlgItemTextA(d, ID_NOTES_EDIT, text, SQ_NOTES_MAX + 1);
        for (char *c = text; *c; c++) if ((unsigned char)*c < 32) *c = ' ';   /* one line */
        EndDialog(d, 1);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDCANCEL) { EndDialog(d, 0); return TRUE; }
    return FALSE;
}
static WORD *dlg_text(WORD *w, const char *s) {   /* a dialog template's text: UTF-16, here plain ASCII */
    do *w++ = (WORD)(unsigned char)*s; while (*s++);
    return w;
}
static WORD *dlg_item(WORD *w, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const char *text) {
    w = (WORD *)(((ULONG_PTR)w + 3) & ~(ULONG_PTR)3);
    DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)w;
    it->style = style | WS_CHILD | WS_VISIBLE;
    it->dwExtendedStyle = 0;
    it->x = x; it->y = y; it->cx = cx; it->cy = cy; it->id = id;
    w = (WORD *)(it + 1);
    *w++ = 0xFFFF;
    *w++ = cls;   /* 0x80 button, 0x81 edit, 0x82 static */
    w = dlg_text(w, text);
    *w++ = 0;
    return w;
}
/* a modal box asking for the notes (in: the current ones; out: the new ones); 0 if cancelled */
static int ask_notes(ed_t *e, char *notes) {
    static DWORD buf[512];
    memset(buf, 0, sizeof buf);
    DLGTEMPLATE *dt = (DLGTEMPLATE *)buf;
    dt->style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dt->cdit = 4;
    dt->cx = 280;
    dt->cy = 62;
    WORD *w = (WORD *)(dt + 1);
    *w++ = 0;
    *w++ = 0;
    w = dlg_text(w, "Performance notes");
    *w++ = 9;
    w = dlg_text(w, "Segoe UI");
    w = dlg_item(w, SS_LEFT, 8, 8, 264, 10, 0xFFFF, 0x82, "Notes saved with this sequence (optional), one line:");
    w = dlg_item(w, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 8, 21, 264, 13, ID_NOTES_EDIT, 0x81, "");
    w = dlg_item(w, WS_TABSTOP | BS_DEFPUSHBUTTON, 168, 41, 50, 14, IDOK, 0x80, "OK");
    dlg_item(w, WS_TABSTOP | BS_PUSHBUTTON, 222, 41, 50, 14, IDCANCEL, 0x80, "Skip");
    char text[SQ_NOTES_MAX + 1];
    snprintf(text, sizeof text, "%s", notes);
    HINSTANCE inst = e->wnd ? (HINSTANCE)GetWindowLongPtrA(e->wnd, GWLP_HINSTANCE) : GetModuleHandleA(NULL);
    if (DialogBoxIndirectParamA(inst, dt, e->wnd, notes_proc, (LPARAM)text) != 1) return 0;
    snprintf(notes, SQ_NOTES_MAX + 1, "%s", text);
    return 1;
}

/* ---- the buttons ---- */
static void press_button(ed_t *e, int b) {
    int len = e->sq.len < 1 ? 1 : e->sq.len;
    switch (b) {
    case B_REST:
        if (e->sq.view) {   /* MODS: DEL LANE */
            if (sel_mlane(e)->dest) seq_send(e, "lane %d 0 0 0", e->sq.mlane);
        } else {
            seq_send(e, "rest %d %d", e->sq.step, !(e->sq.q.rest >> e->sq.step & 1));
        }
        return;
    case B_TIE: {
        sq_note_t *n = sel_note(e);
        if (!sq_has(n)) return;
        n->flags ^= SQ_TIE;
        send_note(e, e->sq.step, e->sq.lane);
        return;
    }
    case B_ALL:   /* ALL: the values row and DELETE act on the whole step */
        e->sq.all = !e->sq.all;
        redraw(e);
        return;
    case B_DELETE:
        if (e->sq.view) seq_send(e, "off %d %d x", e->sq.mlane, e->sq.step);
        else if (e->sq.all) seq_send(e, "clearstep %d", e->sq.step);   /* DEL STEP: its notes, rest and offsets */
        else seq_send(e, "note %d %d -1 0 0 0 0", e->sq.step, e->sq.lane);
        return;
    case B_CLEAR:
        if (MessageBoxA(e->wnd, "Clear the whole sequence (its notes and its modulation lanes)?", "ONE-ish",
                        MB_YESNO | MB_ICONQUESTION) == IDYES)
            seq_send(e, "clear");
        return;
    case B_DUP: {   /* DUPLICATE X-Y: the selected step to the range end (or the whole length), after the length */
        int x = e->sq.range_to >= 0 ? (e->sq.step < e->sq.range_to ? e->sq.step : e->sq.range_to) : 0;
        int y = e->sq.range_to >= 0 ? (e->sq.step > e->sq.range_to ? e->sq.step : e->sq.range_to) : len - 1;
        if (len >= SQ_STEPS) { say(e, "The sequence is already 64 steps long: nothing to add."); return; }
        seq_send(e, "dup %d %d", x, y);
        say(e, "Steps %d-%d copied after step %d.", x + 1, y + 1, len);
        e->sq.range_to = -1;
        e->sq.range_arm = 0;
        return;
    }
    case B_SAVE: {
        char path[MAX_PATH] = "My Sequence";
        if (file_dialog(e, 1, path, sizeof path)) {
            char name[64];
            base_name(path, name, sizeof name);
            ask_notes(e, e->sq.notes);   /* (Skip keeps the notes as they were) */
            if (write_seq(e, path, name)) say(e, "Saved the sequence as \"%s\".", name);
            else say(e, "Couldn't save \"%s\" (Windows error %lu).", name, GetLastError());
        }
        return;
    }
    case B_LOAD: {
        char path[MAX_PATH] = "";
        if (file_dialog(e, 0, path, sizeof path)) load_path(e, path);
        return;
    }
    }
}

/* ---- the GEN view (2026-10-29): the sequence generator (src/seqgen.h) for the Synth in focus. Eight option rows on
 * the left (STYLE, RHYTHM and RANGE are one-click choices, the rest 0-100% bars), the focused Synth's pattern on the
 * right; KEY, SCALE (the Synth's own key_root / key_scale), LENGTH (seq_length) and SEED in the values row; GENERATE
 * (the options with the seed shown) and RE-ROLL (a new seed) in the buttons' row. One click = one undo step. ---- */
static const char *const GEN_LABELS[GEN_ROWS] = {"STYLE", "RHYTHM", "DENSITY", "RANGE", "VARIATION", "ACCENT", "FILLS",
                                                 "MODS"};
static const char *const GEN_STYLES[SG_STYLES] = {"BASS", "ARP", "LEAD", "STAB", "RAND"};   /* (melody, chords, random) */
static const char *const GEN_RHYTHMS[SG_RHYTHMS] = {"STRAIGHT", "SYNCO", "EUCLID"};
static const char *const GEN_RANGES[4] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
static int gen_synth(ed_t *e) { return e->focus >= 0 && e->focus < SQ_GEN_SYNTHS ? e->focus : 0; }
static seqgen_opts_t *gen_opts(ed_t *e) { return &e->sq.gen[gen_synth(e)]; }
static int gen_count(int row) { return row == 0 ? SG_STYLES : row == 1 ? SG_RHYTHMS : row == 3 ? 4 : 0; }   /* 0: a bar */
static int *gen_val(seqgen_opts_t *o, int row) {
    switch (row) {
    case 0: return &o->style;
    case 1: return &o->rhythm;
    case 2: return &o->density;
    case 3: return &o->range;
    case 4: return &o->variation;
    case 5: return &o->accent;
    case 6: return &o->fills;
    default: return &o->mods;
    }
}
static int gen_get(ed_t *e, int row) { return *gen_val(gen_opts(e), row) - (row == 3); }   /* (RANGE: 1-4 shown 0-3) */
static void gen_set(ed_t *e, int row, int v) {
    int n = gen_count(row);
    v = n ? clampi(v, 0, n - 1) : clampi(v, row == 2 ? 1 : 0, 100);
    *gen_val(gen_opts(e), row) = v + (row == 3);
    redraw(e);
}
static int gen_key(ed_t *e) { return P_KEY >= 0 ? option_of(e, P_KEY) : 0; }
static int gen_scale(ed_t *e) { return P_SCALE >= 0 ? option_of(e, P_SCALE) : KS_MINOR; }
static const char *gen_key_name(ed_t *e) { return P_KEY >= 0 ? PARAMS[P_KEY].opts[gen_key(e)] : "C"; }
static const char *gen_scale_name(ed_t *e) { return P_SCALE >= 0 ? PARAMS[P_SCALE].opts[gen_scale(e)] : "Minor"; }
static RECT gen_row_rect(int row) {   /* a row's label and its control */
    RECT r = {L.row[row].left, L.row[row].top, L.row[row].left + sc(GEN_CTL_R), L.row[row].bottom};
    return r;
}
static RECT gen_ctl_rect(int row) {
    RECT r = {L.row[row].left + sc(GEN_LABEL_W), L.row[row].top + sc(3), L.row[row].left + sc(GEN_CTL_R),
              L.row[row].bottom - sc(3)};
    return r;
}
static RECT gen_preview_rect(void) {
    RECT r = {L.row[0].left + sc(GEN_PREV_L), L.row[0].top + sc(3), L.row[0].right, L.row[ROWS - 1].bottom - sc(3)};
    return r;
}
static RECT gen_btn_rect(int which) {   /* GENERATE: the first two buttons' room; RE-ROLL: the next two */
    RECT a = btn_rect(2 * which), b = btn_rect(2 * which + 1);
    a.right = b.right;
    return a;
}
static int gen_at(int row, int x) {   /* the value a click at x picks on a row */
    RECT r = gen_ctl_rect(row);
    float t = (x - r.left) / (float)(rw(&r) > 0 ? rw(&r) : 1);
    int n = gen_count(row);
    return n ? clampi((int)(t * n), 0, n - 1) : clampi((int)lroundf(t * 100), 0, 100);
}

/* GENERATE (reroll 0: the seed shown, or a first one) or RE-ROLL (1: a new seed): a pattern from the options in the
 * focused Synth's KEY and SCALE, as long as its sequence, sent to its sequencer as one "load" (one undo step) */
static void gen_run(ed_t *e, int reroll) {
    uint32_t *seed = &e->sq.gen_seed[gen_synth(e)];
    if (reroll || !*seed) {
        uint32_t x = (*seed + 0x9E3779B9u) ^ (uint32_t)GetTickCount() * 2654435761u ^ (uint32_t)(uintptr_t)e;
        do { x ^= x << 13; x ^= x >> 17; x ^= x << 5; } while (!x);
        uint32_t n = x % 99999u + 1;   /* (a number short enough to note down) */
        *seed = n == *seed ? n % 99999u + 1 : n;
    }
    seqgen_opts_t o = *gen_opts(e);
    o.len = seq_len(e);
    uint32_t sd = *seed;
    sq_seq_t q;
    int played = seqgen_make(&o, gen_key(e), gen_scale(e), &sd, &q);
    static char t[SQ_TEXT_MAX];
    sq_encode(&q, t, sizeof t);
    lib_commit(e);   /* (an edit still open, a KEY change say, stays its own step) */
    lib_hold(e, 1);
    lib_touch(e, reroll ? "Re-roll" : "Generate");
    seq_send(e, "load %s", t);
    lib_hold(e, -1);
    say(e, "Synth %d: a %s pattern in %s %s, %d of %d steps playing (seed %u). RE-ROLL for another; UNDO undoes it.",
        e->focus + 1, GEN_STYLES[o.style], gen_key_name(e), gen_scale_name(e), played, o.len, (unsigned)*seed);
}
/* the randomiser's SEQUENCER tick (2026-10-29): RANDOMISE writes a new pattern into q (random generator options, wider
 * as WILD rises, in the key and scale the randomiser just picked); MUTATE keeps the notes, or with probability AMOUNT
 * makes new ones with the Synth's GEN options (always, if there are none). The options and seed land in the GEN view.
 * 1: q has new notes (its lanes are the randomiser's to fill next). */
static uint32_t rn_next(uint32_t *s) {
    uint32_t x = *s ? *s : 1;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static int rn_range(uint32_t *s, int lo, int hi) { return hi <= lo ? lo : lo + (int)(rn_next(s) % (uint32_t)(hi - lo + 1)); }
int seq_random_notes(ed_t *e, int mutate, float amount, float wild, int key, int scale, int len, uint32_t *seed,
                     sq_seq_t *q) {
    int has = 0;
    for (int s = 0; s < len && s < SQ_STEPS && !has; s++)
        for (int l = 0; l < SQ_NL && !(q->rest >> s & 1u); l++) has |= sq_has(&q->n[s][l]);
    if (mutate && has && (rn_next(seed) % 1000u) >= (uint32_t)(amount * 1000.0f)) return 0;
    seqgen_opts_t *o = gen_opts(e);
    if (!mutate) {
        int w = (int)lroundf(wild * 100.0f);
        o->style = rn_range(seed, 0, SG_STYLES - 1);
        o->rhythm = rn_range(seed, 0, SG_RHYTHMS - 1);
        o->density = rn_range(seed, 35 - w / 5, 80 + w / 5);
        o->range = rn_range(seed, 1, 2 + w / 50);
        o->variation = rn_range(seed, 10, 60 + w * 4 / 10);
        o->accent = rn_range(seed, 20, 80);
        o->fills = rn_range(seed, 0, 40 + w * 6 / 10);
        o->mods = rn_range(seed, 0, 50 + w / 2);
    }
    uint32_t *gs = &e->sq.gen_seed[gen_synth(e)];
    *gs = rn_next(seed) % 99999u + 1;
    seqgen_opts_t op = *o;
    op.len = len < 1 ? 1 : len > SQ_STEPS ? SQ_STEPS : len;
    uint32_t sd = *gs;
    seqgen_make(&op, key, scale, &sd, q);
    return 1;
}

/* the values row: KEY and SCALE (the focused Synth's params), LENGTH, SEED (a new one generates at once) */
static void gen_field_step(ed_t *e, int f, int d) {
    if (f == 0 && P_KEY >= 0) set_option(e, P_KEY, ((gen_key(e) + d) % 12 + 12) % 12);
    if (f == 1 && P_SCALE >= 0) set_option(e, P_SCALE, clampi(gen_scale(e) + d, 0, KS_COUNT - 1));
    if (f == 2 && SEQ_P[3] >= 0) set_option(e, SEQ_P[3], clampi(seq_len(e) + d, 1, SQ_STEPS));
    if (f == 3) {
        uint32_t *seed = &e->sq.gen_seed[gen_synth(e)];
        int v = (int)*seed + d;
        *seed = (uint32_t)(v < 1 ? 1 : v > 99999 ? 99999 : v);
        gen_run(e, 0);
    }
    redraw(e);
}

/* ---- the mouse and the keys ---- */
static void mod_paint(ed_t *e, int col, int row, int y) {   /* MODS: an offset from the height in the row */
    RECT r = cell_rect(col, row);
    int step = e->sq.page * SQ_PAGE + col, v = clampi((int)lroundf(((r.top + r.bottom) / 2.0f - y) / (rh(&r) / 2.0f - 2) * 100), -100, 100);
    sq_mlane_t *m = &e->sq.q.m[row];
    if (!m->dest) return;
    if (m->off[step] == v) return;
    m->off[step] = (int8_t)v;
    seq_send(e, "off %d %d %d", row, step, v);
}

/* a press on z at (x, y); 1: a drag follows (the editor captures the mouse) */
int seq_press(ed_t *e, int z, int x, int y, int shift) {
    e->sq.drag = -1;
    if (z >= Z_PLED && z < Z_PLED + 4) { seq_open(e, z - Z_PLED); return 0; }
    if (z >= Z_PAGEBTN && z < Z_PAGEBTN + 4) { e->sq.page = z - Z_PAGEBTN; redraw(e); return 0; }
    if (z >= Z_VIEW && z < Z_VIEW + NVIEWS) { e->sq.view = z - Z_VIEW; redraw(e); return 0; }
    if (z == Z_GEN_GO || z == Z_GEN_REROLL) { gen_run(e, z == Z_GEN_REROLL); return 0; }
    if (z >= Z_GEN_ROW && z < Z_GEN_ROW + GEN_ROWS) {   /* a choice, or a bar (a drag follows) */
        int row = z - Z_GEN_ROW;
        if (x < gen_ctl_rect(row).left) return 0;
        gen_set(e, row, gen_at(row, x));
        if (gen_count(row)) return 0;
        e->sq.drag = z;
        return 1;
    }
    if (z >= Z_GEN_FIELD && z < Z_GEN_FIELD + NFIELD) {   /* KEY, SCALE: a list; LENGTH: a drag; SEED: the wheel */
        int f = z - Z_GEN_FIELD;
        if (f < 2) {
            int param = f ? P_SCALE : P_KEY, c = pick_list(e, field_rect(e, f), param, f ? gen_scale(e) : gen_key(e));
            if (c >= 0) set_option(e, param, c);
            redraw(e);
            return 0;
        }
        if (f == 3) return 0;
        e->sq.drag = z;
        e->sq.drag_y = y;
        e->sq.drag_v0 = 0;
        return 1;
    }
    if (z == Z_RTZ) {   /* RETURN TO ZERO: step 1 next (and the selection there) */
        seq_send(e, "rtz");
        e->sq.step = e->sq.page = 0;
        say(e, "Return to zero: the sequence goes on from step 1.");
        return 0;
    }
    if (z == Z_AUTO) { e->sq.autoscroll = !e->sq.autoscroll; redraw(e); return 0; }
    if (z == Z_RANGE) {   /* lit: clear the range; off: the next step number clicked ends it */
        if (range_lit(e)) { e->sq.range_arm = 0; e->sq.range_to = -1; say(e, "No range: DUP copies the whole length."); }
        else { e->sq.range_arm = 1; say(e, "RANGE: now click the number of the last step to copy (step %d is the first).",
                                       e->sq.step + 1); }
        return 0;
    }
    if (z >= Z_BTN && z < Z_BTN + NBTN) { press_button(e, z - Z_BTN); return 0; }
    if (z >= Z_LANE && z < Z_LANE + ROWS) { select_cell(e, e->sq.step, z - Z_LANE); return 0; }
    if (z >= Z_STEPNUM && z < Z_STEPNUM + SQ_PAGE) {
        int step = e->sq.page * SQ_PAGE + z - Z_STEPNUM;
        if (shift || e->sq.range_arm) {   /* (RANGE lit, or SHIFT + click: the range's end) */
            e->sq.range_to = step;
            e->sq.range_arm = 0;
            int a = e->sq.step < step ? e->sq.step : step, b = e->sq.step < step ? step : e->sq.step;
            say(e, "Steps %d-%d marked: DUP copies them after the sequence's end.", a + 1, b + 1);
            return 0;
        }
        e->sq.range_to = -1;
        select_cell(e, step, e->sq.view ? e->sq.mlane : e->sq.lane);
        return 0;
    }
    if (z >= Z_FIELD && z < Z_FIELD + NFIELD) {
        int f = z - Z_FIELD;
        if (e->sq.view && f < 2) {   /* DEST, CTRL: a list */
            sq_mlane_t *m = sel_mlane(e);
            RECT r = field_rect(e, f);
            int c = pick_list(e, r, f ? P_SRC : P_DST, f ? m->ctl : m->dest);
            if (c >= 0) {
                int dest = f ? m->dest : c, ctl = f ? c : m->ctl;
                if (!dest && f) return 0;   /* a controller needs a lane: pick its DEST first */
                seq_send(e, "lane %d %d %d %d", e->sq.mlane, dest, ctl, m->amt);
            }
            return 0;
        }
        e->sq.drag = z;
        e->sq.drag_y = y;
        e->sq.drag_v0 = 0;
        return 1;
    }
    if (z >= Z_CELL && z < Z_CELL + SQ_PAGE * ROWS) {
        int col = (z - Z_CELL) / ROWS, row = (z - Z_CELL) % ROWS, step = e->sq.page * SQ_PAGE + col;
        e->sq.range_to = -1;
        if (e->sq.view) {
            e->sq.step = step;
            e->sq.mlane = row;
            mod_paint(e, col, row, y);
            e->sq.drag = z;
            return 1;
        }
        int again = step == e->sq.step && row == e->sq.lane;   /* (the cell selected already) */
        select_cell(e, step, row);
        if (!sq_has(sel_note(e))) {   /* empty: a second click puts a note there (as a double-click) */
            if (again) new_note(e, step, row, e->sq.last_note);
            return 0;
        }
        e->sq.drag = z;
        e->sq.drag_x = x;
        e->sq.drag_y = y;
        e->sq.drag_axis = 0;
        e->sq.drag_v0 = sel_note(e)->vel;
        e->sq.drag_v1 = sel_note(e)->gate;
        return 1;
    }
    return 0;
}
void seq_drag(ed_t *e, int x, int y, int shift) {
    int z = e->sq.drag;
    if (z < 0) return;
    if (z >= Z_GEN_ROW && z < Z_GEN_ROW + GEN_ROWS) { gen_set(e, z - Z_GEN_ROW, gen_at(z - Z_GEN_ROW, x)); return; }
    if (z >= Z_GEN_FIELD && z < Z_GEN_FIELD + NFIELD) {   /* LENGTH: 6 pixels a step */
        int d = (e->sq.drag_y - y) / sc(6);
        if (d != e->sq.drag_v0) { gen_field_step(e, z - Z_GEN_FIELD, d - e->sq.drag_v0); e->sq.drag_v0 = d; }
        return;
    }
    if (z >= Z_FIELD && z < Z_FIELD + NFIELD) {   /* a value: 6 pixels a step (SHIFT: finer for velocity/gate) */
        int d = (e->sq.drag_y - y) / (shift ? sc(12) : sc(6));
        if (d != e->sq.drag_v0) { field_step(e, z - Z_FIELD, d - e->sq.drag_v0); e->sq.drag_v0 = d; }
        return;
    }
    if (e->sq.view) {   /* MODS: paint offsets across the row */
        int row = (z - Z_CELL) % ROWS;
        float col = (x - grid_x0()) / cell_w();
        if (col >= 0 && col < SQ_PAGE) { e->sq.step = e->sq.page * SQ_PAGE + (int)col; mod_paint(e, (int)col, row, y); }
        return;
    }
    int dx = x - e->sq.drag_x, dy = e->sq.drag_y - y;   /* NOTES: up/down velocity, sideways gate */
    if (!e->sq.drag_axis && (abs(dx) > sc(4) || abs(dy) > sc(4))) e->sq.drag_axis = abs(dx) > abs(dy) ? 2 : 1;
    sq_note_t *n = sel_note(e);
    if (!sq_has(n) || !e->sq.drag_axis) return;
    int vel = n->vel, gate = n->gate;
    if (e->sq.drag_axis == 1) vel = clampi(e->sq.drag_v0 + dy * 127 / sc(80), 1, 127);
    else gate = clampi(e->sq.drag_v1 + dx * 100 / (int)(cell_w() * 1.5f + 1), 1, 100);
    if (vel == n->vel && gate == n->gate) return;
    n->vel = (uint8_t)vel;
    n->gate = (uint8_t)gate;
    send_note(e, e->sq.step, e->sq.lane);
    remember(e, n);
}
void seq_release(ed_t *e) { e->sq.drag = -1; }

void seq_right(ed_t *e, int z) {   /* right-click: delete, or a step's rest */
    if (z >= Z_STEPNUM && z < Z_STEPNUM + SQ_PAGE) {
        int step = e->sq.page * SQ_PAGE + z - Z_STEPNUM;
        seq_send(e, "rest %d %d", step, !(e->sq.q.rest >> step & 1));
    } else if (z >= Z_CELL && z < Z_CELL + SQ_PAGE * ROWS) {
        int col = (z - Z_CELL) / ROWS, row = (z - Z_CELL) % ROWS, step = e->sq.page * SQ_PAGE + col;
        if (e->sq.view) seq_send(e, "off %d %d x", row, step);
        else seq_send(e, "note %d %d -1 0 0 0 0", step, row);
    } else if (z == Z_FIELD && !e->sq.view) {
        seq_send(e, "note %d %d -1 0 0 0 0", e->sq.step, e->sq.lane);
    }
}
void seq_double(ed_t *e, int z) {   /* double-click an empty note cell: a new note there */
    if (z < Z_CELL || z >= Z_CELL + SQ_PAGE * ROWS || e->sq.view) return;
    int col = (z - Z_CELL) / ROWS, row = (z - Z_CELL) % ROWS, step = e->sq.page * SQ_PAGE + col;
    if (sq_has(&e->sq.q.n[step][row])) return;
    select_cell(e, step, row);
    new_note(e, step, row, e->sq.last_note);
}
int seq_wheel(ed_t *e, int z, int dir, int shift) {   /* 1: taken */
    if (z >= Z_GEN_ROW && z < Z_GEN_ROW + GEN_ROWS) {   /* a choice: the next; a bar: 5% (SHIFT: 1%) */
        int row = z - Z_GEN_ROW;
        gen_set(e, row, gen_get(e, row) + dir * (gen_count(row) || shift ? 1 : 5));
        return 1;
    }
    if (z >= Z_GEN_FIELD && z < Z_GEN_FIELD + NFIELD) { gen_field_step(e, z - Z_GEN_FIELD, dir); return 1; }
    if (z >= Z_FIELD && z < Z_FIELD + NFIELD) { field_step(e, z - Z_FIELD, dir * (shift && z == Z_FIELD ? 12 : 1)); return 1; }
    if (z >= Z_CELL && z < Z_CELL + SQ_PAGE * ROWS) {
        int col = (z - Z_CELL) / ROWS, row = (z - Z_CELL) % ROWS, step = e->sq.page * SQ_PAGE + col;
        if (e->sq.view) {
            e->sq.step = step;
            e->sq.mlane = row;
            if (e->sq.q.m[row].dest) field_step(e, 3, dir * (shift ? 1 : 5));
            return 1;
        }
        if (!sq_has(&e->sq.q.n[step][row])) return 1;
        if (step != e->sq.step || row != e->sq.lane) select_cell(e, step, row);
        field_step(e, 0, dir * (shift ? 12 : 1));
        return 1;
    }
    if (seq_zone(z) || e->screen == SCR_SEQ) {   /* elsewhere on the page: the pages */
        e->sq.page = clampi(e->sq.page - dir, 0, SQ_STEPS / SQ_PAGE - 1);
        redraw(e);
        return 1;
    }
    return 0;
}
int seq_key(ed_t *e, int vk, int shift) {   /* the arrow keys move the selection; Delete deletes. 1: taken */
    if (e->sq.view == V_GEN) return vk == VK_DELETE || vk == VK_BACK;   /* (GEN: nothing to delete) */
    int lanes = e->sq.view ? SQ_ML : SQ_NL, lane = e->sq.view ? e->sq.mlane : e->sq.lane;
    switch (vk) {
    case VK_LEFT: select_cell(e, (e->sq.step + SQ_STEPS - 1) % SQ_STEPS, lane); return 1;
    case VK_RIGHT: select_cell(e, (e->sq.step + 1) % SQ_STEPS, lane); return 1;
    case VK_UP: select_cell(e, e->sq.step, (lane + lanes - 1) % lanes); return 1;
    case VK_DOWN: select_cell(e, e->sq.step, (lane + 1) % lanes); return 1;
    case VK_PRIOR: case VK_NEXT: select_cell(e, (e->sq.step + (vk == VK_NEXT ? SQ_PAGE : SQ_STEPS - SQ_PAGE)) % SQ_STEPS, lane); return 1;
    case VK_DELETE: case VK_BACK:
        if (shift && !e->sq.view) seq_send(e, "clearstep %d", e->sq.step);   /* SHIFT + DELETE: the whole step */
        else press_button(e, B_DELETE);
        return 1;
    }
    return 0;
}

/* ---- drawing ---- */
static void txt(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt | DT_NOPREFIX | DT_SINGLELINE);
}
static void button(HDC dc, RECT r, const char *label, int lit, int hot) {
    gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), lit ? C_LCD : 0xFF0F2230);
    gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : lit ? C_LCD : 0xFF3E6273);
    txt(dc, r, label, font_cell, lit ? T_DARK : hot ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER);
}
static void field(HDC dc, ed_t *e, int f, const char *label, const char *value, int active) {
    RECT r = field_rect(e, f);
    int hot = e->hover == (e->sq.view == V_GEN ? Z_GEN_FIELD : Z_FIELD) + f;
    gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : 0xFF27404E);
    RECT lr = {r.left + sc(4), r.top, r.right, r.bottom}, vr = {r.left, r.top, r.right - sc(4), r.bottom};
    txt(dc, lr, label, font_tiny, T_DIM, DT_LEFT | DT_VCENTER);
    txt(dc, vr, value, font_cell, active ? (hot ? T_HOT : T_LCD) : T_DIM, DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
}

/* a note: a box from the step's start, as wide as its gate and as tall as its velocity (from the bottom), a brighter
 * edge where it starts, its name on top, a tick per ratchet under it, an arrow on into the next step when tied */
static void draw_note(HDC dc, ed_t *e, const sq_note_t *n, RECT c, int sel, int rest) {
    float x = c.left + 1.0f, y = c.top + 1.5f, w = rw(&c) - 2.0f, h = rh(&c) - 3.0f;
    float bw = w * n->gate / 100.0f, bh = h * (0.3f + 0.7f * n->vel / 127.0f);
    if (bw < 3) bw = 3;
    gfx_rect(x, y + h - bh, bw, bh, rest ? 0xFF1A2C36 : sel ? 0xFF3C8CA4 : 0xFF225568);
    gfx_rect(x, y + h - bh, 1.5f, bh, rest ? 0xFF3E5A68 : 0xFF9EE2F2);
    int r = sq_ratchet(n);
    for (int k = 0; k < r && r > 1; k++)   /* a tick per re-articulation along the bottom */
        gfx_rect(x + 2 + (bw - 4) * k / (r - 1), y + h - 2.2f * L.s, 1.6f, 2.2f * L.s, 0xFFF4F4E8);
    if (n->flags & SQ_TIE) {   /* an arrow to the next step */
        float ty = y + h * 0.35f;
        gfx_line(x + bw, ty, (float)c.right + 1, ty, 1.4f, 0xFFF4D27A);
        gpt_t a[3] = {{(float)c.right + 1, ty}, {(float)c.right - 3.0f * L.s, ty - 2.5f * L.s}, {(float)c.right - 3.0f * L.s, ty + 2.5f * L.s}};
        gfx_poly(a, 3, 0xFFF4D27A);
    }
    char nm[8];
    sq_note_name(n->note, nm, sizeof nm);
    RECT tr = {c.left + sc(3), c.top + sc(1), c.right + sc(6), c.top + sc(13)};
    txt(dc, tr, nm, font_tiny, rest ? T_DIM : sel ? T_HOT : RGB(222, 244, 250), DT_LEFT | DT_TOP);
    (void)e;
}

static void draw_mod(ed_t *e, const sq_mlane_t *m, int step, RECT c, int sel) {
    float cy = (c.top + c.bottom) / 2.0f, half = rh(&c) / 2.0f - 2, x = c.left + 3.0f, w = rw(&c) - 6.0f;
    if (!m->dest) return;
    gfx_line((float)c.left, cy, (float)c.right, cy, 1.0f, 0xFF27404E);
    if (m->off[step] == SQ_NO_OFF) { gfx_disc((c.left + c.right) / 2.0f, cy, 1.4f * L.s, 0xFF3E6273); return; }
    float v = m->off[step] / 100.0f * half;
    if (fabsf(v) < 1.5f) v = v < 0 ? -1.5f : 1.5f;
    gfx_rect(x, v > 0 ? cy - v : cy, w, fabsf(v), sel ? 0xFF96D7E8 : v > 0 ? 0xFF4F9CB2 : 0xFFB2784F);
    (void)e;
}

/* the GEN view's pattern, small: the focused Synth's every step across, its notes' span (at least an octave) up; the
 * key's root lines faint, beats and bars, the playing step */
static void gen_preview(HDC dc, ed_t *e) {
    RECT p = gen_preview_rect();
    gfx_rect((float)p.left, (float)p.top, (float)rw(&p), (float)rh(&p), 0xFF0A1822);
    gfx_frame((float)p.left, (float)p.top, (float)rw(&p), (float)rh(&p), 1.0f, 0xFF27404E);
    int len = e->sq.len < 1 ? 1 : e->sq.len, lo = 127, hi = 0, key = gen_key(e);
    for (int s = 0; s < len; s++)
        for (int l = 0; l < SQ_NL; l++)
            if (sq_has(&e->sq.q.n[s][l])) {
                int n = e->sq.q.n[s][l].note;
                if (n < lo) lo = n;
                if (n > hi) hi = n;
            }
    if (lo > hi) {
        txt(dc, p, "Empty: GENERATE fills it", font_tiny, T_DIM, DT_CENTER | DT_VCENTER);
        return;
    }
    if (hi - lo < 12) { lo -= (12 - (hi - lo)) / 2; hi = lo + 12; }
    float x0 = p.left + 3.0f, w = rw(&p) - 6.0f, y0 = p.top + 3.0f, h = rh(&p) - 6.0f, cw = w / len, nh = h / (hi - lo + 1);
    for (int n = lo; n <= hi; n++)
        if (((n - key) % 12 + 12) % 12 == 0)
            gfx_line(x0, y0 + (hi - n + 0.5f) * nh, x0 + w, y0 + (hi - n + 0.5f) * nh, 0.8f, 0xFF1E3E4C);
    for (int s = 4; s < len; s += 4)
        gfx_line(x0 + s * cw, y0, x0 + s * cw, y0 + h, s % 16 ? 0.5f : 1.0f, s % 16 ? 0xFF142834 : 0xFF27404E);
    if (e->sq.play >= 0 && e->sq.play < len) gfx_rect(x0 + e->sq.play * cw, y0, cw, h, 0x34FF6A50);
    for (int s = 0; s < len; s++)
        for (int l = 0; l < SQ_NL; l++) {
            const sq_note_t *n = &e->sq.q.n[s][l];
            if (!sq_has(n)) continue;
            float bw = (n->flags & SQ_TIE) ? cw : cw * n->gate / 100.0f, bh = nh < 2.0f ? 2.0f : nh;
            if (bw < 1.5f) bw = 1.5f;
            int rest = (int)(e->sq.q.rest >> s & 1);
            gfx_rect(x0 + s * cw, y0 + (hi - n->note) * nh, bw, bh, rest ? 0xFF2A4250 : n->vel > 104 ? 0xFF9EE2F2 : 0xFF4F9CB2);
            if (sq_ratchet(n) > 1) gfx_rect(x0 + s * cw, y0 + (hi - n->note) * nh + bh, bw, 1.2f, 0xFFF4F4E8);
        }
}

/* the GEN view under the title line: the options, the pattern, the values row and the buttons */
static void gen_draw(HDC dc, ed_t *e) {
    char b[96];
    RECT h1 = {L.head.left, L.head.top, L.head.left + sc(GEN_CTL_R), L.head.bottom};
    snprintf(b, sizeof b, "SEQUENCE GENERATOR: SYNTH %d", e->focus + 1);
    txt(dc, h1, b, font_tiny, T_DIM, DT_LEFT | DT_VCENTER);
    RECT h2 = {L.head.left + sc(GEN_PREV_L), L.head.top, L.head.right, L.head.bottom};
    snprintf(b, sizeof b, "%s %s, %d STEPS", gen_key_name(e), gen_scale_name(e), e->sq.len);
    txt(dc, h2, b, font_tiny, T_LCD, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    for (int row = 0; row < GEN_ROWS; row++) {
        int hot = e->hover == Z_GEN_ROW + row, n = gen_count(row), v = gen_get(e, row);
        RECT lr = {L.row[row].left, L.row[row].top, L.row[row].left + sc(GEN_LABEL_W - 4), L.row[row].bottom};
        txt(dc, lr, GEN_LABELS[row], font_tiny, hot ? T_HOT : T_DIM, DT_LEFT | DT_VCENTER);
        RECT c = gen_ctl_rect(row);
        if (n) {   /* one button per choice, the one picked lit */
            const char *const *names = row == 0 ? GEN_STYLES : row == 1 ? GEN_RHYTHMS : GEN_RANGES;
            float sw = (rw(&c) - (n - 1) * sc(2)) / (float)n;
            for (int k = 0; k < n; k++) {
                RECT sr = {c.left + (int)(k * (sw + sc(2))), c.top, c.left + (int)(k * (sw + sc(2)) + sw), c.bottom};
                button(dc, sr, names[k], k == v, 0);
            }
        } else {   /* a bar, 0-100% */
            gfx_rect((float)c.left, (float)c.top, (float)rw(&c), (float)rh(&c), 0xFF0F2230);
            gfx_rect((float)c.left, (float)c.top, rw(&c) * v / 100.0f, (float)rh(&c), 0xFF2F6F82);
            gfx_frame((float)c.left, (float)c.top, (float)rw(&c), (float)rh(&c), 1.0f, hot ? 0xFFFFFFFF : 0xFF3E6273);
            char pc[8];
            snprintf(pc, sizeof pc, "%d%%", v);
            RECT vr = {c.left, c.top, c.right - sc(4), c.bottom};
            txt(dc, vr, pc, font_cell, hot ? T_HOT : T_LCD, DT_RIGHT | DT_VCENTER);
        }
    }
    gen_preview(dc, e);
    RECT lr = {L.help.left, L.help.top, field_rect(e, 0).left - sc(4), L.help.top + sc(20)};
    snprintf(b, sizeof b, "SYNTH %d", e->focus + 1);
    txt(dc, lr, b, font_tiny, T_LCD, DT_LEFT | DT_VCENTER);
    char len[8], seed[12];
    uint32_t sd = e->sq.gen_seed[gen_synth(e)];
    snprintf(len, sizeof len, "%d", e->sq.len);
    if (sd) snprintf(seed, sizeof seed, "%u", (unsigned)sd);
    else snprintf(seed, sizeof seed, "--");
    field(dc, e, 0, "KEY", gen_key_name(e), 1);
    field(dc, e, 1, "SCALE", gen_scale_name(e), 1);
    field(dc, e, 2, "LENGTH", len, 1);
    field(dc, e, 3, "SEED", seed, sd != 0);
    button(dc, gen_btn_rect(0), "GENERATE", 1, e->hover == Z_GEN_GO);
    button(dc, gen_btn_rect(1), "RE-ROLL", 0, e->hover == Z_GEN_REROLL);
    char dup[24];
    snprintf(dup, sizeof dup, "DUP 1-%d", e->sq.len);
    const char *const GB[4] = {"CLEAR", dup, "SAVE", "LOAD"};
    for (int k = 0; k < 4; k++) button(dc, btn_rect(B_CLEAR + k), GB[k], 0, e->hover == Z_BTN + B_CLEAR + k);
}

void seq_draw(HDC dc, ed_t *e) {
    gfx_begin(dc);
    int len = e->sq.len < 1 ? 1 : e->sq.len, view = e->sq.view, page = e->sq.page;
    /* the title line: SEQUENCER, the views, the pages (lit: shown; a dot: playing; dim: past the length) */
    RECT t = {L.tabline.left, L.tabline.top, L.tabline.left + sc(94), L.tabline.bottom};
    if (view == V_GEN) txt(dc, t, "STEP EDIT", font_tiny, T_DIM, DT_LEFT | DT_VCENTER);
    else button(dc, range_rect(), "RANGE", range_lit(e), e->hover == Z_RANGE);   /* (2026-10-30) DUP's steps */
    static const char *const VIEWS[NVIEWS] = {"NOTES", "MODS", "GEN"};
    for (int v = 0; v < NVIEWS; v++) button(dc, view_rect(v), VIEWS[v], view == v, e->hover == Z_VIEW + v);
    for (int p = 0; p < SQ_STEPS / SQ_PAGE; p++) {
        RECT r = pagebtn_rect(p);
        char n[4];
        snprintf(n, sizeof n, "%d", p + 1);
        button(dc, r, n, p == page, e->hover == Z_PAGEBTN + p);
        if (p * SQ_PAGE >= len) gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 0x900B1621);
        if (e->sq.play >= 0 && e->sq.play / SQ_PAGE == p) gfx_disc((float)r.right - 3.0f * L.s, (float)r.top + 3.0f * L.s, 1.8f * L.s, 0xFFFF6A50);
    }
    button(dc, title_btn_rect(Z_RTZ), "RTZ", 0, e->hover == Z_RTZ);
    button(dc, title_btn_rect(Z_AUTO), "AUTO", e->sq.autoscroll, e->hover == Z_AUTO);
    RECT pr = {title_btn_rect(Z_AUTO).right + sc(6), L.tabline.top, pagebtn_rect(0).left - sc(4), L.tabline.bottom};
    char info[64];
    snprintf(info, sizeof info, "%s", e->sq.readout[0] ? e->sq.readout : "");
    txt(dc, pr, info, font_tiny, !strncmp(e->sq.readout, "Too", 3) ? RGB(255, 140, 110) : T_LCD, DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
    if (view == V_GEN) { gen_draw(dc, e); gfx_end(); return; }   /* the sequence generator */
    /* the columns: playing, selected, rests, past the length */
    for (int c = 0; c < SQ_PAGE; c++) {
        int step = page * SQ_PAGE + c, rest = (int)(e->sq.q.rest >> step & 1);
        RECT top = cell_rect(c, 0), bot = cell_rect(c, ROWS - 1), num = stepnum_rect(c);
        float x = (float)top.left, w = (float)rw(&top), y0 = (float)num.top, y1 = (float)bot.bottom;
        if (c % 4 == 0) gfx_line(x, y0, x, y1, 1.0f, 0xFF27404E);   /* beats */
        else gfx_line(x, (float)top.top, x, y1, 0.6f, 0xFF142834);
        if (step == e->sq.step) gfx_rect(x, (float)top.top, w, y1 - top.top, 0x2096D7E8);
        if (step == e->sq.play) gfx_rect(x, y0, w, y1 - y0, 0x34FF6A50);
        if (rest && !view)
            for (int k = 0; k < 6; k++) {   /* hatched */
                float yy = top.top + (y1 - top.top) * k / 6.0f;
                gfx_line(x + 1, yy + (y1 - top.top) / 6.0f, x + w - 1, yy, 1.0f, 0x50FF8070);
            }
        int in_range = e->sq.range_to >= 0 && ((step >= e->sq.step && step <= e->sq.range_to) || (step <= e->sq.step && step >= e->sq.range_to));
        if (in_range) gfx_rect(x, y0, w, (float)rh(&num), 0x40F4D27A);
        char n[4];
        snprintf(n, sizeof n, "%d", step + 1);
        txt(dc, num, n, font_tiny, step == e->sq.play ? T_HOT : rest && !view ? RGB(255, 128, 112) : step == e->sq.step ? T_LCD
            : e->hover == Z_STEPNUM + c ? T_HOT : T_DIM, DT_CENTER | DT_VCENTER);
    }
    /* the rows */
    for (int row = 0; row < ROWS; row++) {
        RECT lr = lane_rect(row);
        char lab[8];
        int sel_lane = view ? row == e->sq.mlane : row == e->sq.lane;
        snprintf(lab, sizeof lab, view ? "M%d" : "%d", row + 1);
        txt(dc, lr, lab, font_tiny, sel_lane ? T_LCD : e->hover == Z_LANE + row ? T_HOT : T_DIM, DT_LEFT | DT_VCENTER);
        gfx_line((float)grid_x0(), (float)L.row[row].bottom, (float)L.row[0].right, (float)L.row[row].bottom, 0.6f, 0xFF142834);
        if (view && e->sq.q.m[row].dest) {   /* the lane's destination, faint behind its bars */
            RECT dr = {grid_x0() + sc(3), L.row[row].top, L.row[0].right, L.row[row].bottom};
            txt(dc, dr, dest_name(e->sq.q.m[row].dest), font_tiny, RGB(40, 70, 84), DT_LEFT | DT_VCENTER);
        }
        for (int c = 0; c < SQ_PAGE; c++) {
            int step = page * SQ_PAGE + c, sel = step == e->sq.step && (sel_lane || (e->sq.all && !view));
            RECT cr = cell_rect(c, row);
            if (view) draw_mod(e, &e->sq.q.m[row], step, cr, sel);
            else if (sq_has(&e->sq.q.n[step][row])) draw_note(dc, e, &e->sq.q.n[step][row], cr, sel, (int)(e->sq.q.rest >> step & 1));
            if (sel) gfx_frame(cr.left + 0.5f, cr.top + 0.5f, rw(&cr) - 1.0f, rh(&cr) - 1.0f, 1.2f, 0xFFFFFFFF);
            else if (e->hover == Z_CELL + c * ROWS + row) gfx_frame(cr.left + 0.5f, cr.top + 0.5f, rw(&cr) - 1.0f, rh(&cr) - 1.0f, 1.0f, 0x8096D7E8);
            if (step >= len) gfx_rect((float)cr.left, (float)cr.top, (float)rw(&cr), (float)rh(&cr), 0xA00B1621);
        }
    }
    /* the selection's values */
    char lab[32], v[4][48];
    RECT lr = {L.help.left, L.help.top, field_rect(e, 0).left - sc(4), L.help.top + sc(20)};
    if (!view) {
        const sq_note_t *n = sel_note(e);
        int has = sq_has(n);
        if (e->sq.all) snprintf(lab, sizeof lab, "STEP %d ALL", e->sq.step + 1);
        else snprintf(lab, sizeof lab, "STEP %d.%d", e->sq.step + 1, e->sq.lane + 1);
        if (has) sq_note_name(n->note, v[0], sizeof v[0]); else snprintf(v[0], sizeof v[0], "--");
        snprintf(v[1], sizeof v[1], has ? "%d" : "--", n->vel);
        snprintf(v[2], sizeof v[2], has ? "%d%%" : "--", n->gate);
        snprintf(v[3], sizeof v[3], has ? "%d" : "--", has ? sq_ratchet(n) : 0);
        static const char *const F[NFIELD] = {"NOTE", "VEL", "GATE", "RATCH"};
        for (int f = 0; f < NFIELD; f++) field(dc, e, f, F[f], v[f], has || f == 0);
    } else {
        const sq_mlane_t *m = sel_mlane(e);
        snprintf(lab, sizeof lab, "LANE M%d", e->sq.mlane + 1);
        snprintf(v[0], sizeof v[0], "%s", m->dest ? dest_name(m->dest) : "(pick)");
        snprintf(v[1], sizeof v[1], "%s", src_name(m->ctl));
        snprintf(v[2], sizeof v[2], "%d%%", m->amt);
        if (m->dest && m->off[e->sq.step] != SQ_NO_OFF) snprintf(v[3], sizeof v[3], "%+d%%", m->off[e->sq.step]);
        else snprintf(v[3], sizeof v[3], "--");
        static const char *const F[NFIELD] = {"DEST", "CTRL", "AMT", "STEP"};
        for (int f = 0; f < NFIELD; f++) field(dc, e, f, F[f], v[f], m->dest || f == 0);
    }
    txt(dc, lr, lab, font_tiny, T_LCD, DT_LEFT | DT_VCENTER);
    /* the buttons */
    char dup[24];
    int x = e->sq.range_to >= 0 ? (e->sq.step < e->sq.range_to ? e->sq.step : e->sq.range_to) : 0;
    int y = e->sq.range_to >= 0 ? (e->sq.step > e->sq.range_to ? e->sq.step : e->sq.range_to) : len - 1;
    snprintf(dup, sizeof dup, "DUP %d-%d", x + 1, y + 1);
    const sq_note_t *sn = sel_note(e);
    const char *const NB[NBTN] = {"REST", "TIE", "ALL", e->sq.all ? "DEL STEP" : "DELETE", "CLEAR", dup, "SAVE", "LOAD"};
    const char *const MB[NBTN] = {"DEL LANE", "", "", "DELETE", "CLEAR", dup, "SAVE", "LOAD"};
    for (int b = 0; b < NBTN; b++) {
        if (view && (b == B_TIE || b == B_ALL)) continue;
        int lit = !view && ((b == B_REST && (e->sq.q.rest >> e->sq.step & 1)) || (b == B_TIE && sq_has(sn) && (sn->flags & SQ_TIE))
                            || (b == B_ALL && e->sq.all));
        button(dc, btn_rect(b), view ? MB[b] : NB[b], lit, e->hover == Z_BTN + b);
    }
    gfx_end();
}

/* the SEQUENCER module's page LEDs: the page playing lit (or, stopped, the one being edited, dimmer); pages past
 * LENGTH dark */
void seq_panel_draw(HDC dc, ed_t *e) {
    if (rw(&L.seq_leds) <= 0) return;
    gfx_begin(dc);
    skin_bind(gfx_g);   /* (the LEDs write pixels: skin.c flushes this surface first) */
    int len = seq_len(e), playing = e->sq.play >= 0, cur = playing ? e->sq.play / SQ_PAGE : e->sq.page;
    for (int p = 0; p < 4; p++) {
        RECT r = pled_rect(p);
        float cx = (r.left + r.right) / 2.0f, cy = r.top + sc(7), rr = 2.8f * L.s + 0.4f;
        int on = p == cur, past = p * SQ_PAGE >= len;
        skin_led(cx, cy, rr, on && !past ? (playing ? SK_LED_RED : 1) : 0);   /* (skin.c: the panel's LEDs) */
        if (past) gfx_disc(cx, cy, rr * 1.1f, 0xA0101012);   /* past LENGTH: darker still */
        char n[4];
        snprintf(n, sizeof n, "%d", p + 1);
        RECT tr = {r.left, r.top + sc(12), r.right, r.top + sc(26)};
        txt(dc, tr, n, font_tiny, e->hover == Z_PLED + p ? T_HOT : past ? RGB(80, 80, 84) : RGB(140, 140, 146), DT_CENTER | DT_TOP);
    }
    RECT lr = {L.seq_leds.left, L.seq_leds.bottom - sc(14), L.seq_leds.right, L.seq_leds.bottom};
    txt(dc, lr, "PAGE", font_label, RGB(214, 214, 216), DT_CENTER | DT_VCENTER);
    skin_bind(NULL);
    gfx_end();
}

/* ---- help ---- */
const char *seq_zone_help(ed_t *e, int z) {
    if (z >= Z_PLED && z < Z_PLED + 4) return "Page LEDs: the page of 16 steps playing (lit red) or being edited. Click one "
                                              "to edit that page in the step editor.";
    if (z >= Z_PAGEBTN && z < Z_PAGEBTN + 4) return "The four pages of 16 steps (a red dot: the page playing). LENGTH sets "
                                                    "how many steps play; steps past it are dimmed.";
    if (z == Z_RTZ) return "RTZ (return to zero): the sequence goes on from step 1 at its next step (stopped: it starts there).";
    if (z == Z_AUTO) return "AUTO (step edit auto-scroll): lit, the page shown follows the step playing. Off, the page stays "
                            "where you put it.";
    if (z == Z_RANGE) return range_lit(e)
        ? "RANGE (lit): click a step number to end the range at it, or RANGE again to clear the range (DUP then copies "
          "the whole length)."
        : "RANGE: choose the steps DUP copies. Select the first step (click its number), light RANGE, then click the "
          "last step's number. (Shortcut: SHIFT + click the last step's number.)";
    if (z == Z_VIEW) return "NOTES: the note lanes (a box per note: height = velocity, width = gate; ticks = ratchets, an arrow "
                            "= a tie).";
    if (z == Z_VIEW + 1) return "MODS: the modulation lanes: each a destination with a bipolar offset per step (drag across "
                                "a row to draw them). STEP MOD records them from the knobs.";
    if (z == Z_VIEW + V_GEN) return "GEN: the sequence generator. Pick a style and options, then GENERATE: a new sequence in "
                                    "the KEY and SCALE of the Synth in focus (each Synth has its own).";
    if (z >= Z_GEN_ROW && z < Z_GEN_ROW + GEN_ROWS) {
        static const char *const GH[GEN_ROWS] = {
            "STYLE: BASS (on the chord's root, fifths and octaves), ARP (through the chord's notes), LEAD (a melody: steps "
            "through the scale, chord notes on the beat), STAB (chords), RAND (anything in the scale).",
            "RHYTHM: STRAIGHT (the beats first), SYNCO (off-beat, a 3-3-2 feel) or EUCLID (the notes spread as evenly as "
            "they go).",
            "DENSITY: how many of the steps play (click or drag the bar; the wheel: 5%, SHIFT: 1%).",
            "RANGE: how many octaves the notes spread over.",
            "VARIATION: 0% repeats the first bar exactly; more lets later bars wander away from it (and brings in chord "
            "changes).",
            "ACCENT: 0% gives every note one velocity; more makes the beats louder and the off-beats softer.",
            "FILLS: ties (a note held into the next step) and ratchets (a note repeated inside its step), more of them "
            "at the end.",
            "MODS: modulation lanes: both filters' cutoff, then resonance, then one more (the MODS view shows them). 0%: "
            "none."};
        return GH[z - Z_GEN_ROW];
    }
    if (z >= Z_GEN_FIELD && z < Z_GEN_FIELD + NFIELD) {
        static const char *const GF[NFIELD] = {
            "KEY of the Synth in focus (its own: each Synth can play in another key). Click for the list, or scroll.",
            "SCALE (or mode) of the Synth in focus. Click for the list, or scroll.",
            "LENGTH: the sequence's steps (1-64), the same as the SEQUENCER module's. Drag or scroll.",
            "SEED: the number behind the pattern. The same seed and options give the same pattern again; scroll to step "
            "through seeds (each makes its pattern at once)."};
        return GF[z - Z_GEN_FIELD];
    }
    if (z == Z_GEN_GO) return "GENERATE: replaces the sequence of the Synth in focus with a new one from these options and "
                              "the seed shown (the first time: a new seed). The console's UNDO takes it back.";
    if (z == Z_GEN_REROLL) return "RE-ROLL: the same options with a new seed: another pattern. UNDO takes it back.";
    if (z >= Z_STEPNUM && z < Z_STEPNUM + SQ_PAGE) return e->sq.range_arm
        ? "Step number: click to end DUP's range here (RANGE is waiting for it)."
        : "Step number: click to select the step. RANGE (top left) then a click marks the end of the steps DUP copies; "
          "REST makes the selected step a rest. (Shortcuts: SHIFT + click, right-click.)";
    if (z >= Z_LANE && z < Z_LANE + ROWS) return "A lane: click to select it. Each note lane holds one note per step; a chord "
                                                 "takes several lanes.";
    if (z >= Z_CELL && z < Z_CELL + SQ_PAGE * ROWS) return e->sq.view
        ? "Click or drag up/down across the row to set each step's offset (middle = 0); DELETE removes the selected one "
          "(shortcut: right-click). Pick the "
          "lane's DEST first."
        : "Click to select, click an empty cell again for a new note; drag a note up/down for velocity, sideways for gate; "
          "the NOTE field (or the wheel) changes its note; DELETE removes it. With REC lit, play the keys. (Shortcuts: "
          "double-click, right-click.)";
    if (z >= Z_FIELD && z < Z_FIELD + NFIELD) {
        static const char *const NF[NFIELD] = {
            "NOTE of the selected step and lane: drag up/down or scroll (SHIFT: octaves); DELETE removes it. On an empty "
            "cell this makes a new note.",
            "VELOCITY of the selected note (1-127): drag or scroll.",
            "GATE: how long the note sounds, as a part of its step (1-100%). The next new note gets the last one set.",
            "RATCHET: plays the note 1 to 4 times inside its step (a stutter)."};
        static const char *const MF[NFIELD] = {
            "DEST: what this lane moves (the mod matrix's list). Off removes the lane.",
            "CTRL: a controller that scales the lane (None: full strength). With AMT 100%, the controller at its minimum "
            "silences the lane.",
            "AMT: how much the controller has its say (0%: none).",
            "The selected step's offset on this lane: drag or scroll (-100..+100%)."};
        return e->sq.view ? MF[z - Z_FIELD] : NF[z - Z_FIELD];
    }
    switch (z - Z_BTN) {
    case B_REST: return e->sq.view ? "DEL LANE: removes the selected modulation lane."
                                   : "REST: the selected step plays nothing (its notes are kept). Again to undo.";
    case B_TIE: return "TIE: the selected note carries on into the next step: the same note there goes on without a new attack, "
                       "another note takes over legato.";
    case B_ALL: return "ALL: the values row (NOTE moves every note of the step together; VEL, GATE, RATCH give them all the "
                       "same value) and DELETE (DEL STEP) act on the whole selected step. Again to edit one note.";
    case B_DELETE: return e->sq.all ? "DEL STEP: empties the selected step (its notes, its rest and its offsets). SHIFT + "
                                      "Delete does too."
                                    : "DELETE: removes the selected note (MODS: the selected step's offset). The Delete key "
                                      "does too. To empty the whole step: light ALL, then DEL STEP.";
    case B_CLEAR: return "CLEAR: removes every note and modulation lane (asks first).";
    case B_DUP: return "DUPLICATE: copies the steps shown after the end of the sequence and makes it that much longer (up to "
                       "64). To choose the steps: select the first, light RANGE (top left), click the last step's number.";
    case B_SAVE: return "SAVE: writes the sequence (its steps, length and rate) to a .oneseq file in Documents\\ONE-ish\\Sequences, "
                        "with performance notes if you write some (LOAD shows them).";
    case B_LOAD: return "LOAD: reads a .oneseq file into the sequencer (the sound stays as it is).";
    }
    return NULL;
}
void seq_help(ed_t *e, char *out, int n) {
    const char *m = seq_message(e);
    if (m) { snprintf(out, n, "%s", m); return; }
    if (e->sq.view == V_GEN) {
        snprintf(out, n, "Pick a STYLE and the options, then GENERATE: a pattern in Synth %d's key and scale. RE-ROLL for "
                 "another; NOTES to edit it; ON and PLAY (SEQUENCER, below) to hear it.", e->focus + 1);
        return;
    }
    if (e->sq.notes[0]) { snprintf(out, n, "Notes: %s", e->sq.notes); return; }   /* the sequence's performance notes */
    snprintf(out, n, "Select a step, light REC (SEQUENCER, below) and play keys: notes go in and the selection moves on. "
             "PLAY runs it (ON lets it play the synth); TRIG plays it while a key is held, TRANSP moves it with the keys. "
             "The SEQUENCER triangle: its settings.");
}

/* ---- load_test (TEST_SEQ) ---- */
intptr_t seq_test(ed_t *e, intptr_t op, char *buf) {
    switch (op) {
    case SQT_SAVE: return save_named(e, buf);
    case SQT_LOAD: {
        char dir[MAX_PATH], path[MAX_PATH + 64];
        if (!seq_dir(dir, sizeof dir, 0)) return 0;
        snprintf(path, sizeof path, "%s\\%s" EXT, dir, buf);
        return load_path(e, path);
    }
    case SQT_DIR: return seq_dir(buf, SQT_BUF, 0);
    case SQT_TEXT: seq_read(e); sq_encode(&e->sq.q, buf, SQT_BUF); return (intptr_t)strlen(buf);
    case SQT_MESSAGE: snprintf(buf, SQT_BUF, "%s", e->sq.msg); return 1;
    case SQT_SELECTED: return e->sq.step * 16 + e->sq.lane;
    case SQT_SET_NOTES: snprintf(e->sq.notes, sizeof e->sq.notes, "%s", buf); return 1;
    case SQT_NOTES: snprintf(buf, SQT_BUF, "%s", e->sq.notes); return 1;
    case SQT_PAGE: return e->sq.page;
    }
    return 0;
}
