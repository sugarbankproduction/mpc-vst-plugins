/* Desktop editor window for ONE-ish (Windows VST2 build only; the MPC uses its own touchscreen skin).
 * A front panel laid out like the instrument it is inspired by: LFOs, oscillators, their modulation, noise and
 * polyphony on the left; a centre console with a screen (presets, the 16-row mod matrix, the modules' MORE pages, help
 * for the control under the mouse); mixer, filters, envelopes, effects and output on the right; and along the bottom a
 * left-hand controller (octave, glide, pitch and mod wheels, an X/Y pad, bend range), a ribbon and a 61-key keyboard
 * that play the synth. Wooden end cheeks. All artwork is drawn from scratch (editor_draw.c: GDI text, GDI+ shapes via
 * gfx.h): no maker's names, marks or photos. This file: the layout (scaled to fit the screen; ONEISH_EDITOR_SCALE
 * overrides it), the mouse, the MIDI from the window, and the VST2 side.
 *
 * MORE pages, as on the instrument: a module with a small triangle in its top-right corner has second-layer controls;
 * a click on the triangle shows them on the centre screen (the MORE button lists every page, ANALOG opens the analog
 * character page). A page can show some items only for one option of another control (the synth effect's page lists
 * the controls of the selected effect type); a page with more than 16 items has sub-pages (< > on its title line).
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
 * and CC 17 (127 while touched, 0 on release); the engine decides what they do (spring or hold). */
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"

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

static ed_t *ed_of(AEffect *fx) {
    for (int i = 0; i < MAX_INSTANCES; i++)
        if (eds[i] && eds[i]->fx == fx) return eds[i];
    return NULL;
}

/* ---- MIDI from the window: the UI thread writes, the audio thread reads (no locks, no allocation) ---- */
static void ui_midi(ed_t *e, int status, int d1, int d2) {
    unsigned w = e->mq_w, r = __atomic_load_n(&e->mq_r, __ATOMIC_ACQUIRE);
    if (w - r >= MQ) return;   /* full (the host isn't processing): drop */
    e->mq[w % MQ][0] = (unsigned char)status;
    e->mq[w % MQ][1] = (unsigned char)(d1 & 127);
    e->mq[w % MQ][2] = (unsigned char)(d2 & 127);
    __atomic_store_n(&e->mq_w, w + 1, __ATOMIC_RELEASE);
}

