/* Desktop editor window for ONE-ish (Windows VST2 build only; the MPC uses its own touchscreen skin).
 * A front panel laid out like the instrument it is inspired by: LFOs, oscillators, their modulation, noise and
 * polyphony on the left; a centre console with a screen (presets, the 16-row mod matrix, the modules' MORE pages, help
 * for the control under the mouse); mixer, filters, envelopes, effects and output on the right; and along the bottom a
 * left-hand controller (octave, glide, pitch and mod wheels, an X/Y pad, bend range), a ribbon and a 61-key keyboard
 * that play the synth. Wooden end cheeks. All artwork is drawn from scratch (editor_draw.c: GDI text, GDI+ shapes via
 * gfx.h, materials and 3D parts shaded per pixel by skin.c): no maker's names, marks, photos or image files. A change
 * repaints only the parts it touches (invalidate_param / invalidate_zone below). This file: the layout (scaled to fit the screen; ONEISH_EDITOR_SCALE
 * overrides it), the mouse, the MIDI from the window, and the VST2 side.
 *
 * RANDOM (the console button next to ANALOG) shows the patch randomiser on the screen (src/randomize.h): RANDOMISE makes
 * a new sound, MUTATE a variation of this one by AMOUNT, the lock buttons keep sections as they are, UNDO / REDO step
 * through the last RND_HIST sounds (the focused Synth's; ALL 3 SYNTHS or Shift+R / Shift+M change all three, R and M
 * are RANDOMISE and MUTATE). WILD sets how strange both get; the ARPEGGIATOR, GENERATIVE and SEQUENCER ticks add those
 * sections (SEQUENCER: its settings and modulation lanes; notes from the sequence generator, seq_random_notes in seq_page.c). Each
 * result is set param by param through the same path as a knob (setParameter,
 * audioMasterAutomate between begin/end edit), so the host and the window follow it.
 *
 * MORE pages, as on the instrument: a module with a small triangle in its top-right corner has second-layer controls;
 * a click on the triangle shows them on the centre screen (the MORE button lists every page, ANALOG opens the analog
 * character page). A page can show some items only for one option of another control (the synth effect's page lists
 * the controls of the selected effect type); a page with more than 16 items has sub-pages (< > on its title line).
 *
 * Patches (patches.c): the name line, < >, PREV/NEXT and the PRESET knob browse the factory presets then the owner's
 * own patch files; a click on the name opens the PATCHES page (save, save as, rename, delete, import, export).
 * The library (library.c): every edit from the window is an undo step (Ctrl+Z / Ctrl+Y; set_value, the step editor,
 * the randomiser and loads report to it), COMPARE and SNAPSHOT under PREV / NEXT, the SET, SETS, SNAPSHOTS and TAGS
 * pages next to PATCHES; MIDI program and bank change from the host load user patches and set slots (noted on the
 * host's thread, loaded on the window's: a hidden window's timer does it while the editor is closed).
 *
 * QUICK ASSIGN (quickmod.c): a DEST button (LFOs, envelopes, NOISE, GENERATIVE, the controller panel) or Alt-click /
 * right-click on a source, then a click on a knob connects them in the matrix and its drag sets the amount; the screen
 * lists the source's connections meanwhile (- / +, REMOVE, MATRIX); rings show what is modulated.
 *
 * (2026-10-30) Every feature works with a plain click on something visible; Shift-, Ctrl-, Alt-, double- and
 * right-clicks and the keys are shortcuts on top (docs/PLAN item 26 lists each with its visible way).
 *
 * It sits in front of the shared wrapper: wrapper/vst2_wrap.c is compiled with -DVSTPluginMain=one_ish_wrapped_main,
 * and this file's VSTPluginMain adds the editor opcodes and passes everything else on. Where each control sits comes
 * from desktop/gen_layout.py (build/desktop/layout.h), by parameter key. The editor talks to the plugin through the
 * VST2 calls a host would make (setParameter, getParameter, effGetParamDisplay, programs) and tells the host about
 * every change (audioMasterAutomate).
 *
 * Playing from the window: the keyboard, wheels, pad and ribbon make MIDI on the UI thread. It goes into a small
 * lock-free queue per instance (one writer, the UI thread; one reader, the audio thread), which this file's
 * processReplacing empties into the wrapper's effProcessEvents just before each block, on the audio thread, as a host
 * would. No allocation and no lock is added to the audio path. The ribbon sends CC 16 (position, left 0 .. right 127)
 * and CC 17 (127 while touched, 0 on release); the engine decides what they do (spring or hold). The X/Y pad sends
 * CC 2 (X), CC 3 (Y) and CC 18 (127 while touched, 0 on release), the matrix sources Pad X, Pad Y and Pad Gate; Y
 * also sends aftertouch (back to 0 on release), as it always did. */
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "library.h"
#include "hostrate.h"
#include "skin.h"
#include "../src/randomize.h"

layout_t L;
HFONT font_title, font_label, font_value, font_tiny, font_screen, font_cell, font_big, font_brand;
const short MXC[MX_COLS + 2] = {0, 14, 70, 144, 194, 252, 294, 344, 384};   /* #, src .. xf_amt, the end */
/* Instances' editor state. A static pool, never freed: each audio thread looks its instance up here (ed_of, before
 * every block), and another instance closing must not free memory that scan might be reading. */
static ed_t pool[MAX_INSTANCES];
static ed_t *eds[MAX_INSTANCES];
static dispatcher_t wrapped_dispatch;
static process_t wrapped_replacing, wrapped_process;
static HINSTANCE dll;
static const char *CLASS_NAME = "ONEishEditor";

intptr_t wrapped_call(AEffect *fx, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt) {
    return wrapped_dispatch(fx, op, idx, v, ptr, opt);
}

static ed_t *ed_of(AEffect *fx) {
    for (int i = 0; i < MAX_INSTANCES; i++)
        if (eds[i] && eds[i]->fx == fx) return eds[i];
    return NULL;
}
void ed_each(void (*fn)(ed_t *)) {   /* UI thread */
    for (int i = 0; i < MAX_INSTANCES; i++)
        if (eds[i]) fn(eds[i]);
}

/* the MIDI channel the window plays on (0-15): the one the instrument listens on (MIDI CHANNEL; Omni: channel 1; Per
 * Synth: the focused Synth's own, so the keys play the Synth on the panel) */
static int ui_channel(ed_t *e) {
    static int k = -2, ks = -2;
    if (k == -2) k = rnd_key("midi_ch") < NPARAMS ? rnd_key("midi_ch") : -1;
    if (ks == -2) ks = rnd_key("syn_ch") < NPARAMS ? rnd_key("syn_ch") : -1;
    if (k < 0) return 0;
    int o = (int)lroundf(e->fx->getParameter(e->fx, k) * (PARAMS[k].nopts - 1));
    if (o == 17 && ks >= 0) {   /* Per Synth */
        int s = synth_param(ks, e->focus), c = (int)lroundf(e->fx->getParameter(e->fx, s) * (PARAMS[s].nopts - 1));
        return c >= 1 && c <= 16 ? c - 1 : 0;
    }
    return o >= 1 && o <= 16 ? o - 1 : 0;
}

/* ---- MIDI from the window: the UI thread writes, the audio thread reads (no locks, no allocation) ---- */
static void ui_midi_on(ed_t *e, int status, int d1, int d2);
static void ui_midi(ed_t *e, int status, int d1, int d2) {   /* on the window's channel */
    ui_midi_on(e, status < 0xF0 ? status | ui_channel(e) : status, d1, d2);
}
static void ui_midi_on(ed_t *e, int status, int d1, int d2) {   /* on the channel in status */
    unsigned w = e->mq_w, r = __atomic_load_n(&e->mq_r, __ATOMIC_ACQUIRE);
    if (w - r >= MQ) return;   /* full (the host isn't processing): drop */
    e->mq[w % MQ][0] = (unsigned char)status;
    e->mq[w % MQ][1] = (unsigned char)(d1 & 127);
    e->mq[w % MQ][2] = (unsigned char)(d2 & 127);
    __atomic_store_n(&e->mq_w, w + 1, __ATOMIC_RELEASE);
}

static void drain_ui_midi(ed_t *e, AEffect *fx) {   /* audio thread, before each block */
    if (!e) return;
    unsigned r = e->mq_r, w = __atomic_load_n(&e->mq_w, __ATOMIC_ACQUIRE);
    if (r == w) return;
    VstMidiEvent ev[64];
    struct { int32_t n; intptr_t reserved; VstMidiEvent *ev[64]; } list;
    int n = 0;
    for (; r != w && n < 64; r++, n++) {
        memset(&ev[n], 0, sizeof ev[n]);
        ev[n].type = 1;
        ev[n].byteSize = sizeof ev[n];
        memcpy(ev[n].midiData, e->mq[r % MQ], 3);
        list.ev[n] = &ev[n];
        memcpy(e->mlog[e->mlog_n++ % MLOG], e->mq[r % MQ], 3);   /* a copy for load_test (TEST_MIDI_LOG) */
    }
    __atomic_store_n(&e->mq_r, r, __ATOMIC_RELEASE);
    list.n = n;
    list.reserved = 0;
    wrapped_dispatch(fx, effProcessEvents, 0, 0, &list, 0);
}

/* the host's rate and float output (hostrate.c): the wrapper renders whole engine blocks, converted to the host's
 * rate, and the host's audio input converted to 44.1 kHz for the engine; the old path only for load_test's comparison
 * (TEST_LEGACY: no input) */
static void hooked_replacing(AEffect *fx, float **in, float **out, int32_t n) {
    ed_t *e = ed_of(fx);
    drain_ui_midi(e, fx);
    if (e && e->hr && hr_on(e->hr)) hr_process(e->hr, fx, wrapped_replacing, in, out, n, 0);
    else wrapped_replacing(fx, in, out, n);
}
static void hooked_process(AEffect *fx, float **in, float **out, int32_t n) {
    ed_t *e = ed_of(fx);
    drain_ui_midi(e, fx);
    if (e && e->hr && hr_on(e->hr)) hr_process(e->hr, fx, wrapped_replacing, in, out, n, 1);
    else wrapped_process(fx, in, out, n);
}

