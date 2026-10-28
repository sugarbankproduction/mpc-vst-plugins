/* Desktop editor window for ONE-ish (Windows VST2 build only; the MPC uses its own touchscreen skin).
 * A front panel laid out like the instrument it is inspired by: LFOs, oscillators, their modulation, noise and
 * polyphony on the left; a centre console with a screen (presets, the 16-row mod matrix, the modules' MORE pages, help
 * for the control under the mouse); mixer, filters, envelopes, effects and output on the right; and along the bottom a
 * left-hand controller (octave, glide, pitch and mod wheels, an X/Y pad, bend range), a ribbon and a 61-key keyboard
 * that play the synth. Wooden end cheeks. All artwork is drawn from scratch (editor_draw.c: GDI text, GDI+ shapes via
 * gfx.h): no maker's names, marks or photos. This file: the layout (scaled to fit the screen; ONEISH_EDITOR_SCALE
 * overrides it), the mouse, the MIDI from the window, and the VST2 side.
 *
 * RANDOM (the console button next to ANALOG) shows the patch randomiser on the screen (src/randomize.h): RANDOMISE makes
 * a new sound, MUTATE a variation of this one by AMOUNT, the lock buttons keep sections as they are, UNDO / REDO step
 * through the last RND_HIST sounds. Each result is set param by param through the same path as a knob (setParameter,
 * audioMasterAutomate between begin/end edit), so the host and the window follow it.
 *
 * MORE pages, as on the instrument: a module with a small triangle in its top-right corner has second-layer controls;
 * a click on the triangle shows them on the centre screen (the MORE button lists every page, ANALOG opens the analog
 * character page). A page can show some items only for one option of another control (the synth effect's page lists
 * the controls of the selected effect type); a page with more than 16 items has sub-pages (< > on its title line).
 *
 * Patches (patches.c): the name line, < >, PREV/NEXT and the PRESET knob browse the factory presets then the owner's
 * own patch files; a click on the name opens the PATCHES page (save, save as, rename, delete, import, export).
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
#include "editor.h"
#include "hostrate.h"
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

/* ---- MIDI from the window: the UI thread writes, the audio thread reads (no locks, no allocation) ---- */
static void ui_midi(ed_t *e, int status, int d1, int d2) {
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
 * rate; the old path only for load_test's comparison (TEST_LEGACY) */
static void hooked_replacing(AEffect *fx, float **in, float **out, int32_t n) {
    ed_t *e = ed_of(fx);
    drain_ui_midi(e, fx);
    if (e && e->hr && hr_on(e->hr)) hr_process(e->hr, fx, wrapped_replacing, out, n, 0);
    else wrapped_replacing(fx, in, out, n);
}
static void hooked_process(AEffect *fx, float **in, float **out, int32_t n) {
    ed_t *e = ed_of(fx);
    drain_ui_midi(e, fx);
    if (e && e->hr && hr_on(e->hr)) hr_process(e->hr, fx, wrapped_replacing, out, n, 1);
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
    L.seq_leds = zone(ZONE_SEQ_LEDS);
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
    L.euclid = zone(ZONE_EUCLID);
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

/* the randomiser page: RANDOMISE and MUTATE side by side (rows 0-1), the amount bar (row 2, right), the section locks
 * (rows 3-6, two columns), UNDO and REDO (row 7, left) */
RECT rnd_rect(int z) {
    int half = (rw(&L.row[0]) - sc(8)) / 2, xl = L.row[0].left, xr = xl + half + sc(8);
    RECT r = {0, 0, 0, 0};
    if (z == Z_RND_GO || z == Z_RND_MUTATE) {
        int x = z == Z_RND_GO ? xl : xr;
        SetRect(&r, x, L.row[0].top + sc(3), x + half, L.row[1].bottom - sc(3));
    } else if (z == Z_RND_AMOUNT) {
        SetRect(&r, xr + sc(54), L.row[2].top + sc(4), xr + half - sc(40), L.row[2].bottom - sc(4));
    } else if (z >= Z_RND_LOCK && z < Z_RND_LOCK + RS_COUNT) {
        int k = z - Z_RND_LOCK, x = k < 4 ? xl : xr, row = 3 + k % 4;
        SetRect(&r, x, L.row[row].top + sc(2), x + half, L.row[row].bottom - sc(2));
    } else if (z == Z_RND_UNDO || z == Z_RND_REDO) {
        int q = (half - sc(8)) / 2, x = xl + (z == Z_RND_REDO ? q + sc(8) : 0);
        SetRect(&r, x, L.row[7].top + sc(2), x + q, L.row[7].bottom - sc(2));
    }
    return r;
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

/* ---- the randomiser: results go in param by param like knob moves; a history of sounds for UNDO / REDO ---- */
static void rnd_snapshot(ed_t *e, float *v) {
    for (int i = 0; i < NPARAMS; i++) v[i] = e->fx->getParameter(e->fx, i);
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
static void rnd_action(ed_t *e, int mutate) {   /* RANDOMISE (0) or MUTATE (1) */
    float cur[NPARAMS], out[NPARAMS];
    char name[40];
    if (!e->rnd_seed) e->rnd_seed = ((uint32_t)GetTickCount() * 2654435761u) ^ (uint32_t)(uintptr_t)e ^ 0x2545F491u;
    if (!e->rnd_seed) e->rnd_seed = 1;
    rnd_snapshot(e, cur);
    if (e->hist_n == 0 || !rnd_same(cur, e->hist[e->hist_pos])) {   /* the sound before, to come back to */
        char prog[32] = "";
        int p = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
        e->fx->dispatcher(e->fx, effGetProgramNameIndexed, p, 0, prog, 0);
        if (e->hist_n) snprintf(name, sizeof name, "Edited");
        else snprintf(name, sizeof name, "Start: %s", prog);
        rnd_push(e, cur, name);
    }
    if (mutate) {
        rnd_mutate(cur, out, e->rnd_amount, e->rnd_locks, &e->rnd_seed);
        snprintf(name, sizeof name, "Mutation %d (%.0f%%)", ++e->mut_count, e->rnd_amount * 100.0f);
    } else {
        int a = rnd_generate(cur, out, e->rnd_locks, &e->rnd_seed);
        snprintf(name, sizeof name, "Random %d: %s", ++e->rnd_count, RND_ARCH[a]);
    }
    rnd_apply(e, out);
    rnd_snapshot(e, cur);   /* as the plugin took it */
    rnd_push(e, cur, name);
    InvalidateRect(e->wnd, NULL, FALSE);
}
static void rnd_step(ed_t *e, int dir) {   /* UNDO (-1) / REDO (+1) through the history */
    float cur[NPARAMS];
    if (!e->hist_n) return;
    rnd_snapshot(e, cur);
    if (!rnd_same(cur, e->hist[e->hist_pos])) rnd_push(e, cur, "Edited");   /* changed by hand since: keep that too */
    int to = e->hist_pos + dir;
    if (to < 0 || to >= e->hist_n) return;
    e->hist_pos = to;
    rnd_apply(e, e->hist[to]);
    InvalidateRect(e->wnd, NULL, FALSE);
}
static void rnd_set_amount(ed_t *e, int x) {   /* the amount bar: 0 .. 100% across it */
    RECT b = rnd_rect(Z_RND_AMOUNT);
    float v = (x - b.left) / (float)(rw(&b) > 0 ? rw(&b) : 1);
    e->rnd_amount = roundf((v < 0 ? 0 : v > 1 ? 1 : v) * 100.0f) / 100.0f;
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
    if (e->screen == SCR_PATCHES) return patches_subs(e);
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
    if (e->screen == SCR_RANDOM) {
        static const int Z[] = {Z_RND_GO, Z_RND_MUTATE, Z_RND_AMOUNT, Z_RND_UNDO, Z_RND_REDO};
        for (unsigned k = 0; k < sizeof Z / sizeof Z[0]; k++) {
            RECT r = rnd_rect(Z[k]);
            if (Z[k] == Z_RND_AMOUNT) InflateRect(&r, sc(4), sc(4));   /* easier to catch */
            if (PtInRect(&r, pt)) return Z[k];
        }
        for (int k = 0; k < RS_COUNT; k++) {
            RECT r = rnd_rect(Z_RND_LOCK + k);
            if (PtInRect(&r, pt)) return Z_RND_LOCK + k;
        }
    } else if (e->screen == SCR_MATRIX) {
        for (int r = 0; r < ROWS && e->sub * ROWS + r < NMATRIX; r++)
            for (int k = 0; k < MX_COLS; k++) {
                RECT c = matrix_cell(r, k + 1);
                int i = MATRIX[e->sub * ROWS + r][k];
                if (i >= 0 && PtInRect(&c, pt)) { e->hit_screen = 1; e->hit_rect = c; return i; }
            }
    } else if (e->screen == SCR_PATCHES) {
        return patches_hit(e, pt);
    } else if (e->screen == SCR_SEQ) {
        return seq_hit(e, pt);
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
    if ((s = seq_hit_panel(e, pt)) >= 0) return s;   /* the SEQUENCER module's page LEDs */
    static const struct { RECT *r; int z; } Z[] = {
        {&L.keys, Z_KEYS}, {&L.ribbon, Z_RIBBON}, {&L.pitch, Z_PITCH}, {&L.modw, Z_MODW}, {&L.pad, Z_PAD},
        {&L.oct_down, Z_OCT_DOWN}, {&L.oct_up, Z_OCT_UP}, {&L.prev, Z_PREV}, {&L.next, Z_NEXT}, {&L.bprev, Z_PREV},
        {&L.bnext, Z_NEXT}, {&L.name, Z_NAME}, {&L.encoder, Z_ENCODER}, {&L.tab_matrix, Z_TAB_MATRIX},
        {&L.tab_more, Z_TAB_MORE}, {&L.tab_analog, Z_TAB_ANALOG}, {&L.tab_random, Z_TAB_RANDOM}, {&L.tab_seq, Z_TAB_SEQ},
        {&L.euclid, Z_EUCLID}};   /* (an empty rect when there is no GENERATIVE section) */
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
    } else if (e->mode == M_RND_AMOUNT) {
        rnd_set_amount(e, x);
    } else if (e->mode == M_ENC) {
        int steps = (e->enc_y - y) / sc(18);
        if (steps) {
            e->enc_moved = 1;
            e->enc_y -= steps * sc(18);
            patches_step(e, steps);
        }
    }
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void controller_release(ed_t *e) {   /* the mouse let go (or lost): notes off, the pitch wheel springs back */
    int m = e->mode;
    e->mode = M_NONE;   /* first: the preset list below takes the mouse, which comes back here */
    if (m == M_KEY) key_off(e);
    if (m == M_PITCH) { e->bend = 0; ui_midi(e, 0xE0, 0, 64); }
    if (m == M_PAD) { e->pad_on = 0; ui_midi(e, 0xD0, 0, 0); ui_midi(e, 0xB0, PAD_TOUCH_CC, 0); }
    if (m == M_RIBBON) { e->ribbon_on = 0; e->ribbon_cc = -1; ui_midi(e, 0xB0, RIBBON_TOUCH_CC, 0); }
    if (m == M_ENC && !e->enc_moved) patches_open(e);   /* a click on the preset knob: the patch list */
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void set_octave(ed_t *e, int oct) {
    if (oct < -2 || oct > 2) return;
    key_off(e);
    e->oct = oct;
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void press_zone(ed_t *e, int z) {   /* the console buttons, MORE triangles, page entries, sub-page arrows */
    if (patches_press(e, z)) return;   /* the PATCHES page's buttons and entries */
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
    } else if (z == Z_TAB_SEQ) {   /* the step editor; again: its other view (NOTES / MODS) */
        if (e->screen == SCR_SEQ) e->sq.view = !e->sq.view;
        seq_open(e, -1);
    } else if (z == Z_EUCLID && PG_GEN >= 0) {   /* the ring of dots: the GENERATIVE section's page */
        show(e, PG_GEN, 0);
    } else if (z == Z_RND_GO || z == Z_RND_MUTATE) {
        rnd_action(e, z == Z_RND_MUTATE);
    } else if (z == Z_RND_UNDO || z == Z_RND_REDO) {
        rnd_step(e, z == Z_RND_UNDO ? -1 : 1);
    } else if (z >= Z_RND_LOCK && z < Z_RND_LOCK + RS_COUNT) {
        e->rnd_locks ^= 1u << (z - Z_RND_LOCK);
        InvalidateRect(e->wnd, NULL, FALSE);
    } else if (z == Z_SUB_PREV) {
        show(e, e->screen, (e->sub + subs - 1) % subs);
    } else if (z == Z_SUB_NEXT) {
        show(e, e->screen, (e->sub + 1) % subs);
    }
}

static void press(ed_t *e, int x, int y) {
    int i = hit(e, x, y);
    if (seq_zone(i)) {   /* the step editor and the page LEDs (seq_page.c); a drag on a note or a value follows */
        if (seq_press(e, i, x, y, GetKeyState(VK_SHIFT) < 0)) { e->mode = M_SEQ; SetCapture(e->wnd); }
        return;
    }
    switch (i) {
    case -1: return;
    case Z_PREV: patches_step(e, -1); return;   /* factory presets, then user patches */
    case Z_NEXT: patches_step(e, 1); return;
    case Z_NAME: patches_toggle(e); return;
    case Z_OCT_DOWN: set_octave(e, e->oct - 1); return;
    case Z_OCT_UP: set_octave(e, e->oct + 1); return;
    case Z_KEYS: e->mode = M_KEY; break;
    case Z_PITCH: e->mode = M_PITCH; break;
    case Z_MODW: e->mode = M_MODW; break;
    case Z_PAD: e->mode = M_PAD; break;   /* (controller_move sends the touch and the position) */
    case Z_RIBBON: e->mode = M_RIBBON; e->ribbon_cc = -1; break;
    case Z_ENCODER: e->mode = M_ENC; e->enc_y = y; e->enc_moved = 0; break;
    case Z_RND_AMOUNT: e->mode = M_RND_AMOUNT; break;
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
        if (patches_tick(e)) changed = 1;   /* the edited mark, a message that ran out */
        if (seq_tick(e, changed)) changed = 1;   /* the playing step; the sequence while it is edited */
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
        if (e->mode == M_SEQ) { seq_drag(e, x, y, (wp & MK_SHIFT) != 0); return 0; }
        if (e->mode != M_NONE) { controller_move(e, x, y); return 0; }
        int h = hit(e, x, y);
        if (h != e->hover) { e->hover = h; InvalidateRect(w, NULL, FALSE); }
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, w, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_KEYDOWN:   /* the step editor: arrow keys move the selection, Delete deletes */
        if (e && e->screen == SCR_SEQ && seq_key(e, (int)wp, GetKeyState(VK_SHIFT) < 0)) return 0;
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
        int i = hit(e, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (seq_zone(i)) seq_right(e, i);   /* the step editor: delete, a step's rest */
        else if (i >= 0 && i < NPARAMS && PARAMS[i].nopts > 1) pick_option(e, i, e->hit_rect);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (!e) return 0;
        if (e->mode == M_SEQ) {
            seq_release(e);
            e->mode = M_NONE;
        } else if (e->mode == M_PARAM) {
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
        if (seq_zone(i) && i < Z_SEQ_BASE + 200) seq_double(e, i);   /* an empty step cell: a new note */
        else if (i >= 0 && i < NPARAMS && PARAMS[i].nopts == 0) edit_value(e, i, PARAMS[i].def);
        else if (i >= 0) press(e, x, y);   /* a second click on a button or a key counts as a click */
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!e) return 0;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(w, &pt);
        int i = hit(e, pt.x, pt.y), dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (i == Z_ENCODER) { patches_step(e, -dir); return 0; }
        if ((seq_zone(i) || (e->screen == SCR_SEQ && PtInRect(&L.screen, pt) && (i < 0 || i >= NPARAMS)))
            && seq_wheel(e, i, dir, (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0))
            return 0;
        if (i == Z_RND_AMOUNT) {
            float v = e->rnd_amount + dir * 0.01f;
            e->rnd_amount = roundf((v < 0 ? 0 : v > 1 ? 1 : v) * 100.0f) / 100.0f;
            InvalidateRect(w, NULL, FALSE);
            return 0;
        }
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
            if (e->pad_on) { e->pad_on = 0; ui_midi(e, 0xD0, 0, 0); ui_midi(e, 0xB0, PAD_TOUCH_CC, 0); }
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

/* The window class and the fonts, made when the first editor opens and undone when the last instance closes (and,
 * should a host unload the DLL without closing an instance, as the DLL goes): a class left registered would point into
 * the unloaded DLL, and a rebuilt DLL loaded again at the same address would then open its window through it. */
static int class_ready;
static HFONT *const FONTS[] = {&font_title, &font_label, &font_value, &font_tiny, &font_screen, &font_cell, &font_big,
                               &font_brand};
static void free_window_class(void) {
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

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved) {
    (void)inst;
    if (why == DLL_PROCESS_DETACH && !reserved) free_window_class();   /* FreeLibrary (not the process ending) */
    return TRUE;
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
        if (idx == TEST_PATCH && e && ptr) return patches_test(e, v, ptr, opt);   /* load_test: user patches */
        if (idx == TEST_SEQ && e && ptr) return seq_test(e, v, ptr);   /* load_test: the step sequencer's page */
        if (idx == TEST_LEGACY && e && e->hr) {   /* load_test: the old 44.1 kHz int16 path, to compare against */
            hr_set_legacy(e->hr, (int)v);
            fx->initialDelay = hr_latency(e->hr);
            return 1;
        }
        break;
    case effSetSampleRate:   /* the converter follows the host's rate; its latency goes to the host */
        if (e && e->hr) fx->initialDelay = hr_set_rate(e->hr, opt);
        break;
    case effMainsChanged:    /* resume: the converter starts clean */
        if (e && e->hr && v) hr_restart(e->hr);
        break;
    case effProcessEvents:   /* host MIDI waits for the engine block nearest its time (hostrate.c) */
        if (e && e->hr && ptr && hr_events(e->hr, fx, ptr)) return 1;
        break;
    case effSetProgram:   /* the host's or the browser's: the patch shown follows (patches.c) */
        if (e) {
            int before = (int)wrapped_dispatch(fx, effGetProgram, 0, 0, NULL, 0);
            intptr_t r = wrapped_dispatch(fx, op, idx, v, ptr, opt);
            patches_program_set(e, before);
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
        e->screen = SCR_MATRIX;
        e->bend = 0;
        e->pad_x = 0.5f;
        e->ribbon = 0.5f;
        e->ribbon_cc = -1;
        e->rnd_amount = 0.2f;
        e->hr = hr;
        eds[i] = e;
        wrapped_dispatch = fx->dispatcher;
        wrapped_replacing = fx->processReplacing;
        wrapped_process = fx->process;
        fx->dispatcher = dispatcher;
        fx->processReplacing = hooked_replacing;
        fx->process = hooked_process;
        fx->flags |= effFlagsHasEditor;
        patches_init(e);
        seq_init(e);
        break;
    }
    return fx;   /* no free slot: the plugin still works, without its window, at 44.1 kHz in 16 bits (the old path) */
}