static void drain_ui_midi(AEffect *fx) {   /* audio thread, before each block */
    ed_t *e = ed_of(fx);
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

static void hooked_replacing(AEffect *fx, float **in, float **out, int32_t n) {
    drain_ui_midi(fx);
    wrapped_replacing(fx, in, out, n);
}
static void hooked_process(AEffect *fx, float **in, float **out, int32_t n) {
    drain_ui_midi(fx);
    wrapped_process(fx, in, out, n);
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
    L.encoder = zone(ZONE_ENCODER);
    L.bprev = zone(ZONE_PREV);
    L.bnext = zone(ZONE_NEXT);
    L.brand = zone(ZONE_BRAND);
    L.oct_leds = zone(ZONE_OCT_LEDS);
    L.oct_down = zone(ZONE_OCT_DOWN);
    L.oct_up = zone(ZONE_OCT_UP);
    L.pitch = zone(ZONE_PITCH);
    L.modw = zone(ZONE_MODW);
    L.pad = zone(ZONE_PAD);
    L.keys = zone(ZONE_KEYS);
    L.ribbon = zone(ZONE_RIBBON);
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

/* ---- values ---- */
void param_text(ed_t *e, int i, char *out, int n) {
    char disp[64] = "", lab[32] = "";
    e->fx->dispatcher(e->fx, effGetParamDisplay, i, 0, disp, 0);
    e->fx->dispatcher(e->fx, effGetParamLabel, i, 0, lab, 0);
    const char *d = disp;
    while (*d == ' ') d++;
    snprintf(out, n, lab[0] && PARAMS[i].nopts == 0 ? "%s %s" : "%s", d, lab);
}

static void set_value(ed_t *e, int i, float v) {
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    e->fx->setParameter(e->fx, i, v);
    if (e->host) e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void edit_value(ed_t *e, int i, float v) {   /* a one-shot change, bracketed for the host's undo/automation */
    if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
    set_value(e, i, v);
    if (e->host) e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
}

static float option_norm(int i, int opt) { return PARAMS[i].nopts > 1 ? (float)opt / (PARAMS[i].nopts - 1) : 0.0f; }
int option_of(ed_t *e, int i) {
    return PARAMS[i].nopts > 1 ? (int)lroundf(e->fx->getParameter(e->fx, i) * (PARAMS[i].nopts - 1)) : 0;
}
float real_of(ed_t *e, int i) {   /* the value in the param's own units */
    return PARAMS[i].min + e->fx->getParameter(e->fx, i) * (PARAMS[i].max - PARAMS[i].min);
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
    int items[512], n = e->screen == SCR_MATRIX ? NMATRIX : e->screen == SCR_INDEX ? NPAGES : page_items(e, e->screen, items);
    int per = e->screen == SCR_MATRIX ? ROWS : SLOTS;
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
    if (e->screen == SCR_MATRIX) {
        for (int r = 0; r < ROWS && e->sub * ROWS + r < NMATRIX; r++)
            for (int k = 0; k < MX_COLS; k++) {
                RECT c = matrix_cell(r, k + 1);
                int i = MATRIX[e->sub * ROWS + r][k];
                if (i >= 0 && PtInRect(&c, pt)) { e->hit_screen = 1; e->hit_rect = c; return i; }
            }
    } else if (e->screen == SCR_INDEX) {
        for (int p = e->sub * SLOTS; p < NPAGES && p < (e->sub + 1) * SLOTS; p++) {
            RECT nm, c;
            slot_rects(p % SLOTS, &nm, &c);
            RECT all = {nm.left, nm.top, c.right, nm.bottom};
            if (PtInRect(&all, pt)) return Z_PAGE + p;
        }
    } else {
        int items[512], n = page_items(e, e->screen, items);
        for (int j = e->sub * SLOTS; j < n && j < (e->sub + 1) * SLOTS; j++) {
            RECT nm, c;
            slot_rects(j % SLOTS, &nm, &c);
            if (PtInRect(&c, pt)) { e->hit_screen = 1; e->hit_rect = c; return ITEMS[items[j]].param; }
        }
    }
    return -1;
}

static int hit(ed_t *e, int x, int y) {
    POINT pt = {x, y};
    e->hit_screen = 0;
    for (int k = 0; k < NMORES; k++)
        if (PtInRect(&L.more[k], pt)) return Z_PAGE + MORES[k].page;
    for (int c = 0; c < NCTLS; c++)
        if (PtInRect(&L.ctl[CTLS[c].param], pt)) { e->hit_rect = L.ctl[CTLS[c].param]; return CTLS[c].param; }
    int s = hit_on_screen(e, pt);
    if (s >= 0) return s;
    static const struct { RECT *r; int z; } Z[] = {
        {&L.keys, Z_KEYS}, {&L.ribbon, Z_RIBBON}, {&L.pitch, Z_PITCH}, {&L.modw, Z_MODW}, {&L.pad, Z_PAD},
        {&L.oct_down, Z_OCT_DOWN}, {&L.oct_up, Z_OCT_UP}, {&L.prev, Z_PREV}, {&L.next, Z_NEXT}, {&L.bprev, Z_PREV},
        {&L.bnext, Z_NEXT}, {&L.name, Z_NAME}, {&L.encoder, Z_ENCODER}, {&L.tab_matrix, Z_TAB_MATRIX},
        {&L.tab_more, Z_TAB_MORE}, {&L.tab_analog, Z_TAB_ANALOG}};
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

static void set_program(ed_t *e, int p) {
    if (p < 0 || p >= e->fx->numPrograms) return;
    e->fx->dispatcher(e->fx, effSetProgram, 0, p, NULL, 0);
    if (e->host) e->host(e->fx, audioMasterUpdateDisplay, 0, 0, NULL, 0);
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void pick_program(ed_t *e) {
    HMENU m = CreatePopupMenu();
    int cur = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
    for (int p = 0; p < e->fx->numPrograms; p++) {
        char name[64] = "", item[80];
        e->fx->dispatcher(e->fx, effGetProgramNameIndexed, p, 0, name, 0);
        snprintf(item, sizeof item, "%03d  %s", p + 1, name);
        AppendMenuA(m, MF_STRING | (p == cur ? MF_CHECKED : 0) | (p && p % 30 == 0 ? MF_MENUBARBREAK : 0),
                    (UINT_PTR)(p + 1), item);
    }
    POINT pt = {L.name.left, L.name.bottom};
    ClientToScreen(e->wnd, &pt);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    DestroyMenu(m);
    if (choice > 0) set_program(e, choice - 1);
}

static void key_off(ed_t *e) {
    if (e->key >= 0) ui_midi(e, 0x80, e->key, 0);
    e->key = -1;
}

static void controller_move(ed_t *e, int x, int y) {   /* a drag on the keys, a wheel, the pad or the ribbon */
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
        ui_midi(e, 0xB0, 2, (int)lroundf(e->pad_x * 127));
        ui_midi(e, 0xD0, (int)lroundf(e->pad_y * 127), 0);
    } else if (e->mode == M_RIBBON) {   /* the position (only when it changes), then the touch on the first move */
        float v = (x - L.ribbon.left) / (float)rw(&L.ribbon);
        e->ribbon = v < 0 ? 0 : v > 1 ? 1 : v;
        int cc = (int)lroundf(e->ribbon * 127);
        if (cc != e->ribbon_cc) ui_midi(e, 0xB0, RIBBON_CC, cc);
        if (!e->ribbon_on) ui_midi(e, 0xB0, RIBBON_TOUCH_CC, 127);
        e->ribbon_cc = cc;
        e->ribbon_on = 1;
    } else if (e->mode == M_ENC) {
        int steps = (e->enc_y - y) / sc(18);
        if (steps) {
            e->enc_moved = 1;
            e->enc_y -= steps * sc(18);
            set_program(e, e->shown_prog + steps);
            e->shown_prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
        }
    }
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void controller_release(ed_t *e) {   /* the mouse let go (or lost): notes off, the pitch wheel springs back */
    int m = e->mode;
    e->mode = M_NONE;   /* first: the preset list below takes the mouse, which comes back here */
    if (m == M_KEY) key_off(e);
    if (m == M_PITCH) { e->bend = 0; ui_midi(e, 0xE0, 0, 64); }
    if (m == M_PAD) { e->pad_on = 0; ui_midi(e, 0xD0, 0, 0); }
    if (m == M_RIBBON) { e->ribbon_on = 0; e->ribbon_cc = -1; ui_midi(e, 0xB0, RIBBON_TOUCH_CC, 0); }
    if (m == M_ENC && !e->enc_moved) pick_program(e);
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void set_octave(ed_t *e, int oct) {
    if (oct < -2 || oct > 2) return;
    key_off(e);
    e->oct = oct;
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void press_zone(ed_t *e, int z) {   /* the console buttons, MORE triangles, page entries, sub-page arrows */
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
    } else if (z == Z_SUB_PREV) {
        show(e, e->screen, (e->sub + subs - 1) % subs);
    } else if (z == Z_SUB_NEXT) {
        show(e, e->screen, (e->sub + 1) % subs);
    }
}

static void press(ed_t *e, int x, int y) {
    int i = hit(e, x, y);
    switch (i) {
    case -1: return;
    case Z_PREV: set_program(e, e->shown_prog - 1); return;
    case Z_NEXT: set_program(e, e->shown_prog + 1); return;
    case Z_NAME: pick_program(e); return;
    case Z_OCT_DOWN: set_octave(e, e->oct - 1); return;
    case Z_OCT_UP: set_octave(e, e->oct + 1); return;
    case Z_KEYS: e->mode = M_KEY; break;
    case Z_PITCH: e->mode = M_PITCH; break;
    case Z_MODW: e->mode = M_MODW; break;
    case Z_PAD: e->mode = M_PAD; e->pad_on = 1; break;
    case Z_RIBBON: e->mode = M_RIBBON; e->ribbon_cc = -1; break;
    case Z_ENCODER: e->mode = M_ENC; e->enc_y = y; e->enc_moved = 0; break;
    default:
        if (i >= Z_KEYS) { press_zone(e, i); return; }
        break;
    }
    if (e->mode != M_NONE) {
        SetCapture(e->wnd);
        controller_move(e, x, y);
        return;
    }
    int screen = e->hit_screen, n = PARAMS[i].nopts;
    RECT r = e->hit_rect;
    int k = screen ? (n ? K_MENU : K_AMOUNT) : L.kind[i];
    if (k == K_KNOB || k == K_SMALL || k == K_BIG || k == K_AMOUNT || (k == K_OCT && n == 0)) {
        e->drag = i;
        e->mode = M_PARAM;
        e->drag_horiz = k == K_AMOUNT;
        e->drag_x = x;
        e->drag_y = y;
        e->drag_from = e->fx->getParameter(e->fx, i);
        SetCapture(e->wnd);
        if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
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
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    ed_t *e = (ed_t *)GetWindowLongPtrA(w, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        SetTimer(w, 1, 50, NULL);
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
        int changed = 0, prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
        for (int i = 0; i < NPARAMS; i++) {
            float v = e->fx->getParameter(e->fx, i);
            if (v != e->shown[i]) { e->shown[i] = v; changed = 1; }
        }
        if (prog != e->shown_prog) { e->shown_prog = prog; changed = 1; }
        if (changed) InvalidateRect(w, NULL, FALSE);
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
        if (e->mode != M_NONE) { controller_move(e, x, y); return 0; }
        int h = hit(e, x, y);
        if (h != e->hover) { e->hover = h; InvalidateRect(w, NULL, FALSE); }
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, w, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
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
        int i = hit(e, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i >= 0 && i < NPARAMS && PARAMS[i].nopts > 1) pick_option(e, i, e->hit_rect);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (!e) return 0;
        if (e->mode == M_PARAM) {
            if (e->host && e->drag >= 0) e->host(e->fx, audioMasterEndEdit, e->drag, 0, NULL, 0);
            e->drag = -1;
            e->mode = M_NONE;
        } else if (e->mode != M_NONE) {
            controller_release(e);
        }
        if (msg == WM_LBUTTONUP && GetCapture() == w) ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK: {   /* back to the default */
        if (!e) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), i = hit(e, x, y);
        if (i >= 0 && i < NPARAMS && PARAMS[i].nopts == 0) edit_value(e, i, PARAMS[i].def);
        else if (i >= 0) press(e, x, y);   /* a second click on a button or a key counts as a click */
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!e) return 0;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(w, &pt);
        int i = hit(e, pt.x, pt.y), dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (i == Z_ENCODER) { set_program(e, e->shown_prog - dir); return 0; }
        if (i < 0 || i >= NPARAMS) {   /* over the screen: its sub-pages */
            int subs = screen_subs(e);
            if (PtInRect(&L.screen, pt) && subs > 1) show(e, e->screen, (e->sub + subs - dir) % subs);
            return 0;
        }
        float v = e->fx->getParameter(e->fx, i);
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
            e->mode = M_NONE;
        }
        KillTimer(w, 1);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static HFONT font(float px, int weight, const char *face) {
    int h = (int)lroundf(px * L.s);
    return CreateFontA(-(h < 7 ? 7 : h), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

static void init_window_class(void) {
    static int done;
    if (done) return;
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
    done = 1;
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
        e->wnd = CreateWindowExA(0, CLASS_NAME, "ONE-ish", WS_CHILD | WS_VISIBLE, 0, 0, L.w, L.h, (HWND)ptr, NULL, dll, e);
        return e->wnd != NULL;
    }
    case effEditClose:
        if (e && e->wnd) { DestroyWindow(e->wnd); e->wnd = NULL; }
        return 1;
    case effEditIdle:
        return 0;
    case VENDOR_SPECIFIC:   /* load_test: the MIDI the window passed to the engine since the last call (oldest first) */
        if (idx == TEST_MIDI_LOG && e && ptr) {
            unsigned n = e->mlog_n < MLOG ? e->mlog_n : MLOG;
            for (unsigned k = 0; k < n; k++) memcpy((unsigned char *)ptr + 3 * k, e->mlog[(e->mlog_n - n + k) % MLOG], 3);
            e->mlog_n = 0;
            return (intptr_t)n;
        }
        break;
    case 1: {   /* effClose: the instance goes away; forget it after the wrapper is done */
        if (e && e->wnd) { DestroyWindow(e->wnd); e->wnd = NULL; }
        intptr_t r = wrapped_dispatch(fx, op, idx, v, ptr, opt);
        for (int i = 0; i < MAX_INSTANCES; i++)
            if (eds[i] == e) eds[i] = NULL;   /* the slot stays allocated: see pool */
        return r;
    }
    }
    return wrapped_dispatch(fx, op, idx, v, ptr, opt);
}

__declspec(dllexport) AEffect *VSTPluginMain(audioMasterCallback host) {
    AEffect *fx = one_ish_wrapped_main(host);
    if (!fx) return NULL;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (eds[i]) continue;
        ed_t *e = &pool[i];
        memset(e, 0, sizeof *e);
        e->fx = fx;
        e->host = host;
        e->hover = e->drag = e->key = -1;
        e->shown_prog = -1;
        e->screen = SCR_MATRIX;
        e->bend = 0;
        e->pad_x = 0.5f;
        e->ribbon = 0.5f;
        e->ribbon_cc = -1;
        eds[i] = e;
        wrapped_dispatch = fx->dispatcher;
        wrapped_replacing = fx->processReplacing;
        wrapped_process = fx->process;
        fx->dispatcher = dispatcher;
        fx->processReplacing = hooked_replacing;
        fx->process = hooked_process;
        fx->flags |= effFlagsHasEditor;
        break;
    }
    return fx;   /* no free slot: the plugin still works, just without its window */
}