/* ---- layout ---- */
static void layout(void) {
    if (L.ready) return;
    RECT work;
    float s = 1.0f;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {   /* fit the screen, never larger than base size */
        float sx = (rw(&work) - 24) / (float)BASE_W, sy = (rh(&work) - 70) / (float)BASE_H;
        s = sx < sy ? sx : sy;
        if (s > 1.0f) s = 1.0f;
        if (s < 0.5f) s = 0.5f;
    }
    const char *force = getenv("ONEISH_EDITOR_SCALE");   /* e.g. 1.5 on a big screen, 0.8 to test a small one */
    if (force && atof(force) >= 0.5 && atof(force) <= 2.0) s = (float)atof(force);
    L.s = s;
    L.w = sc(BASE_W);
    L.h = sc(BASE_H);
    for (int c = 0; c < NCTLS; c++) {
        const lay_ctl_t *k = &CTLS[c];
        int i = k->param;
        SetRect(&L.ctl[i], sc(k->x), sc(k->y), sc(k->x + k->w), sc(k->y + k->h));
        L.kind[i] = k->kind;
        L.size[i] = k->kind == K_TOGGLE ? k->size : sc(k->size);   /* a toggle's size: 1 = label beside it */
        L.label[i] = k->label;
        L.leds[i] = k->leds;
    }
    /* the screen: preset line, title line (with < > for sub-pages), column heads or a note, 8 rows, help */
    L.screen = zone(ZONE_SCREEN);
    int x0 = L.screen.left + sc(8), x1 = L.screen.right - sc(8), y = L.screen.top + sc(6);
    SetRect(&L.prev, x0, y, x0 + sc(26), y + sc(34));
    SetRect(&L.next, x1 - sc(26), y, x1, y + sc(34));
    SetRect(&L.name, x0 + sc(28), y, x1 - sc(28), y + sc(34));
    y += sc(36);
    SetRect(&L.tabline, x0, y, x1, y + sc(18));
    SetRect(&L.sub_next, x1 - sc(18), y, x1, y + sc(18));
    SetRect(&L.sub_prev, x1 - sc(64), y, x1 - sc(46), y + sc(18));
    y += sc(20);
    SetRect(&L.head, x0, y, x1, y + sc(18));
    y += sc(18);
    for (int r = 0; r < ROWS; r++, y += sc(22)) SetRect(&L.row[r], x0, y, x1, y + sc(22));
    SetRect(&L.help, x0, y + sc(8), x1, L.screen.bottom - sc(6));
    L.tab_matrix = zone(ZONE_TAB_MATRIX);
    L.tab_more = zone(ZONE_TAB_MORE);
    L.tab_analog = zone(ZONE_TAB_ANALOG);
    L.tab_random = zone(ZONE_TAB_RANDOM);
    L.tab_seq = zone(ZONE_TAB_SEQ);
    L.focus[0] = zone(ZONE_FOCUS1);
    L.focus[1] = zone(ZONE_FOCUS2);
    L.focus[2] = zone(ZONE_FOCUS3);
    L.tab_setup = zone(ZONE_TAB_SETUP);
    L.tab_settings = zone(ZONE_TAB_SETTINGS);
    L.seq_leds = zone(ZONE_SEQ_LEDS);
    L.encoder = zone(ZONE_ENCODER);
    L.bprev = zone(ZONE_PREV);
    L.bnext = zone(ZONE_NEXT);
    L.brand = zone(ZONE_BRAND);
    /* COMPARE and SNAPSHOT (library.c) under PREV and NEXT, where the console's name was */
    /* (a little further apart than PREV and NEXT: their names are longer) */
    SetRect(&L.compare, sc(ZONE_PREV[0] - 9), sc(ZONE_FOCUS1[1]), sc(ZONE_PREV[0] + ZONE_PREV[2] - 9), sc(ZONE_FOCUS1[1] + ZONE_FOCUS1[3]));
    SetRect(&L.snapshot, sc(ZONE_NEXT[0] - 2), sc(ZONE_FOCUS1[1]), sc(ZONE_NEXT[0] + ZONE_NEXT[2] - 2), sc(ZONE_FOCUS1[1] + ZONE_FOCUS1[3]));
    SetRectEmpty(&L.brand);
    L.oct_leds = zone(ZONE_OCT_LEDS);
    L.oct_down = zone(ZONE_OCT_DOWN);
    L.oct_up = zone(ZONE_OCT_UP);
    L.pitch = zone(ZONE_PITCH);
    L.modw = zone(ZONE_MODW);
    L.pad = zone(ZONE_PAD);
    L.keys = zone(ZONE_KEYS);
    L.ribbon = zone(ZONE_RIBBON);
    L.euclid = zone(ZONE_EUCLID);
    L.tab_gen = zone(ZONE_TAB_GEN);   /* the console's third row (hub.c) */
    L.gen_off = zone(ZONE_GEN_OFF);
    L.tempo = zone(ZONE_TEMPO);
    L.tempo_host = zone(ZONE_TEMPO_HOST);
    L.tempo_own = zone(ZONE_TEMPO_OWN);
    L.vol_master = zone(ZONE_VOL_MASTER);
    L.together = zone(ZONE_TOGETHER);   /* (2026-10-30) TOGETHER, UNDO, REDO (hub.c); the controller's DEST */
    L.undo = zone(ZONE_UNDO);
    L.redo = zone(ZONE_REDO);
    L.qm_hands = zone(ZONE_QM_HANDS);
    L.kb_hint = zone(ZONE_KB_HINT);
    L.panic = zone(ZONE_PANIC);
    for (int k = 0; k < NMORES; k++) {
        const short z[4] = {MORES[k].x, MORES[k].y, MORES[k].w, MORES[k].h};
        L.more[k] = zone(z);
    }
    L.ready = 1;
}

/* a page's slot: 8 rows in two columns; the name on the left of each half, its control on the right */
void slot_rects(int slot, RECT *name, RECT *ctl) {
    int r = slot % ROWS, half = (rw(&L.row[0]) - sc(8)) / 2, x = L.row[0].left + (slot / ROWS) * (half + sc(8));
    SetRect(name, x, L.row[r].top, x + half * 56 / 100, L.row[r].bottom);
    SetRect(ctl, name->right + sc(4), L.row[r].top + sc(2), x + half, L.row[r].bottom - sc(2));
}

RECT matrix_cell(int r, int col) {
    RECT c = {L.row[r].left + sc(MXC[col]), L.row[r].top + sc(2), L.row[r].left + sc(MXC[col + 1]) - sc(4),
              L.row[r].bottom - sc(2)};
    return c;
}

/* the randomiser page: RANDOMISE and MUTATE side by side (rows 0-1), the WILD and AMOUNT bars under them (row 2), the
 * column titles (row 3), then three columns (rows 4-7): the locks (four, then three with UNDO / REDO under them) and
 * what RANDOMISE also changes (the ARPEGGIATOR, GENERATIVE and SEQUENCER ticks, ALL 3 SYNTHS) */
RECT rnd_col(int col) {   /* column col (0-2) of rows 4-7, its left and right edges */
    int cw = (rw(&L.row[0]) - 2 * sc(8)) / 3, x = L.row[0].left + col * (cw + sc(8));
    RECT r = {x, L.row[4].top, x + cw, L.row[7].bottom};
    return r;
}
RECT rnd_rect(int z) {
    int half = (rw(&L.row[0]) - sc(8)) / 2, xl = L.row[0].left, xr = xl + half + sc(8);
    RECT r = {0, 0, 0, 0};
    if (z == Z_RND_GO || z == Z_RND_MUTATE) {
        int x = z == Z_RND_GO ? xl : xr;
        SetRect(&r, x, L.row[0].top + sc(3), x + half, L.row[1].bottom - sc(3));
    } else if (z == Z_RND_AMOUNT || z == Z_RND_WILD) {   /* a bar between its name and its value */
        int x = z == Z_RND_WILD ? xl : xr;
        SetRect(&r, x + sc(54), L.row[2].top + sc(4), x + half - sc(40), L.row[2].bottom - sc(4));
    } else if (z >= Z_RND_LOCK && z < Z_RND_LOCK + RS_COUNT) {
        int k = z - Z_RND_LOCK, row = 4 + k % 4;
        RECT c = rnd_col(k / 4);
        SetRect(&r, c.left, L.row[row].top + sc(2), c.right, L.row[row].bottom - sc(2));
    } else if (z == Z_RND_UNDO || z == Z_RND_REDO) {
        RECT c = rnd_col(1);
        int q = (rw(&c) - sc(6)) / 2, x = c.left + (z == Z_RND_REDO ? q + sc(6) : 0);
        SetRect(&r, x, L.row[7].top + sc(2), x + q, L.row[7].bottom - sc(2));
    } else if (z >= Z_RND_ALSO && z < Z_RND_ALSO + 3) {
        RECT c = rnd_col(2);
        int row = 4 + z - Z_RND_ALSO;
        SetRect(&r, c.left, L.row[row].top + sc(2), c.right, L.row[row].bottom - sc(2));
    } else if (z == Z_RND_ALL) {
        RECT c = rnd_col(2);
        SetRect(&r, c.left, L.row[7].top + sc(2), c.right, L.row[7].bottom - sc(2));
    }
    return r;
}

/* SYNTH c's button (the Panel Focus): its select cap on the left, the M | S split cap beside it (MUTE: the Synth's ON
 * off, red; SOLO: only the soloed Synths heard, lit); the label under both */
RECT focus_part(int c, int part) {
    RECT z = L.focus[c], r;
    int top = z.top + sc(6), bh = sc(13), x = z.left + sc(2), cap = sc(22), half = sc(9);
    if (part == 0) SetRect(&r, x, top, x + cap, top + bh);
    else {
        int x0 = x + cap + sc(2) + (part == 2 ? half + sc(1) : 0);
        SetRect(&r, x0, top, x0 + half, top + bh);
    }
    return r;
}
static int syn_key(int c, int solo) { return synth_param(rnd_key(solo ? "syn_solo" : "syn_on"), c); }
int synth_muted(ed_t *e, int c) { return e->fx->getParameter(e->fx, syn_key(c, 0)) < 0.5f; }
int synth_soloed(ed_t *e, int c) { return e->fx->getParameter(e->fx, syn_key(c, 1)) > 0.5f; }
static void edit_abs(ed_t *e, int i, float v);
static void synth_toggle(ed_t *e, int c, int solo) {   /* M (the Synth's ON) or S, as a click on the SETUP page would */
    int i = syn_key(c, solo);
    edit_abs(e, i, e->fx->getParameter(e->fx, i) > 0.5f ? 0.0f : 1.0f);
}

static int square_corners(HWND wnd);   /* (below: the host window's corners) */

/* ---- what to repaint: only the parts a change touches (the whole window takes longer to paint) ---- */
static void invalidate(ed_t *e, const RECT *r) {
    if (e->wnd) InvalidateRect(e->wnd, r, FALSE);
}
static int on_screen(ed_t *e, int i) {   /* does the centre screen's current page show param i (or depend on it)? */
    if (e->screen == SCR_MATRIX) {
        for (int m = e->sub * ROWS; m < (e->sub + 1) * ROWS && m < NMATRIX; m++)
            for (int k = 0; k < MX_COLS; k++)
                if (MATRIX[m][k] == i || MATRIX_ON[m] == i) return 1;
        return 0;
    }
    if (e->screen == SCR_INDEX || lib_screen(e->screen)) return 0;   /* (the patch list, the library's pages) */
    if (e->screen == SCR_SETUP || e->screen == SCR_SETTINGS) return 1;   /* (every Synth's setup: any change may show) */
    if (e->screen < 0 || e->screen >= NPAGES) return 1;   /* RANDOM (it describes the sound), SEQ: all of it */
    for (int k = PAGES[e->screen].first; k < PAGES[e->screen].first + PAGES[e->screen].count; k++)
        if (ITEMS[k].param == i || ITEMS[k].cond == i) return 1;   /* (a condition changes which items show) */
    return 0;
}
static void invalidate_param(ed_t *e, int i) {   /* a value changed: its control, the screen, what shows it */
    if (i < 0 || i >= NPARAMS) { invalidate(e, NULL); return; }
    qm_matrix_changed(e, i);   /* a matrix row: the knobs' modulation rings (quickmod.c) */
    if (i == rnd_key("syn_on") || i == rnd_key("syn_solo"))   /* the SYNTH buttons' M | S */
        for (int c = 0; c < NSYNTHS; c++) invalidate(e, &L.focus[c]);
    if (i == master_param() && rnd_key("volume") < NPARAMS) {   /* MASTER: the VOLUME knob may show it */
        RECT r = L.ctl[rnd_key("volume")];
        InflateRect(&r, sc(16), sc(6));
        invalidate(e, &r);
    }
    for (int k = 0; k < NHUB; k++)   /* a generator's ON: GEN OFF's name (GEN BACK) may change */
        if (HUB[k].on == i) { RECT g = L.gen_off; InflateRect(&g, sc(12), sc(4)); invalidate(e, &g); }
    if (!IsRectEmpty(&L.ctl[i])) {   /* on the panel: the control, its label and value (they spill a little) */
        RECT r = L.ctl[i];
        InflateRect(&r, sc(16), sc(6));
        invalidate(e, &r);
    }
    if (on_screen(e, i)) invalidate(e, &L.screen);   /* the matrix and MORE pages show values */
    else {   /* the help lines (the value of the control under the mouse) */
        RECT h = L.help;
        InflateRect(&h, sc(2), sc(6));
        invalidate(e, &h);
    }
    for (int w = 0; w < NWAVES; w++)
        for (int k = 0; k < 4; k++)
            if (WAVES[w].param[k] == i) {
                RECT r = {sc(WAVES[w].x), sc(WAVES[w].y), sc(WAVES[w].x + WAVES[w].w), sc(WAVES[w].y + WAVES[w].h)};
                invalidate(e, &r);
            }
    for (int k = 0; k < 4; k++)
        if (EUCLID_P[k] == i) {
            RECT r = L.euclid;
            InflateRect(&r, sc(6), sc(6));
            invalidate(e, &r);
        }
}
static void invalidate_zone(ed_t *e, int z) {   /* the hover moved onto or off z: it and the help line */
    if (z < 0) return;
    if (z < NPARAMS) { invalidate_param(e, z); return; }
    const RECT *r = NULL;
    switch (z) {
    case Z_KEYS: r = &L.keys; break;
    case Z_RIBBON: r = &L.ribbon; break;
    case Z_PITCH: r = &L.pitch; break;
    case Z_MODW: r = &L.modw; break;
    case Z_PAD: r = &L.pad; break;
    case Z_OCT_DOWN: r = &L.oct_down; break;
    case Z_OCT_PICK: r = &L.oct_leds; break;
    case Z_PANIC: r = &L.panic; break;
    case Z_OCT_UP: r = &L.oct_up; break;
    case Z_PREV: case Z_NEXT: invalidate(e, &L.bprev); r = &L.bnext; break;
    case Z_ENCODER: r = &L.encoder; break;
    case Z_TAB_MATRIX: r = &L.tab_matrix; break;
    case Z_TAB_MORE: r = &L.tab_more; break;
    case Z_TAB_ANALOG: r = &L.tab_analog; break;
    case Z_TAB_RANDOM: r = &L.tab_random; break;
    case Z_TAB_SEQ: r = &L.tab_seq; break;
    case Z_FOCUS1: case Z_FOCUS2: case Z_FOCUS3: r = &L.focus[z - Z_FOCUS1]; break;
    case Z_MUTE1: case Z_MUTE2: case Z_MUTE3: r = &L.focus[z - Z_MUTE1]; break;
    case Z_SOLO1: case Z_SOLO2: case Z_SOLO3: r = &L.focus[z - Z_SOLO1]; break;
    case Z_TAB_SETUP: r = &L.tab_setup; break;
    case Z_TAB_SETTINGS: r = &L.tab_settings; break;
    case Z_EUCLID: r = &L.euclid; break;
    case Z_COMPARE: r = &L.compare; break;
    case Z_SNAPSHOT: r = &L.snapshot; break;
    case Z_TAB_GEN: r = &L.tab_gen; break;   /* (the console's third row, hub.c) */
    case Z_GEN_OFF: r = &L.gen_off; break;
    case Z_VOL_MASTER: invalidate(e, &L.vol_master); r = &L.ctl[rnd_key("volume") < NPARAMS ? rnd_key("volume") : 0]; break;
    case Z_TOGETHER: for (int c = 0; c < NSYNTHS; c++) invalidate(e, &L.focus[c]); r = &L.together; break;
    case Z_UNDO: r = &L.undo; break;
    case Z_REDO: r = &L.redo; break;
    case Z_TEMPO: case Z_TEMPO_HOST: case Z_TEMPO_OWN: case Z_TEMPO_DOWN: case Z_TEMPO_UP:
        invalidate(e, &L.tempo_host); invalidate(e, &L.tempo_own);
        r = &L.tempo; break;
    }
    if (seq_zone(z)) r = &L.seq_leds;   /* the step editor (on the screen) or the SEQUENCER's page LEDs */
    if (z >= Z_PAGE && z < Z_PAGE + NPAGES)   /* its triangle(s) */
        for (int k = 0; k < NMORES; k++)
            if (MORES[k].page == z - Z_PAGE) {
                RECT t = L.more[k];
                InflateRect(&t, sc(6), sc(6));
                invalidate(e, &t);
            }
    if (r) {
        RECT g = *r;
        InflateRect(&g, sc(20), sc(18));   /* labels under the parts */
        invalidate(e, &g);
    }
    invalidate(e, &L.screen);   /* everything else lives on the screen; the help line is there too */
}

/* ---- PANEL FOCUS (multitimbral): the panel's controls are Synth 1's params ("o1_wave"); the focused Synth's own copy
 * ("s2_o1_wave") is read and written in their place. Shared params (the master effects, tuning, settings) have one
 * value for all three. ---- */
static short FMAP[NSYNTHS][NPARAMS], FBASE[NPARAMS];   /* panel index -> Synth c's index; any index -> the panel's */
static signed char FSYN[NPARAMS];                      /* the Synth an index belongs to */
static unsigned char FSHARED[NPARAMS];
static int fmap_ready;
static void fmap_init(void) {   /* UI thread; the same for every instance */
    if (fmap_ready) return;
    for (int i = 0; i < NPARAMS; i++) {
        for (int c = 0; c < NSYNTHS; c++) FMAP[c][i] = (short)i;
        FBASE[i] = (short)i;
        FSYN[i] = 0;
    }
    for (int i = 0; i < NPARAMS; i++) {
        const char *k = PARAMS[i].key;
        if (k[0] != 's' || k[1] < '2' || k[1] > '0' + NSYNTHS || k[2] != '_') continue;
        int c = k[1] - '1', b = rnd_key(k + 3);
        if (b >= NPARAMS) continue;
        FMAP[c][b] = (short)i;
        FBASE[i] = (short)b;
        FSYN[i] = (signed char)c;
    }
    for (int i = 0; i < NPARAMS; i++) FSHARED[i] = FSYN[i] == 0 && FMAP[1][i] == i;
    fmap_ready = 1;
}
int fp(ed_t *e, int i) {
    if (i < 0 || i >= NPARAMS || e->abs) return i;
    fmap_init();
    return FSYN[i] ? i : FMAP[e->focus][i];
}
int base_of(int i) { fmap_init(); return i >= 0 && i < NPARAMS ? FBASE[i] : i; }
int synth_of(int i) { fmap_init(); return i >= 0 && i < NPARAMS ? FSYN[i] : 0; }
int shared_param(int i) { fmap_init(); return i >= 0 && i < NPARAMS && FSHARED[i]; }
int synth_param(int i, int c) { fmap_init(); return i >= 0 && i < NPARAMS && c >= 0 && c < NSYNTHS ? FMAP[c][i] : i; }
float ed_get(ed_t *e, int i) { return e->fx->getParameter(e->fx, fp(e, i)); }

/* the params an edit of panel index i goes to: the focused Synth's, and every other Synth in focus (one: shared);
 * with its LINK lit (hub.c), all three Synths' */
static int edit_targets(ed_t *e, int i, int *t) {
    int n = 0;
    t[n++] = fp(e, i);
    if (e->abs || shared_param(i) || synth_of(i) || i < 0 || i >= NPARAMS) return n;
    int all = link_lit(e, i);
    for (int c = 0; c < NSYNTHS; c++)
        if (c != e->focus && (all || (e->focus_mask >> c & 1u))) t[n++] = FMAP[c][i];
    return n;
}

/* the panel shows Synth c (add: TOGETHER lit or Ctrl, several Synths edited together, the last one picked shown; an
 * add lights TOGETHER, so the console shows that the SYNTH buttons now add and remove) */
static void set_focus(ed_t *e, int c, int add) {
    if (c < 0 || c >= NSYNTHS) return;
    if (add) e->together = 1;
    if (!add) {
        e->focus_mask = 1u << c;
    } else if ((e->focus_mask >> c & 1u) && e->focus_mask != 1u << c) {   /* out of the group */
        e->focus_mask &= ~(1u << c);
        if (e->focus == c) {   /* the one shown went: the first left shows */
            for (int k = 0; k < NSYNTHS; k++)
                if (e->focus_mask >> k & 1u) { c = k; break; }
        } else {
            c = e->focus;   /* (another went: the panel stays on the one it shows) */
        }
    } else {
        e->focus_mask |= 1u << c;
    }
    e->focus = c;
    e->sq.dirty = 1;   /* the step editor shows this Synth's sequence */
    seq_read(e);
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);   /* every control shows this Synth's value */
}

/* ---- the SETUP page: the three Synths side by side (and the shared settings) ---- */
static const char *const SETUP_KEY[] = {"syn_on", "syn_ch", "key_lo", "key_hi", "transpose", "octave_shift", "voices",
                                        "volume", "pan", "mfx_send1", "mfx_send2", "modfx_on", "midi_ch", "master_level",
                                        "seq_all", "learn_mode"};
const char *const SETUP_LABEL[] = {"ON", "MIDI CHANNEL", "LOWEST KEY", "HIGHEST KEY", "TRANSPOSE", "OCTAVE", "VOICES",
                                   "LEVEL", "PAN", "SEND 1", "SEND 2", "SYNTH EFFECT", "MIDI IN (ALL)", "MASTER LEVEL",
                                   "PLAY ALL SEQS", "CHORD LEARN"};
int setup_rows(int sub, int *keys) {
    int n = 0;
    for (int r = 0; r < ROWS; r++) {
        unsigned k = (unsigned)(sub * ROWS + r);
        keys[r] = k < sizeof SETUP_KEY / sizeof SETUP_KEY[0] && rnd_key(SETUP_KEY[k]) < NPARAMS ? rnd_key(SETUP_KEY[k]) : -1;
        n += keys[r] >= 0;
    }
    return n;
}
RECT setup_cell(int row, int col) {
    RECT r = L.row[row];
    int lw = rw(&r) * 30 / 100, cw = (rw(&r) - lw) / NSYNTHS;
    if (col < 0) r.right = r.left + lw;
    else if (col < NSYNTHS) { r.left += lw + col * cw + sc(4); r.right = r.left + cw - sc(8); }
    else { r.left += lw + sc(4); r.right -= sc(4); }
    if (col >= 0) { r.top += sc(2); r.bottom -= sc(2); }
    return r;
}

/* ---- values ---- */
void param_text(ed_t *e, int i, char *out, int n) {
    char disp[64] = "", lab[32] = "";
    i = fp(e, i);
    e->fx->dispatcher(e->fx, effGetParamDisplay, i, 0, disp, 0);
    e->fx->dispatcher(e->fx, effGetParamLabel, i, 0, lab, 0);
    const char *d = disp;
    while (*d == ' ') d++;
    snprintf(out, n, lab[0] && PARAMS[i].nopts == 0 ? "%s %s" : "%s", d, lab);
}

static void set_value(ed_t *e, int i, float v) {   /* i: a panel index (the focused Synths'), or with abs its own */
    int t[NSYNTHS], n = edit_targets(e, i, t);
    lib_touch(e, NULL);   /* an undo step (a drag or a burst of wheel clicks: one) */
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    float d = v - e->fx->getParameter(e->fx, t[0]);
    int rel = n > 1 && link_relative(e, i);   /* ALL 3 (the cutoffs): the others move by as much, offsets kept */
    for (int k = 0; k < n; k++) {
        float vk = rel && k ? e->fx->getParameter(e->fx, t[k]) + d : v;
        vk = vk < 0 ? 0 : vk > 1 ? 1 : vk;
        e->fx->setParameter(e->fx, t[k], vk);
        if (e->host) e->host(e->fx, audioMasterAutomate, t[k], 0, NULL, vk);
    }
    invalidate_param(e, base_of(i));
}

static void begin_edit(ed_t *e, int i, int end) {   /* the host's undo / automation bracket, for every target */
    int t[NSYNTHS], n = edit_targets(e, i, t);
    for (int k = 0; k < n && e->host; k++) e->host(e->fx, end ? audioMasterEndEdit : audioMasterBeginEdit, t[k], 0, NULL, 0);
    if (end) lib_commit(e);   /* the edit is over: its undo step */
}

static void edit_value(ed_t *e, int i, float v) {   /* a one-shot change, bracketed for the host's undo/automation */
    begin_edit(e, i, 0);
    set_value(e, i, v);
    begin_edit(e, i, 1);
}
static void edit_abs(ed_t *e, int i, float v) {   /* the same for a param's own index (any Synth's, not the focus's) */
    int was = e->abs;
    e->abs = 1;
    edit_value(e, i, v);
    e->abs = was;
    for (int c = 0; c < NSYNTHS; c++) invalidate(e, &L.focus[c]);
}
void ed_edit(ed_t *e, int i, float v) { edit_value(e, i, v); }       /* (for hub.c) */
void ed_edit_abs(ed_t *e, int i, float v) { edit_abs(e, i, v); }
static void show(ed_t *e, int screen, int sub);
void ed_show(ed_t *e, int screen, int sub) { show(e, screen, sub); }
void ed_focus(ed_t *e, int c, int add) { set_focus(e, c, add); }

/* ---- the randomiser: results go in param by param like knob moves; a history of sounds for UNDO / REDO ---- */
static void rnd_snapshot(ed_t *e, float *v) {   /* the focused Synth's sound, in Synth 1's places */
    for (int i = 0; i < NPARAMS; i++) v[i] = ed_get(e, i);
}
static int rnd_same(const float *a, const float *b) {
    for (int i = 0; i < NPARAMS; i++)
        if (fabsf(a[i] - b[i]) > 1e-6f) return 0;
    return 1;
}
static void rnd_push(ed_t *e, const float *v, const char *name) {   /* a new sound: anything REDO could reach goes */
    if (e->hist_n > 0) e->hist_n = e->hist_pos + 1;
    if (e->hist_n == RND_HIST) {   /* full: the oldest goes */
        memmove(e->hist[0], e->hist[1], sizeof e->hist[0] * (RND_HIST - 1));
        memmove(e->hist_name[0], e->hist_name[1], sizeof e->hist_name[0] * (RND_HIST - 1));
        e->hist_n--;
    }
    memcpy(e->hist[e->hist_n], v, sizeof e->hist[0]);
    snprintf(e->hist_name[e->hist_n], sizeof e->hist_name[0], "%s", name);
    e->hist_pos = e->hist_n++;
}
/* every param that differs, as a knob move would set it. LINK CUTOFF would drag one cutoff along with the other while
 * they land, so it is off meanwhile and goes back on (if the sound has it) last, with the new cutoffs' offset. */
static void rnd_apply(ed_t *e, const float *target) {
    static int link = -2;
    if (link == -2) link = rnd_key("flt_link") < NPARAMS ? rnd_key("flt_link") : -1;
    float cur[NPARAMS];
    rnd_snapshot(e, cur);
    if (link >= 0 && cur[link] > 0.5f) edit_value(e, link, 0.0f);
    for (int i = 0; i < NPARAMS; i++)
        if (i != link && fabsf(target[i] - cur[i]) > 1e-7f) edit_value(e, i, target[i]);
    if (link >= 0 && target[link] > 0.5f) edit_value(e, link, target[link]);
}
/* a new sound for the focused Synth: RANDOMISE (0) or MUTATE (1) of cur; returns the archetype (RANDOMISE). SEQUENCER
 * lit: its notes (the hook), then its modulation lanes go to the engine here, the settings with the rest of out. */
static int rnd_make(ed_t *e, const float *cur, float *out, int mutate) {
    rnd_opts_t o = {e->rnd_wild, e->rnd_also, 0};
    int seq = e->rnd_also >> RS_SEQ & 1u;
    if (seq) o.seq_notes = 1;   /* (the sequence always has notes after: kept, or new ones from the generator below) */
    int a = mutate ? (rnd_mutate_x(cur, out, e->rnd_amount, e->rnd_locks, &e->rnd_seed, &o), 0)
                   : rnd_generate_x(cur, out, e->rnd_locks, &e->rnd_seed, &o);
    for (int i = 0; i < NPARAMS && e->focus; i++)   /* Synth 2 or 3: the shared master effects stay as they are */
        if (shared_param(i)) out[i] = cur[i];
    if (seq) {   /* the notes (the sequence generator, in the key just picked), then the lanes: one load */
        static sq_seq_t q;
        int li = rnd_key("seq_length"), ki = rnd_key("key_root"), si = rnd_key("key_scale");
        int len = li < NPARAMS ? (int)lroundf(rnd_real(li, out[li])) : 16;
        int key = ki < NPARAMS ? (int)lroundf(rnd_real(ki, out[ki])) : 0;
        int scale = si < NPARAMS ? (int)lroundf(rnd_real(si, out[si])) : KS_MINOR;
        seq_read(e);
        q = e->sq.q;
        int notes = seq_random_notes(e, mutate, e->rnd_amount, e->rnd_wild, key, scale, len, &e->rnd_seed, &q);
        if (rnd_seq_lanes(q.m, len, out, mutate, e->rnd_amount, &o, &e->rnd_seed) | notes) seq_load(e, &q);
    }
    return a;
}
/* all: the other Synths get a sound of their own too (ON, channels and key ranges stay: the randomiser never
 * touches them); the randomiser's UNDO / REDO follow the Synth in focus, Ctrl+Z takes back all three at once */
static void rnd_action(ed_t *e, int mutate, int all) {   /* RANDOMISE (0) or MUTATE (1) */
    float cur[NPARAMS], out[NPARAMS];
    char name[40];
    if (!e->rnd_seed) e->rnd_seed = ((uint32_t)GetTickCount() * 2654435761u) ^ (uint32_t)(uintptr_t)e ^ 0x2545F491u;
    if (!e->rnd_seed) e->rnd_seed = 1;
    if (e->hist_focus != e->focus) { e->hist_n = e->hist_pos = 0; e->hist_focus = e->focus; }   /* another Synth's */
    lib_hold(e, 1);   /* the whole new sound is one undo step */
    lib_touch(e, mutate ? "Mutate" : "Randomise");
    rnd_snapshot(e, cur);
    if (e->hist_n == 0 || !rnd_same(cur, e->hist[e->hist_pos])) {   /* the sound before, to come back to */
        char prog[32] = "";
        int p = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
        e->fx->dispatcher(e->fx, effGetProgramNameIndexed, p, 0, prog, 0);
        if (e->hist_n) snprintf(name, sizeof name, "Edited");
        else snprintf(name, sizeof name, "Start: %s", prog);
        rnd_push(e, cur, name);
    }
    int a = rnd_make(e, cur, out, mutate);
    if (mutate) snprintf(name, sizeof name, "Mutation %d (%.0f%%)", ++e->mut_count, e->rnd_amount * 100.0f);
    else snprintf(name, sizeof name, "Random %d: %s", ++e->rnd_count, RND_ARCH[a]);
    if (e->rnd_wild > 0.0f) snprintf(name + strlen(name), sizeof name - strlen(name), ", wild %.0f%%", e->rnd_wild * 100.0f);
    rnd_apply(e, out);
    if (all && NSYNTHS > 1) {
        int focus = e->focus;
        unsigned mask = e->focus_mask;
        for (int c = 0; c < NSYNTHS; c++) {
            if (c == focus) continue;
            float v[NPARAMS], o[NPARAMS];
            e->focus = c;
            e->focus_mask = 1u << c;   /* (each Synth on its own, even when several are edited together) */
            rnd_snapshot(e, v);
            rnd_make(e, v, o, mutate);
            rnd_apply(e, o);
        }
        e->focus = focus;
        e->focus_mask = mask;
        if (e->rnd_also >> RS_SEQ & 1u) seq_read(e);   /* (the step editor shows the focused Synth's sequence again) */
        snprintf(name + strlen(name), sizeof name - strlen(name), " (all 3)");
    }
    rnd_snapshot(e, cur);   /* as the plugin took it */
    rnd_push(e, cur, name);
    lib_hold(e, -1);
    InvalidateRect(e->wnd, NULL, FALSE);
}
static void rnd_step(ed_t *e, int dir) {   /* UNDO (-1) / REDO (+1) through the history */
    float cur[NPARAMS];
    if (!e->hist_n || e->hist_focus != e->focus) return;   /* (the history is another Synth's) */
    rnd_snapshot(e, cur);
    if (!rnd_same(cur, e->hist[e->hist_pos])) rnd_push(e, cur, "Edited");   /* changed by hand since: keep that too */
    int to = e->hist_pos + dir;
    if (to < 0 || to >= e->hist_n) return;
    e->hist_pos = to;
    lib_hold(e, 1);
    lib_touch(e, dir < 0 ? "the randomiser's UNDO" : "the randomiser's REDO");
    rnd_apply(e, e->hist[to]);
    lib_hold(e, -1);
    InvalidateRect(e->wnd, NULL, FALSE);
}
static void rnd_set_amount(ed_t *e, int x, int wild) {   /* the AMOUNT (or WILD) bar: 0 .. 100% across it */
    RECT b = rnd_rect(wild ? Z_RND_WILD : Z_RND_AMOUNT);
    float v = (x - b.left) / (float)(rw(&b) > 0 ? rw(&b) : 1);
    *(wild ? &e->rnd_wild : &e->rnd_amount) = roundf((v < 0 ? 0 : v > 1 ? 1 : v) * 100.0f) / 100.0f;
}

static float option_norm(int i, int opt) { return PARAMS[i].nopts > 1 ? (float)opt / (PARAMS[i].nopts - 1) : 0.0f; }
int option_of(ed_t *e, int i) {
    return PARAMS[i].nopts > 1 ? (int)lroundf(ed_get(e, i) * (PARAMS[i].nopts - 1)) : 0;
}
float real_of(ed_t *e, int i) {   /* the value in the param's own units */
    return PARAMS[i].min + ed_get(e, i) * (PARAMS[i].max - PARAMS[i].min);
}

/* ---- the screen's pages ---- */
int page_items(ed_t *e, int p, int *items) {
    int n = 0;
    if (p < 0 || p >= NPAGES) return 0;
    for (int k = PAGES[p].first; k < PAGES[p].first + PAGES[p].count; k++) {
        const lay_item_t *it = &ITEMS[k];
        int o = it->cond >= 0 ? option_of(e, it->cond) : 0;
        if (it->cond < 0 || (o < 32 && (it->mask >> o) & 1u)) items[n++] = k;
    }
    return n;
}

int screen_subs(ed_t *e) {
    if (lib_screen(e->screen)) return lib_subs(e);   /* (the patch list and the library's pages) */
    if (e->screen == SCR_SETUP) return 2;
    if (e->screen == SCR_SETTINGS) return settings_subs(e);
    if (e->screen == SCR_QMOD) return qm_subs(e);   /* (quickmod.c: MOD FROM, the picked source's connections) */
    int items[512], n = e->screen == SCR_MATRIX ? NMATRIX : e->screen == SCR_INDEX ? NPAGES : page_items(e, e->screen, items);
    int per = e->screen == SCR_MATRIX ? ROWS : e->screen == SCR_INDEX ? SLOTS : page_slots(e->screen);
    return n > per ? (n + per - 1) / per : 1;
}

static void show(ed_t *e, int screen, int sub) {
    e->screen = screen;
    e->sub = sub;
    InvalidateRect(e->wnd, NULL, FALSE);
}

/* ---- interaction ---- */
static int key_at(ed_t *e, int x, int y, int *vel) {   /* the note under (x, y) on the keyboard, or -1 */
    RECT *k = &L.keys;
    POINT pt = {x, y};
    if (!PtInRect(k, pt)) return -1;
    float kw = rw(k) / (float)KEYS_WHITE, fy = (y - k->top) / (float)rh(k);
    int base = KEY_BASE + 12 * e->oct, j = (int)((x - k->left) / kw);
    if (j >= KEYS_WHITE) j = KEYS_WHITE - 1;
    *vel = 30 + (int)(97 * fy);
    if (*vel > 127) *vel = 127;
    if (fy < 0.62f) {   /* a black key? */
        static const int has_black[] = {1, 1, 0, 1, 1, 1, 0};
        float bw = kw * 0.58f;
        for (int jj = j - 1; jj <= j; jj++) {
            if (jj < 0 || jj >= KEYS_WHITE - 1 || !has_black[jj % 7]) continue;
            float bx = k->left + (jj + 1) * kw - bw / 2;
            if (x >= bx && x < bx + bw) { *vel = 30 + (int)(97 * fy / 0.62f); return note_of_white(jj, base) + 1; }
        }
    }
    return note_of_white(j, base);
}

static int hit_on_screen(ed_t *e, POINT pt) {   /* a param or a page entry on the centre screen, or -1 */
    if (!PtInRect(&L.screen, pt)) return -1;
    if (PtInRect(&L.sub_prev, pt) && screen_subs(e) > 1) return Z_SUB_PREV;
    if (PtInRect(&L.sub_next, pt) && screen_subs(e) > 1) return Z_SUB_NEXT;
    if (lib_screen(e->screen)) return lib_hit(e, pt);   /* the patch list and the library's pages (library.c) */
    if (e->screen == SCR_RANDOM) {
        static const int Z[] = {Z_RND_GO, Z_RND_MUTATE, Z_RND_AMOUNT, Z_RND_WILD, Z_RND_UNDO, Z_RND_REDO, Z_RND_ALL,
                                Z_RND_ALSO, Z_RND_ALSO + 1, Z_RND_ALSO + 2};
        for (unsigned k = 0; k < sizeof Z / sizeof Z[0]; k++) {
            RECT r = rnd_rect(Z[k]);
            if (Z[k] == Z_RND_AMOUNT || Z[k] == Z_RND_WILD) InflateRect(&r, sc(4), sc(4));   /* easier to catch */
            if (PtInRect(&r, pt)) return Z[k];
        }
        for (int k = 0; k < RS_COUNT; k++) {
            RECT r = rnd_rect(Z_RND_LOCK + k);
            if (PtInRect(&r, pt)) return Z_RND_LOCK + k;
        }
    } else if (e->screen == SCR_MATRIX) {
        for (int r = 0; r < ROWS && e->sub * ROWS + r < NMATRIX; r++) {
            RECT nc = matrix_cell(r, 0);   /* the row's number: its ENABLE (2026-10-29) */
            int on = MATRIX_ON[e->sub * ROWS + r];
            if (on >= 0 && PtInRect(&nc, pt)) { e->hit_screen = 1; e->hit_rect = nc; return on; }
            for (int k = 0; k < MX_COLS; k++) {
                RECT c = matrix_cell(r, k + 1);
                int i = MATRIX[e->sub * ROWS + r][k];
                if (i >= 0 && PtInRect(&c, pt)) { e->hit_screen = 1; e->hit_rect = c; return i; }
            }
        }
    } else if (e->screen == SCR_SEQ) {
        return seq_hit(e, pt);
    } else if (e->screen == SCR_GEN) {   /* the GENERATIVE hub page (hub.c) */
        return hub_hit(e, pt);
    } else if (e->screen == SCR_SETTINGS) {
        return settings_hit(e, pt);
    } else if (e->screen == SCR_SETUP) {   /* a cell: a param's own index (Synth 1, 2 or 3's, or a shared one) */
        int keys[ROWS];
        setup_rows(e->sub, keys);
        for (int r = 0; r < ROWS; r++) {
            if (keys[r] < 0) continue;
            for (int c = 0; c < NSYNTHS; c++) {
                RECT cell = setup_cell(r, shared_param(keys[r]) ? NSYNTHS : c);
                if (!PtInRect(&cell, pt)) continue;
                fmap_init();
                e->hit_screen = 1;
                e->hit_rect = cell;
                return Z_ABS + (shared_param(keys[r]) ? keys[r] : FMAP[c][keys[r]]);
            }
        }
    } else if (e->screen == SCR_INDEX) {
        for (int p = e->sub * SLOTS; p < NPAGES && p < (e->sub + 1) * SLOTS; p++) {
            RECT nm, c;
            slot_rects(p % SLOTS, &nm, &c);
            RECT all = {nm.left, nm.top, c.right, nm.bottom};
            if (PtInRect(&all, pt)) return Z_PAGE + p;
        }
    } else {
        int items[512], n = page_items(e, e->screen, items), per = page_slots(e->screen);
        for (int j = e->sub * per; j < n && j < (e->sub + 1) * per; j++) {
            RECT nm, c;
            slot_rects(page_slot(e->screen, j), &nm, &c);
            if (PtInRect(&c, pt)) { e->hit_screen = 1; e->hit_rect = c; return ITEMS[items[j]].param; }
        }
    }
    return -1;
}

static int hit(ed_t *e, int x, int y) {
    POINT pt = {x, y};
    e->hit_screen = 0;
    int q = qm_hit(e, pt);   /* a DEST button (quickmod.c) */
    if (q >= 0) return q;
    for (int k = 0; k < NMORES; k++)
        if (PtInRect(&L.more[k], pt)) return Z_PAGE + MORES[k].page;
    for (int c = 0; c < NCTLS; c++)
        if (PtInRect(&L.ctl[CTLS[c].param], pt)) {   /* (VOLUME with MASTER lit: master_level, hub.c) */
            e->hit_rect = L.ctl[CTLS[c].param];
            return panel_source(e, CTLS[c].param);
        }
    int s = hit_on_screen(e, pt);
    if (s >= 0) return s;
    if ((s = seq_hit_panel(e, pt)) >= 0) return s;   /* the SEQUENCER module's page LEDs */
    if ((s = hub_hit_console(pt)) >= 0) return s;    /* GENERATIVE, GEN OFF, TEMPO, HOST, OWN (hub.c) */
    for (int c = 0; c < NSYNTHS && NSYNTHS > 1; c++) {   /* a SYNTH button's M | S halves (a little taller to catch) */
        RECT m = focus_part(c, 1), so = focus_part(c, 2);
        m.bottom += sc(3);
        so.bottom += sc(3);
        so.right += sc(2);
        if (PtInRect(&m, pt)) return Z_MUTE1 + c;
        if (PtInRect(&so, pt)) return Z_SOLO1 + c;
    }
    static const struct { RECT *r; int z; } Z[] = {
        {&L.keys, Z_KEYS}, {&L.ribbon, Z_RIBBON}, {&L.pitch, Z_PITCH}, {&L.modw, Z_MODW}, {&L.pad, Z_PAD},
        {&L.oct_down, Z_OCT_DOWN}, {&L.oct_up, Z_OCT_UP}, {&L.oct_leds, Z_OCT_PICK}, {&L.panic, Z_PANIC}, {&L.prev, Z_PREV}, {&L.next, Z_NEXT}, {&L.bprev, Z_PREV},
        {&L.bnext, Z_NEXT}, {&L.name, Z_NAME}, {&L.encoder, Z_ENCODER}, {&L.tab_matrix, Z_TAB_MATRIX},
        {&L.tab_more, Z_TAB_MORE}, {&L.tab_analog, Z_TAB_ANALOG}, {&L.tab_random, Z_TAB_RANDOM}, {&L.tab_seq, Z_TAB_SEQ},
        {&L.focus[0], Z_FOCUS1}, {&L.focus[1], Z_FOCUS2}, {&L.focus[2], Z_FOCUS3}, {&L.tab_setup, Z_TAB_SETUP},
        {&L.euclid, Z_EUCLID}, {&L.compare, Z_COMPARE}, {&L.snapshot, Z_SNAPSHOT},
        {&L.tab_settings, Z_TAB_SETTINGS}};   /* (euclid: empty when there is no GENERATIVE section) */
    for (unsigned k = 0; k < sizeof Z / sizeof Z[0]; k++)
        if (PtInRect(Z[k].r, pt)) return Z[k].z;
    return -1;
}

static void pick_option(ed_t *e, int i, RECT r) {   /* a popup menu of the parameter's options (long lists: columns) */
    HMENU m = CreatePopupMenu();
    int cur = option_of(e, i);
    for (int k = 0; k < PARAMS[i].nopts; k++)
        AppendMenuA(m, MF_STRING | (k == cur ? MF_CHECKED : 0) | (k && k % 30 == 0 ? MF_MENUBARBREAK : 0),
                    (UINT_PTR)(k + 1), PARAMS[i].opts[k]);
    POINT pt = {r.left, r.bottom};
    ClientToScreen(e->wnd, &pt);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    DestroyMenu(m);
    if (choice > 0) edit_value(e, i, option_norm(i, choice - 1));
}

static void key_off(ed_t *e) {
    if (e->key >= 0) ui_midi(e, 0x80, e->key, 0);
    e->key = -1;
}

/* ---- the computer keyboard as a music keyboard (2026-10-29): A S D F G H J K L ; ' the white keys from C, W E T Y U
 * O P the black ones; Z / X the octave (the panel's octave buttons'). A = C4 at octave 0. Each key's note is kept, so
 * a key let go after an octave change still ends its own note; the window losing focus ends them all. ---- */
static int qk_semitone(WPARAM vk) {
    static const struct { unsigned char vk; signed char st; } MAP[] = {
        {'A', 0}, {'W', 1}, {'S', 2}, {'E', 3}, {'D', 4}, {'F', 5}, {'T', 6}, {'G', 7}, {'Y', 8}, {'H', 9}, {'U', 10},
        {'J', 11}, {'K', 12}, {'O', 13}, {'L', 14}, {'P', 15}, {VK_OEM_1, 16}, {VK_OEM_7, 17}};
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        if (MAP[i].vk == vk) return MAP[i].st;
    return -1;
}
static void send_kb_oct(ed_t *e);
static void qk_all_off(ed_t *e) {
    for (int k = 0; k < 256; k++)
        if (e->qk_note[k]) { ui_midi(e, 0x80, e->qk_note[k], 0); e->qk_note[k] = 0; }
    memset(e->qk_down, 0, sizeof e->qk_down);
    invalidate(e, &L.keys);
}
static int qk_key(ed_t *e, WPARAM vk, int down, LPARAM lp) {   /* 1: the key was a music key (taken) */
    if (vk == 'Z' || vk == 'X') {
        if (down && !(lp & 0x40000000)) {
            int o = e->oct + (vk == 'X' ? 1 : -1);
            e->oct = o < -2 ? -2 : o > 2 ? 2 : o;
            send_kb_oct(e);
            InvalidateRect(e->wnd, NULL, FALSE);   /* (the keyboard's labels and the octave LEDs) */
        }
        return 1;
    }
    int st = qk_semitone(vk);
    if (st < 0 || vk > 255) return 0;
    if (down) {
        if (lp & 0x40000000 || e->qk_note[vk]) return 1;   /* (held: the key repeats; no new note) */
        int n = KEY_BASE + 24 + 12 * e->oct + st;
        if (n < 1 || n > 127) return 1;
        e->qk_note[vk] = (unsigned char)n;
        e->qk_down[n]++;
        ui_midi(e, 0x90, n, 100);
    } else if (e->qk_note[vk]) {
        int n = e->qk_note[vk];
        e->qk_note[vk] = 0;
        if (e->qk_down[n]) e->qk_down[n]--;
        ui_midi(e, 0x80, n, 0);
    }
    invalidate(e, &L.keys);
    return 1;
}

static void controller_move(ed_t *e, int x, int y) {   /* a drag on the keys, a wheel, the pad or the ribbon */
    if (e->mode == M_TEMPO) { tempo_drag(e, y, GetKeyState(VK_SHIFT) < 0); return; }   /* (the TEMPO readout) */
    if (e->mode == M_KEY) {
        int vel, n = key_at(e, x, y, &vel);
        if (n != e->key && n >= 0) {
            key_off(e);
            ui_midi(e, 0x90, n, vel);
            e->key = n;
        }
    } else if (e->mode == M_PITCH) {
        float v = ((L.pitch.top + L.pitch.bottom) / 2.0f - y) / (rh(&L.pitch) / 2.0f);
        e->bend = v < -1 ? -1 : v > 1 ? 1 : v;
        int b = 8192 + (int)lroundf(e->bend * 8191);
        ui_midi(e, 0xE0, b & 127, b >> 7);
    } else if (e->mode == M_MODW) {
        float v = (L.modw.bottom - y) / (float)rh(&L.modw);
        e->wheel = v < 0 ? 0 : v > 1 ? 1 : v;
        ui_midi(e, 0xB0, 1, (int)lroundf(e->wheel * 127));
    } else if (e->mode == M_PAD) {
        float px = (x - L.pad.left) / (float)rw(&L.pad), py = (L.pad.bottom - y) / (float)rh(&L.pad);
        e->pad_x = px < 0 ? 0 : px > 1 ? 1 : px;
        e->pad_y = py < 0 ? 0 : py > 1 ? 1 : py;
        if (!e->pad_on) { e->pad_on = 1; ui_midi(e, 0xB0, PAD_TOUCH_CC, 127); }
        ui_midi(e, 0xB0, PAD_X_CC, (int)lroundf(e->pad_x * 127));
        ui_midi(e, 0xB0, PAD_Y_CC, (int)lroundf(e->pad_y * 127));
        ui_midi(e, 0xD0, (int)lroundf(e->pad_y * 127), 0);
    } else if (e->mode == M_RIBBON) {   /* the position (only when it changes), then the touch on the first move */
        float v = (x - L.ribbon.left) / (float)rw(&L.ribbon);
        e->ribbon = v < 0 ? 0 : v > 1 ? 1 : v;
        int cc = (int)lroundf(e->ribbon * 127);
        if (cc != e->ribbon_cc) ui_midi(e, 0xB0, RIBBON_CC, cc);
        if (!e->ribbon_on) ui_midi(e, 0xB0, RIBBON_TOUCH_CC, 127);
        e->ribbon_cc = cc;
        e->ribbon_on = 1;
    } else if (e->mode == M_RND_AMOUNT || e->mode == M_RND_WILD) {
        rnd_set_amount(e, x, e->mode == M_RND_WILD);
    } else if (e->mode == M_ENC) {
        int steps = (e->enc_y - y) / sc(18);
        if (steps) {
            e->enc_moved = 1;
            e->enc_y -= steps * sc(18);
            patches_step(e, steps);
            invalidate(e, NULL);
        }
        return;
    }
    static const int Z[] = {0, 0, Z_KEYS, Z_PITCH, Z_MODW, Z_PAD, 0, Z_RIBBON, 0};   /* by mode (M_*) */
    if (e->mode == M_RND_AMOUNT || e->mode == M_RND_WILD) invalidate(e, &L.screen);
    else if (e->mode > 0 && e->mode < (int)(sizeof Z / sizeof Z[0]) && Z[e->mode]) invalidate_zone(e, Z[e->mode]);
    else invalidate(e, NULL);
}

/* (2026-10-29) the pad and wheel as the engine has them: X/Y HOLD off sends the pad back to its rest when let go;
 * LOAD SAVED POS puts the saved positions back when a sound loads (src/engine.c pad_release, positions_load) */
enum { PP_POLARITY, PP_HOLD, PP_LOAD, PP_WHEEL, PP_X, PP_Y, PP_COUNT };
static int pos_param(int k) {   /* Synth 1's index of each (the focused Synth's through ed_get) */
    static const char *const KEY[PP_COUNT] = {"pad_polarity", "pad_hold", "load_pos", "wheel_pos", "pad_x_pos", "pad_y_pos"};
    static int ix[PP_COUNT] = {-2, -2, -2, -2, -2, -2};
    if (ix[k] == -2) {
        ix[k] = -1;
        for (int i = 0; i < NPARAMS; i++)
            if (!strcmp(PARAMS[i].key, KEY[k])) { ix[k] = i; break; }
    }
    return ix[k];
}
static void pad_rest(ed_t *e) {
    if (pos_param(PP_HOLD) < 0 || option_of(e, pos_param(PP_HOLD)) == 1) return;
    e->pad_x = e->pad_y = pos_param(PP_POLARITY) >= 0 && option_of(e, pos_param(PP_POLARITY)) == 1 ? 0.5f : 0.0f;
}
static void positions_follow(ed_t *e) {   /* after a new sound, or a saved position changed */
    int lp = pos_param(PP_LOAD) >= 0 ? option_of(e, pos_param(PP_LOAD)) : 0;
    if ((lp & 1) && pos_param(PP_WHEEL) >= 0) e->wheel = real_of(e, pos_param(PP_WHEEL)) * 0.01f;
    if ((lp & 2) && pos_param(PP_X) >= 0) {
        e->pad_x = real_of(e, pos_param(PP_X)) * 0.01f;
        e->pad_y = real_of(e, pos_param(PP_Y)) * 0.01f;
    }
}

static void controller_release(ed_t *e) {   /* the mouse let go (or lost): notes off, the pitch wheel springs back */
    int m = e->mode;
    e->mode = M_NONE;   /* first: the preset list below takes the mouse, which comes back here */
    if (m == M_KEY) key_off(e);
    if (m == M_PITCH) { e->bend = 0; ui_midi(e, 0xE0, 0, 64); }
    if (m == M_PAD) { e->pad_on = 0; ui_midi(e, 0xD0, 0, 0); ui_midi(e, 0xB0, PAD_TOUCH_CC, 0); pad_rest(e); }
    if (m == M_RIBBON) { e->ribbon_on = 0; e->ribbon_cc = -1; ui_midi(e, 0xB0, RIBBON_TOUCH_CC, 0); }
    /* a click on the preset knob: what it points at (PRESET AUTOLOAD off), else the patch list */
    if (m == M_ENC && !e->enc_moved && !settings_cue_load(e)) patches_open(e);
    if (m == M_LIB) lib_release(e);   /* a set's number: let go early, it loads; held, it was assigned */
    if (m == M_TEMPO) tempo_release(e);   /* the own tempo saved (once, not on every step of the drag) */
    InvalidateRect(e->wnd, NULL, FALSE);
}

/* (2026-10-30) the keyboard's octave to the engine, for the balls' FOLLOW OCTAVE (src/synths.h "kboct"; never saved) */
static void send_kb_oct(ed_t *e) {
    char b[16];
    snprintf(b, sizeof b, "kboct\n%d", e->oct);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(b) + 1, b, 0);
}

static void set_octave(ed_t *e, int oct) {
    if (oct < -2 || oct > 2) return;
    key_off(e);
    e->oct = oct;
    send_kb_oct(e);
    InvalidateRect(e->wnd, NULL, FALSE);
}

/* MIDI panic, as holding both OCTAVE buttons on the instrument: the pedals up, All Notes Off and All Sound Off on every
 * channel (so every part hears it, whatever its channel) */
static void panic(ed_t *e) {
    key_off(e);
    for (int ch = 0; ch < 16; ch++) {
        ui_midi_on(e, 0xB0 | ch, 64, 0);
        ui_midi_on(e, 0xB0 | ch, 66, 0);
        ui_midi_on(e, 0xB0 | ch, 123, 0);
        ui_midi_on(e, 0xB0 | ch, 120, 0);
    }
}

/* an OCTAVE button held (the window's stand-in for pressing both on the instrument): after 1 s the keyboard goes back
 * to the default octave, after 2 s a MIDI panic; the timer (WM_TIMER) checks */
static void octave_hold(ed_t *e) {
    if (e->mode != M_OCT) return;
    unsigned held = GetTickCount() - e->oct_t0;
    if (held >= 1000 && e->oct_stage == 0) { set_octave(e, 0); e->oct_stage = 1; }
    if (held >= 2000 && e->oct_stage == 1) { panic(e); e->oct_stage = 2; InvalidateRect(e->wnd, NULL, FALSE); }
}

static void press_zone(ed_t *e, int z) {   /* the console buttons, MORE triangles, page entries, sub-page arrows */
    if (patches_press(e, z)) return;   /* the PATCHES page's buttons and entries */
    if (settings_press(e, z)) return;   /* the SETTINGS page's */
    if (hub_press(e, z)) return;        /* the GENERATIVE hub page and the console's third row (hub.c) */
    if (z == Z_TAB_SETTINGS) { settings_open(e); return; }
    int subs = screen_subs(e);
    if (z >= Z_PAGE && z < Z_PAGE + NPAGES) {   /* again on an open page: its next sub-page */
        int p = z - Z_PAGE;
        show(e, p, e->screen == p ? (e->sub + 1) % subs : 0);
    } else if (z == Z_TAB_MATRIX) {
        show(e, SCR_MATRIX, e->screen == SCR_MATRIX ? (e->sub + 1) % subs : 0);
    } else if (z == Z_TAB_MORE) {
        show(e, SCR_INDEX, e->screen == SCR_INDEX ? (e->sub + 1) % subs : 0);
    } else if (z == Z_TAB_ANALOG && PG_ANALOG >= 0) {
        show(e, PG_ANALOG, 0);
    } else if (z == Z_TAB_RANDOM) {
        show(e, SCR_RANDOM, 0);
    } else if (z >= Z_MUTE1 && z <= Z_MUTE3) {
        synth_toggle(e, z - Z_MUTE1, 0);
    } else if (z >= Z_SOLO1 && z <= Z_SOLO3) {
        synth_toggle(e, z - Z_SOLO1, 1);
    } else if (z >= Z_FOCUS1 && z <= Z_FOCUS3) {   /* PANEL FOCUS (TOGETHER lit, or Ctrl: several Synths at once) */
        if (GetKeyState(VK_SHIFT) < 0 && NSYNTHS > 1) synth_toggle(e, z - Z_FOCUS1, 1);   /* SHIFT + SYNTH: solo */
        else set_focus(e, z - Z_FOCUS1, e->together || GetKeyState(VK_CONTROL) < 0);
    } else if (z == Z_TAB_SETUP) {   /* the three Synths side by side; again: its next page */
        show(e, SCR_SETUP, e->screen == SCR_SETUP ? (e->sub + 1) % subs : 0);
    } else if (z == Z_TAB_SEQ) {   /* the step editor; again: its other view (NOTES / MODS) */
        if (e->screen == SCR_SEQ) e->sq.view = !e->sq.view;
        seq_open(e, -1);
    } else if (z == Z_EUCLID && PG_GEN >= 0) {   /* the ring of dots: the GENERATIVE section's page */
        show(e, PG_GEN, 0);
    } else if (z == Z_RND_GO || z == Z_RND_MUTATE) {
        rnd_action(e, z == Z_RND_MUTATE, e->rnd_all);
    } else if (z == Z_RND_ALL) {
        e->rnd_all = !e->rnd_all;
        InvalidateRect(e->wnd, NULL, FALSE);
    } else if (z == Z_RND_UNDO || z == Z_RND_REDO) {
        rnd_step(e, z == Z_RND_UNDO ? -1 : 1);
    } else if (z >= Z_RND_LOCK && z < Z_RND_LOCK + RS_COUNT) {
        e->rnd_locks ^= 1u << (z - Z_RND_LOCK);
        InvalidateRect(e->wnd, NULL, FALSE);
    } else if (z >= Z_RND_ALSO && z < Z_RND_ALSO + 3) {   /* ARPEGGIATOR, GENERATIVE, SEQUENCER */
        e->rnd_also ^= 1u << (RS_ARP + z - Z_RND_ALSO);
        InvalidateRect(e->wnd, NULL, FALSE);
    } else if (z == Z_SUB_PREV) {
        show(e, e->screen, (e->sub + subs - 1) % subs);
    } else if (z == Z_SUB_NEXT) {
        show(e, e->screen, (e->sub + 1) % subs);
    }
}

static void press(ed_t *e, int x, int y) {
    int i = hit(e, x, y);
    if (qm_press(e, i, x, y)) return;   /* QUICK ASSIGN: a source picked, a knob connected (quickmod.c) */
    if (seq_zone(i)) {   /* the step editor and the page LEDs (seq_page.c); a drag on a note or a value follows */
        lib_touch(e, NULL);   /* (a step edit is an undo step; a click that changes nothing is none) */
        if (seq_press(e, i, x, y, GetKeyState(VK_SHIFT) < 0)) { e->mode = M_SEQ; SetCapture(e->wnd); }
        else lib_commit(e);
        return;
    }
    int absolute = i >= Z_ABS && i < Z_ABS + NPARAMS;   /* a SETUP cell: its param's own index, not the focus's */
    if (absolute) {
        i -= Z_ABS;
        e->abs = 1;
    }
    switch (i) {
    case -1: return;
    case Z_PREV: patches_step(e, -1); return;   /* factory presets, then user patches */
    case Z_NEXT: patches_step(e, 1); return;
    case Z_NAME: if (!settings_cue_load(e)) patches_toggle(e); return;   /* (a pointed-at patch: it loads) */
    case Z_OCT_DOWN: case Z_OCT_UP:   /* a click steps; held, the default octave (1 s), then a panic (2 s) */
        set_octave(e, e->oct + (i == Z_OCT_UP ? 1 : -1));
        e->mode = M_OCT;
        e->oct_t0 = GetTickCount();
        e->oct_stage = 0;
        SetCapture(e->wnd);
        return;
    case Z_OCT_PICK: {   /* an octave LED: that octave (the middle one: the default) */
        int k = rw(&L.oct_leds) > 0 ? (x - L.oct_leds.left) * 5 / rw(&L.oct_leds) : 2;
        set_octave(e, (k < 0 ? 0 : k > 4 ? 4 : k) - 2);
        return;
    }
    case Z_PANIC: panic(e); InvalidateRect(e->wnd, NULL, FALSE); return;   /* (the held keys drawn up again) */
    case Z_KEYS: e->mode = M_KEY; break;
    case Z_PITCH: e->mode = M_PITCH; break;
    case Z_MODW: e->mode = M_MODW; break;
    case Z_PAD: e->mode = M_PAD; break;   /* (controller_move sends the touch and the position) */
    case Z_RIBBON: e->mode = M_RIBBON; e->ribbon_cc = -1; break;
    case Z_ENCODER: e->mode = M_ENC; e->enc_y = y; e->enc_moved = 0; break;
    case Z_RND_AMOUNT: e->mode = M_RND_AMOUNT; break;
    case Z_RND_WILD: e->mode = M_RND_WILD; break;
    case Z_TEMPO: tempo_press(e, y); break;   /* a drag sets the own tempo (hub.c) */
    default:
        if (i >= Z_KEYS) { press_zone(e, i); return; }
        break;
    }
    if (e->mode != M_NONE) {
        SetCapture(e->wnd);
        controller_move(e, x, y);
        return;
    }
    if (settings_learn_pick(e, absolute ? i : fp(e, i))) { e->abs = 0; return; }   /* MIDI LEARN takes the control */
    int screen = e->hit_screen, n = PARAMS[i].nopts;
    RECT r = e->hit_rect;
    int k = screen ? (n ? K_MENU : K_AMOUNT) : L.kind[i];
    if (k == K_KNOB || k == K_SMALL || k == K_BIG || k == K_AMOUNT || (k == K_OCT && n == 0)) {
        e->drag = i;
        e->mode = M_PARAM;
        e->drag_horiz = k == K_AMOUNT;
        e->drag_x = x;
        e->drag_y = y;
        e->drag_from = ed_get(e, i);
        SetCapture(e->wnd);
        begin_edit(e, i, 0);
    } else if (k == K_TOGGLE || (k == K_MENU && n == 2)) {
        edit_value(e, i, option_of(e, i) ? 0.0f : 1.0f);
    } else if (k == K_LEDS || k == K_OCT) {   /* an LED (or footage) picks it; the button steps on */
        int x0, y0, row, o = (option_of(e, i) + 1) % n;
        RECT bt;
        leds_geom(i, &x0, &y0, &row, &bt);
        if (k == K_LEDS && y >= y0 && y < y0 + n * row) o = (y - y0) / row;
        edit_value(e, i, option_norm(i, o));
    } else {
        pick_option(e, i, r);
    }
    if (absolute && e->mode != M_PARAM) e->abs = 0;   /* (a drag keeps it until the mouse lets go) */
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    ed_t *e = (ed_t *)GetWindowLongPtrA(w, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        SetTimer(w, 1, 50, NULL);
        SetTimer(w, 2, RATE_TIMER_MS, NULL);   /* the rate LEDs (rate_leds.c) */
        skin_acquire();   /* the shared look (skin.c), kept while any editor window is open */
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (e) paint(e, dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:   /* drawing into a given DC: screenshots, thumbnails */
        if (e) paint(e, (HDC)wp);
        return 0;
    case WM_TIMER: {   /* values the host or a preset changed: redraw */
        if (!e) return 0;
        if (wp == 2) { rate_leds_tick(e); pics_tick(e); return 0; }   /* the rate LEDs: only the ones that changed
                                                                         * repaint; the balls' box while it shows */
        if (e->corners == 0 && e->corner_tries < 100) {   /* the host's window sized to us yet? (for 5 s) */
            e->corner_tries++;
            e->corners = square_corners(w);
        }
        octave_hold(e);
        lib_tick(e);   /* an undo step after a pause, a set's number held, a MIDI program change, the tempo */
        qm_tick(e);   /* quick assign's message runs out */
        settings_tick(e);   /* MIDI LEARN: a CC heard */
        int changed = 0, moved = 0, prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0), which[16];
        for (int i = 0; i < NPARAMS; i++) {
            float v = e->fx->getParameter(e->fx, i);
            if (v != e->shown[i]) {
                e->shown[i] = v;
                if (moved < 16) which[moved] = i;
                moved++;
            }
        }
        if (prog != e->shown_prog) { e->shown_prog = prog; changed = 1; }
        for (int k = 0; k < moved && k < 16 && !changed; k++)   /* a saved wheel or pad position (LOAD SAVED POS) */
            if (base_of(which[k]) >= pos_param(PP_LOAD) && base_of(which[k]) <= pos_param(PP_Y)) changed = 1;
        if (changed || moved > 16) positions_follow(e);
        if (patches_tick(e)) changed = 1;   /* the edited mark, a message that ran out */
        if (seq_tick(e, changed || moved)) {   /* the playing step; the sequence while it is edited: the screen, its LEDs */
            invalidate(e, &L.screen);
            RECT r = L.seq_leds;
            InflateRect(&r, sc(8), sc(8));
            invalidate(e, &r);
        }
        if (changed || moved > 16) InvalidateRect(w, NULL, FALSE);   /* a new sound: all of it */
        else for (int k = 0; k < moved; k++) invalidate_param(e, base_of(which[k]));   /* a few knobs: just them */
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!e) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (e->mode == M_PARAM && e->drag >= 0) {
            int i = e->drag;
            float scale = (wp & MK_SHIFT) ? 1.0f / 1000 : 1.0f / 200;
            float v = e->drag_horiz ? e->drag_from + (x - e->drag_x) * scale * 1.5f : e->drag_from + (e->drag_y - y) * scale;
            set_value(e, i, v);
            return 0;
        }
        if (e->mode == M_SEQ) { seq_drag(e, x, y, (wp & MK_SHIFT) != 0); return 0; }
        if (e->mode == M_QMOD) { qm_drag(e, x, y, (wp & MK_SHIFT) != 0); return 0; }
        if (e->mode != M_NONE) { controller_move(e, x, y); return 0; }
        int h = hit(e, x, y);
        if (h != e->hover) {
            invalidate_zone(e, e->hover);
            e->hover = h;
            invalidate_zone(e, h);
        }
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, w, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_KEYDOWN:   /* 1-3 or Shift+F5/F6/F7: the Synth in focus (Shift+1-3, Ctrl+Shift+F5-F7: added); the step editor: arrows, Delete */
        if (e && qm_key(e, (int)wp)) return 0;   /* Esc: quick assign done */
        if (e && GetKeyState(VK_CONTROL) < 0 && (wp == 'Z' || wp == 'Y')) {   /* undo, redo (Ctrl+Shift+Z: redo) */
            if (wp == 'Y' || GetKeyState(VK_SHIFT) < 0) lib_redo(e);
            else lib_undo(e);
            return 0;
        }
        if (e && wp >= VK_F5 && wp <= VK_F7 && GetKeyState(VK_SHIFT) < 0) {
            set_focus(e, (int)wp - VK_F5, GetKeyState(VK_CONTROL) < 0);
            return 0;
        }
        if (e && NSYNTHS > 1 && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 && e->mode == M_NONE &&
            ((wp >= '1' && wp < '1' + NSYNTHS) || (wp >= VK_NUMPAD1 && wp < VK_NUMPAD1 + NSYNTHS))) {
            int c = (int)wp - (wp >= VK_NUMPAD1 ? VK_NUMPAD1 : '1');
            set_focus(e, c, GetKeyState(VK_SHIFT) < 0);   /* 1-3: that Synth; Shift: added to (or out of) the group */
            return 0;
        }
        if (e && e->screen == SCR_SEQ) {
            lib_touch(e, NULL);   /* (committed after a pause) */
            if (seq_key(e, (int)wp, GetKeyState(VK_SHIFT) < 0)) return 0;
        }
        if (e && e->screen != SCR_SEQ && e->mode == M_NONE && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 &&
            (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_PRIOR || wp == VK_NEXT)) {   /* the screen's sub-pages, as its
                                                                                       * title line's < > (2026-10-30) */
            int subs = screen_subs(e), d = wp == VK_RIGHT || wp == VK_NEXT ? 1 : subs - 1;
            if (subs > 1) show(e, e->screen, (e->sub + d) % subs);
            return 0;
        }
        if (e && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_SHIFT) >= 0 &&
            qk_key(e, wp, 1, lp))
            return 0;   /* a music key (Shift with a letter stays the window's: Shift+R, Shift+M) */
        if (e && (wp == 'R' || wp == 'M') && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 &&
            e->mode == M_NONE && !(lp & 0x40000000)) {   /* (a held key doesn't repeat it) */
            int shift = GetKeyState(VK_SHIFT) < 0;
            if (wp == 'M') rnd_action(e, 1, e->rnd_all || shift);   /* M: mutate (Shift+M: all three) */
            else rnd_action(e, 0, e->rnd_all || shift);              /* R: randomise (Shift+R: all three) */
            return 0;
        }
        break;
    case WM_KEYUP:   /* a music key let go (whatever Shift or Ctrl do meanwhile) */
        if (e && qk_key(e, wp, 0, lp)) return 0;
        break;
    case WM_KILLFOCUS:   /* the window lost the keyboard: no note left hanging */
        if (e) qk_all_off(e);
        break;
    case WM_MOUSELEAVE:
        if (e && e->mode == M_NONE) { e->hover = -1; InvalidateRect(w, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN:
        if (!e) return 0;
        SetFocus(w);
        press(e, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONDOWN: {   /* a list of options for any option control */
        if (!e) return 0;
        int i = hit(e, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)), q = 0;
        if (!seq_zone(i)) q = qm_right(e, i, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));   /* connections, or a source */
        if (q == 1) return 0;
        if (q == 2) { pick_option(e, i, e->hit_rect); return 0; }
        if (seq_zone(i)) { lib_touch(e, NULL); seq_right(e, i); }   /* the step editor: delete, a step's rest */
        else if (i >= 0 && i < NPARAMS && PARAMS[i].nopts > 1) pick_option(e, i, e->hit_rect);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (!e) return 0;
        if (e->mode == M_SEQ) {
            seq_release(e);
            e->mode = M_NONE;
            lib_commit(e);
        } else if (e->mode == M_QMOD) {
            qm_release(e);
        } else if (e->mode == M_PARAM) {
            if (e->drag >= 0) begin_edit(e, e->drag, 1);
            e->drag = -1;
            e->mode = M_NONE;
            e->abs = 0;   /* (a SETUP cell's drag is over) */
        } else if (e->mode != M_NONE) {
            controller_release(e);
        }
        if (msg == WM_LBUTTONUP && GetCapture() == w) ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK: {   /* back to the default */
        if (!e) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), i = hit(e, x, y);
        if (e->qm.src) { press(e, x, y); return 0; }   /* (quick assign on: a second click, not a reset) */
        if (seq_zone(i) && i < Z_SEQ_BASE + 200) { lib_touch(e, NULL); seq_double(e, i); lib_commit(e); }   /* an empty step cell: a new note */
        else if (i >= 0 && i < NPARAMS && PARAMS[i].nopts == 0) edit_value(e, i, PARAMS[i].def);
        else if (i >= Z_FOCUS1 && i <= Z_FOCUS3 && NSYNTHS > 1) synth_toggle(e, i - Z_FOCUS1, 1);   /* solo (the first
                                                                                 * click picked the Synth already) */
        else if (i >= 0) press(e, x, y);   /* a second click on a button or a key counts as a click */
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!e) return 0;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(w, &pt);
        int i = hit(e, pt.x, pt.y), dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (i == Z_ENCODER) { patches_step(e, -dir); return 0; }
        if (settings_wheel(e, i, dir, (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0)) return 0;
        if (tempo_wheel(e, i, dir, (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0)) return 0;   /* (hub.c) */
        if (seq_zone(i) || (e->screen == SCR_SEQ && PtInRect(&L.screen, pt) && (i < 0 || i >= NPARAMS))) {
            lib_touch(e, NULL);   /* (committed after a pause) */
            if (seq_wheel(e, i, dir, (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0)) return 0;
        }
        if (i == Z_RND_AMOUNT || i == Z_RND_WILD) {
            float *a = i == Z_RND_WILD ? &e->rnd_wild : &e->rnd_amount, v = *a + dir * 0.01f;
            *a = roundf((v < 0 ? 0 : v > 1 ? 1 : v) * 100.0f) / 100.0f;
            InvalidateRect(w, NULL, FALSE);
            return 0;
        }
        if (i < 0 || i >= NPARAMS) {   /* over the screen: its sub-pages */
            int subs = screen_subs(e);
            if (PtInRect(&L.screen, pt) && subs > 1) show(e, e->screen, (e->sub + subs - dir) % subs);
            return 0;
        }
        float v = ed_get(e, i);
        if (PARAMS[i].nopts > 1) {
            int o = option_of(e, i) + dir;
            if (o >= 0 && o < PARAMS[i].nopts) set_value(e, i, option_norm(i, o));
        } else {
            float span = PARAMS[i].max - PARAMS[i].min;
            float step = PARAMS[i].int_display && span > 0 ? 1.0f / span : 0.01f;
            set_value(e, i, v + dir * step);
        }
        return 0;
    }
    case WM_DESTROY:
        if (e) {   /* nothing left held: the key, the ribbon's touch */
            key_off(e);
            if (e->ribbon_on) { e->ribbon_on = 0; ui_midi(e, 0xB0, RIBBON_TOUCH_CC, 0); }
            if (e->pad_on) { e->pad_on = 0; ui_midi(e, 0xD0, 0, 0); ui_midi(e, 0xB0, PAD_TOUCH_CC, 0); }
            e->mode = M_NONE;
        }
        KillTimer(w, 1);
        KillTimer(w, 2);
        skin_release();
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static HFONT font(float px, int weight, const char *face) {
    int h = (int)lroundf(px * L.s);
    return CreateFontA(-(h < 7 ? 7 : h), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

/* The window class and the fonts, made when the first editor opens and undone when the last instance closes (and,
 * should a host unload the DLL without closing an instance, as the DLL goes): a class left registered would point into
 * the unloaded DLL, and a rebuilt DLL loaded again at the same address would then open its window through it. */
static int class_ready, idle_ready;
static const char *IDLE_CLASS = "ONEishIdle";
static HFONT *const FONTS[] = {&font_title, &font_label, &font_value, &font_tiny, &font_screen, &font_cell, &font_big,
                               &font_brand};
static void free_window_class(void) {
    if (idle_ready) { UnregisterClassA(IDLE_CLASS, dll); idle_ready = 0; }
    if (!class_ready) return;
    UnregisterClassA(CLASS_NAME, dll);
    for (unsigned k = 0; k < sizeof FONTS / sizeof FONTS[0]; k++) {
        if (*FONTS[k]) DeleteObject(*FONTS[k]);
        *FONTS[k] = NULL;
    }
    class_ready = 0;
}

static void init_window_class(void) {
    if (class_ready) return;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)&wndproc, &dll);
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = dll;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassA(&wc);
    font_title = font(10, FW_BOLD, "Segoe UI");
    font_label = font(9, FW_SEMIBOLD, "Segoe UI");
    font_value = font(9, FW_NORMAL, "Segoe UI");
    font_tiny = font(9, FW_SEMIBOLD, "Segoe UI");
    font_screen = font(11.5f, FW_NORMAL, "Segoe UI");
    font_cell = font(10.5f, FW_NORMAL, "Segoe UI");
    font_big = font(18, FW_SEMIBOLD, "Segoe UI");
    font_brand = font(17, FW_LIGHT, "Segoe UI");
    class_ready = 1;
}

/* a message-only window per instance: its timer loads what a MIDI program change asked for while the editor is closed
 * (library.c: lib_idle). Made on the thread that makes the instance (the host's main thread) and closed with it. */
static LRESULT CALLBACK idle_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER) {
        ed_t *e = (ed_t *)GetWindowLongPtrA(w, GWLP_USERDATA);
        if (e && e->idle == w) lib_idle(e);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}
static HWND make_idle(ed_t *e) {
    if (!idle_ready) {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(void *)&idle_proc, &dll);
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = idle_proc;
        wc.hInstance = dll;
        wc.lpszClassName = IDLE_CLASS;
        idle_ready = RegisterClassA(&wc) != 0;
        if (!idle_ready) return NULL;
    }
    HWND w = CreateWindowExA(0, IDLE_CLASS, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, dll, NULL);
    if (!w) return NULL;
    SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)e);
    SetTimer(w, 1, 100, NULL);
    return w;
}
static void close_idle(ed_t *e) {
    if (!e || !e->idle) return;
    KillTimer(e->idle, 1);
    SetWindowLongPtrA(e->idle, GWLP_USERDATA, 0);
    DestroyWindow(e->idle);   /* (only works on its own thread; elsewhere it stays inert: its pointer is cleared) */
    e->idle = NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved) {
    (void)inst;
    if (why == DLL_PROCESS_DETACH && !reserved) free_window_class();   /* FreeLibrary (not the process ending) */
    return TRUE;
}

/* Windows 11 rounds the corners of top-level windows, so the plugin window a host makes around the editor shows the
 * panel with rounded corners; the instrument is square. Ask for square corners (DWMWA_WINDOW_CORNER_PREFERENCE =
 * DWMWCP_DONOTROUND), but only when that window holds just the editor (its client area our size, give or take a few
 * pixels), never a DAW's own main window. dwmapi is looked up at run time, so older Windows (no such setting, or no
 * DWM) simply keeps its look. 1: done, 0: not this window (yet), -1: Windows said no. */
static int square_corners(HWND wnd) {
    typedef HRESULT(WINAPI * dwm_set_t)(HWND, DWORD, LPCVOID, DWORD);
    static dwm_set_t set;
    static int looked;
    HWND root = wnd ? GetAncestor(wnd, GA_ROOT) : NULL;
    RECT c;
    if (!root || root == wnd || !GetClientRect(root, &c)) return 0;
    if (abs(rw(&c) - L.w) > 8 || abs(rh(&c) - L.h) > 8) return 0;
    if (!looked) {
        looked = 1;
        HMODULE m = LoadLibraryExA("dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m) set = (dwm_set_t)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
    }
    if (!set) return -1;
    int pref = 1;   /* DWMWCP_DONOTROUND */
    return set(root, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof pref) == S_OK ? 1 : -1;
}

/* ---- the VST2 side ---- */
static intptr_t dispatcher(AEffect *fx, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt) {
    ed_t *e = ed_of(fx);
    switch (op) {
    case effEditGetRect: {
        static ERect r;
        layout();
        r.top = 0; r.left = 0; r.bottom = (int16_t)L.h; r.right = (int16_t)L.w;
        if (ptr) *(ERect **)ptr = &r;
        return 1;
    }
    case effEditOpen: {
        if (!e || !ptr) return 0;
        layout();
        init_window_class();
        if (e->wnd) DestroyWindow(e->wnd);
        e->hover = e->drag = e->key = -1;
        e->mode = M_NONE;
        patches_rescan(e);
        e->wnd = CreateWindowExA(0, CLASS_NAME, "ONE-ish", WS_CHILD | WS_VISIBLE, 0, 0, L.w, L.h, (HWND)ptr, NULL, dll, e);
        e->corner_tries = 0;
        e->corners = square_corners(e->wnd);   /* (again from the timer: some hosts size their window afterwards) */
        send_kb_oct(e);   /* (2026-10-30) the engine follows this window's keyboard octave */
        return e->wnd != NULL;
    }
    case effEditClose:
        if (e && e->wnd) { DestroyWindow(e->wnd); e->wnd = NULL; }
        return 1;
    case effEditIdle:
        return 0;
    case VENDOR_SPECIFIC:   /* load_test: the MIDI the window passed to the engine since the last call (oldest first) */
        if (idx == TEST_CORNERS && e) return e->corners;   /* load_test: did the window ask for square corners? */
        if (idx == TEST_MIDI_LOG && e && ptr) {
            unsigned n = e->mlog_n < MLOG ? e->mlog_n : MLOG;
            for (unsigned k = 0; k < n; k++) memcpy((unsigned char *)ptr + 3 * k, e->mlog[(e->mlog_n - n + k) % MLOG], 3);
            e->mlog_n = 0;
            return (intptr_t)n;
        }
        if (idx == TEST_PATCH && e && ptr) return patches_test(e, v, ptr, opt);   /* load_test: user patches */
        if (idx == TEST_LIB && e && ptr) return lib_test(e, v, ptr, opt);         /* load_test: the library */
        if (idx == TEST_FOCUS && e) {   /* load_test: the Panel Focus, as the SYNTH buttons (with Ctrl) set it */
            if (v > 0) {
                set_focus(e, (int)lroundf(opt), 0);
                e->focus_mask = (unsigned)v | 1u << e->focus;
            }
            return e->focus + 4 * (intptr_t)e->focus_mask;
        }
        if (idx == TEST_QM && e && ptr) return qm_test(e, v, ptr, opt);   /* load_test: quick assign */
        if (idx == TEST_SEQ && e && ptr) return seq_test(e, v, ptr);   /* load_test: the step sequencer's page */
        if (idx == TEST_SETTINGS && e && ptr) return settings_test(e, v, ptr, opt);   /* load_test: the settings */
        if (idx == TEST_HUB && e) return hub_test(e, v, ptr, opt);   /* load_test: TEMPO, GEN OFF, LINK (hub.c) */
        if (idx == TEST_LEGACY && e && e->hr) {   /* load_test: the old 44.1 kHz int16 path, to compare against */
            hr_set_legacy(e->hr, (int)v);
            fx->initialDelay = hr_latency(e->hr);
            return 1;
        }
        if (idx == TEST_INPUT && e && e->hr)   /* load_test: the audio input's delay (x 1000), or its dry frames */
            return v ? (intptr_t)hr_input_dry(e->hr) : (intptr_t)llround(hr_input_delay(e->hr) * 1000.0);
        break;
    case effSetSampleRate:   /* the converter follows the host's rate; its latency goes to the host */
        if (e && e->hr) fx->initialDelay = hr_set_rate(e->hr, opt);
        break;
    case effMainsChanged:    /* resume: the converter starts clean */
        if (e && e->hr && v) hr_restart(e->hr);
        break;
    case effProcessEvents:   /* host MIDI waits for the engine block nearest its time (hostrate.c) */
        if (e && ptr) lib_midi_events(e, ptr);   /* a program change for a user patch: noted, loaded on the UI thread */
        if (e && ptr) settings_midi_seen(e, ptr);   /* (MIDI LEARN listens: the last CC, lock-free) */
        if (e && e->hr && ptr && hr_events(e->hr, fx, ptr)) return 1;
        break;
    case effSetProgram:   /* the host's or the browser's: the patch shown follows (patches.c) */
        if (e) {
            int before = (int)wrapped_dispatch(fx, effGetProgram, 0, 0, NULL, 0);
            /* on the window's thread: an unsaved edit autosaved and the load an undo step (never on another thread) */
            int ui = e->lib && GetCurrentThreadId() == e->lib->ui_thread && v != before;
            char what[64] = "";
            if (ui) wrapped_dispatch(fx, effGetProgramNameIndexed, (int32_t)v, 0, what, 0);
            if (ui) lib_before_load(e, what);
            float keep[4];
            int held = settings_midi_keep(e, keep);   /* MIDI I/O FROM PRESET off: the channels stay */
            intptr_t r = wrapped_dispatch(fx, op, idx, v, ptr, opt);
            if (held) settings_midi_put(e, keep);
            patches_program_set(e, before);
            if (ui) lib_after_load(e);
            return r;
        }
        break;
    case effGetChunk:   /* the project: the engine's state plus which patch it is */
        if (e && ptr) return patches_get_chunk(e, ptr);
        break;
    case effSetChunk:
        if (e) {
            intptr_t r = wrapped_dispatch(fx, op, idx, v, ptr, opt);
            if (r) patches_chunk_set(e, ptr, v);
            return r;
        }
        break;
    case 1: {   /* effClose: the instance goes away; forget it after the wrapper is done */
        if (e && e->wnd) { DestroyWindow(e->wnd); e->wnd = NULL; }
        close_idle(e);
        intptr_t r = wrapped_dispatch(fx, op, idx, v, ptr, opt);
        for (int i = 0; i < MAX_INSTANCES; i++)
            if (eds[i] == e) eds[i] = NULL;   /* the slot stays allocated: see pool */
        if (e) { patches_free(e); e->hr = NULL; }   /* (the wrapper freed it with the engine) */
        int left = 0;
        for (int i = 0; i < MAX_INSTANCES; i++) left |= eds[i] != NULL;
        if (!left) free_window_class();   /* the last instance: nothing of this DLL stays registered */
        return r;
    }
    }
    return wrapped_dispatch(fx, op, idx, v, ptr, opt);
}

__declspec(dllexport) AEffect *VSTPluginMain(audioMasterCallback host) {
    AEffect *fx = one_ish_wrapped_main(host);
    hostrate_t *hr = hr_last_created();   /* the engine instance the wrapper just made (hostrate.c) */
    if (!fx) return NULL;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (eds[i]) continue;
        ed_t *e = &pool[i];
        memset(e, 0, sizeof *e);
        e->fx = fx;
        e->host = host;
        e->hover = e->drag = e->key = -1;
        e->shown_prog = -1;
        e->focus_mask = 1u;   /* Synth 1 in focus */
        e->screen = SCR_MATRIX;
        e->bend = 0;
        e->pad_x = 0.5f;
        e->ribbon = 0.5f;
        e->ribbon_cc = -1;
        e->rnd_amount = 0.2f;   /* (WILD 0, no ticks: rnd_wild and rnd_also start at 0) */
        e->qm.dst = e->qm.ctl = e->qm.last_row = e->qm.last_dst = e->qm.mark = -1;   /* (quick assign: nothing yet) */
        e->hr = hr;
        eds[i] = e;
        wrapped_dispatch = fx->dispatcher;
        wrapped_replacing = fx->processReplacing;
        wrapped_process = fx->process;
        fx->dispatcher = dispatcher;
        fx->processReplacing = hooked_replacing;
        fx->process = hooked_process;
        fx->flags |= effFlagsHasEditor;
        lib_new(e);   /* (before patches_init: it records the saved version) */
        e->idle = make_idle(e);
        if (hr) fx->numInputs = 2;   /* a stereo audio input (hostrate.c): the EXT channel and the vocoders */
        patches_init(e);
        seq_init(e);
        settings_init(e);   /* the settings file (once per process) into this instance's engine */
        break;
    }
    return fx;   /* no free slot: the plugin still works, without its window, at 44.1 kHz in 16 bits (the old path) */
}
