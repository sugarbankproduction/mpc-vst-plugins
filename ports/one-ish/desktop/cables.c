/* The CABLES view on the desktop editor (see editor.c), 2026-10-30: the modulation matrix seen as patch cables.
 *
 *   CABLES (the MATRIX page's title line, a plain click) dims the panel a little and draws every active matrix row of
 *   the Synth in focus (SYNTH 1/2/3 switch it; with TOGETHER the one shown) as a sagging cable, coloured by the
 *   source's family (as quick assign's rings: envelopes amber, LFOs blue, the hands' controllers green, random violet,
 *   the generators pink), from its source to the control it moves. A source with a place on the panel starts there (the
 *   LFOs' and envelopes' title strips, NOISE (its envelope), the wheels, the X/Y pad, the ribbon, GENERATIVE); the
 *   others (velocity, aftertouch, the key, the pedals, constant, random) from a labelled jack in the JACK BAY laid over
 *   the keyboard's top. A destination with no knob of its own (a MORE page's item, Pitch, an LFO's rate in semitones)
 *   ends at a jack there too. A row switched off (its ENABLE) is drawn faint.
 *   Each cable has an amount knob in its middle: drag it up or down (1% a pixel, Shift 0.2%; one undo step a drag; the
 *   Synths edited TOGETHER get the same amount, as quick assign's - / + do); green for a positive amount, red for a
 *   negative one. A click on a cable (or its knob) selects it: a label shows its row, source > destination, amount, VIA
 *   and XFORM, with an X that removes the row (one undo step). A click on a source's jack starts quick assign from it:
 *   then a click on a knob connects it (quickmod.c), and the new cable appears. CABLES again, CABLES OFF at the bay's
 *   left, or Esc hide it. Clicks anywhere else go to the panel as always.
 *   (2026-10-31) Dragging, with every action still a plain click too:
 *   - a press on a source's jack (every panel source has one now, used or not; the bay's) and a drag: a live cable
 *     follows the mouse, the knob (or destination jack: Pitch always has one) under it lights; let go there: a new row
 *     at +25% (in every Synth edited together, as quick assign), the cable selected; anywhere else: nothing. A press and
 *     let go without moving: quick assign from it, as before. The matrix full: the help line says so, the ring is red.
 *   - the dots near each cable's ends are grab handles: drag the destination's to another knob or destination jack,
 *     the source's to another source's jack: the row re-patched; let go on empty panel (or the bay): the row removed.
 *   - a selected cable's box: ON / OFF (its ENABLE), X, AMOUNT, VIA (its list) and VIA AMOUNT, XFORM (its list) and its
 *     amount: each value dragged or < > (5%). Every edit and every drag is one undo step, told to the host as a knob,
 *     in the MATRIX page's own params (the page stays the only truth).
 *   The live cable is drawn over the cached layer and repaints only its own area as it moves; a cable whose end is
 *   dragged is left out of the layer (drawn again once at the start and once at the end of the drag).
 *
 * Drawing: the dimming veil, the bay, the jacks and the cables are drawn into a cached layer (premultiplied ARGB, the
 * window's size, anti-aliased with GDI+) only when the rows, the Synth in focus or the window's size change; each paint
 * blends the part it repaints (skin_layer_blend) and draws the knobs, the labels and the selected cable on top. A knob
 * dragged repaints only itself, its label and the help line. UI thread only; the audio thread sees only setParameter,
 * as from a host. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "library.h"
#pragma GCC diagnostic push   /* (only some of gfx.h's shapes) */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "gfx.h"
#pragma GCC diagnostic pop
#include "skin.h"
#include "../src/modmatrix.h"

_Static_assert(NMATRIX <= CB_MAX, "a cable per matrix row");
int WINAPI GdipCreateBitmapFromScan0(int w, int h, int stride, int format, BYTE *scan0, void **bitmap);
int WINAPI GdipGetImageGraphicsContext(void *image, void **g);
int WINAPI GdipDisposeImage(void *image);
#define PIXFMT_PARGB 0x000E200B   /* PixelFormat32bppPARGB */
#define Z_CB_BAY (Z_CB + 3)       /* the bay's background: eats the click (the keyboard is under it) */

/* the sources with no place on the panel: jacks in the bay, in this order */
static const signed char BAY[] = {MS_VEL, MS_AT, MS_KEY, MS_GATE, MS_SUSTAIN, MS_SOSTENUTO, MS_EXP1, MS_EXP2, MS_CONST,
                                  MS_RAND, MS_RAND_UNI};
enum { NBAY = sizeof BAY / sizeof BAY[0] };
/* (2026-10-31) the sources with a place on the panel: a jack there always (a cable can be dragged from it) */
static const signed char PANEL_SRC[] = {MS_LFO1, MS_LFO2, MS_LFO3, MS_LFO4, MS_FENV, MS_AENV, MS_MENV, MS_NOISE_EG,
                                        MS_BEND, MS_WHEEL, MS_PAD_X, MS_PAD_Y, MS_PAD_GATE, MS_RIBBON};
/* (2026-10-31) dragging (e->cb.drag): a value (the amount knob, the box's), a press on a source's jack not moved yet, a
 * new cable from it, a cable's destination end, its source end */
enum { CD_NONE, CD_AMT, CD_PRESS, CD_NEW, CD_DST, CD_SRC };
/* what letting go would do (e->cb.act): nothing, connect, nothing (the matrix full), move the end, nothing (the same
 * end), remove the row, nothing (not a destination / not a source there) */
enum { CA_NONE, CA_CONNECT, CA_FULL, CA_MOVE, CA_SAME, CA_REMOVE, CA_NOT_DST, CA_NOT_SRC };
enum { DRAG_PX = 4, NEW_AMT = 25, BOX_STEP = 5, MSG_MS = 6000 };
#define COL_ON (-2)   /* a "column" for the row's ENABLE (MATRIX_ON) */

/* ---- the matrix, Synth c's own params ---- */
static int cell(int m, int col, int c) { return MATRIX[m][col] >= 0 ? synth_param(MATRIX[m][col], c) : -1; }
static int opt_at(ed_t *e, int i) {
    return i >= 0 && PARAMS[i].nopts > 1 ? (int)lroundf(e->fx->getParameter(e->fx, i) * (PARAMS[i].nopts - 1)) : 0;
}
static float real_at(ed_t *e, int i) { return PARAMS[i].min + e->fx->getParameter(e->fx, i) * (PARAMS[i].max - PARAMS[i].min); }
static float amt_norm(int i, float pct) {
    float v = (pct - PARAMS[i].min) / (PARAMS[i].max - PARAMS[i].min);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
static int row_on(ed_t *e, int m, int c) { return MATRIX_ON[m] < 0 || opt_at(e, synth_param(MATRIX_ON[m], c)) > 0; }
static int row_src(ed_t *e, int m, int c) { return opt_at(e, cell(m, MX_SRC, c)); }
static int row_dst(ed_t *e, int m, int c) { return opt_at(e, cell(m, MX_DST, c)); }

/* a destination's control on the panel (Synth 1's index), or -1 */
static int ctl_of_dst(int d) {
    static short T[1024];
    static int ready;
    if (!ready) {
        for (int k = 0; k < 1024; k++) T[k] = -1;
        for (int c = NCTLS - 1; c >= 0; c--) {   /* (the first control on the panel wins) */
            int x = qm_dst_of(CTLS[c].param);
            if (x > 0 && x < 1024) T[x] = (short)CTLS[c].param;
        }
        ready = 1;
    }
    return d > 0 && d < 1024 ? T[d] : -1;
}
static int panel_rect(const char *title, RECT *r) {
    for (int p = 0; p < NPANELS; p++)
        if (PANELS[p].style == 1 && !strcmp(PANELS[p].title, title)) {
            SetRect(r, sc(PANELS[p].x), sc(PANELS[p].y), sc(PANELS[p].x + PANELS[p].w), sc(PANELS[p].y + PANELS[p].h));
            return 1;
        }
    return 0;
}

/* ---- where things are ---- */
static RECT bay_plate(ed_t *e) {   /* the jack bay over the keyboard's top, as long as its jacks need */
    float right = L.keys.left + sc(110) + (NBAY - 1) * sc(72) + sc(40);
    for (int j = 0; j < e->cb.nj; j++)   /* (the destinations' jacks after the sources') */
        if (e->cb.jack[j].y > L.keys.top && e->cb.jack[j].x + sc(40) > right) right = e->cb.jack[j].x + sc(40);
    RECT r = {L.keys.left + sc(8), L.keys.top + sc(6), (int)right, L.keys.top + sc(44)};
    return r;
}
static RECT off_rect(void) {
    RECT r = {L.keys.left + sc(14), L.keys.top + sc(15), L.keys.left + sc(86), L.keys.top + sc(35)};
    return r;
}
RECT cb_button_rect(void) {
    RECT r = {L.sub_prev.left - sc(62), L.tabline.top + sc(2), L.sub_prev.left - sc(8), L.tabline.bottom - sc(2)};
    return r;
}
static float bay_x(int k) { return L.keys.left + sc(110) + k * sc(72); }
static float bay_y(void) { return L.keys.top + sc(19); }
/* a source's place on the panel (1), or 0: its jack in the bay. gen: how many generator jacks are placed already */
static int spot(int s, int gen, float *x, float *y) {
    static const struct { int s; const char *panel; } P[] = {
        {MS_LFO1, "LFO 1"}, {MS_LFO2, "LFO 2"}, {MS_LFO3, "LFO 3"}, {MS_LFO4, "LFO 4"}, {MS_FENV, "FILTER ENVELOPE"},
        {MS_AENV, "AMPLIFIER ENVELOPE"}, {MS_MENV, "MODULATION ENVELOPE"}, {MS_NOISE_EG, "NOISE"}};
    RECT r;
    for (unsigned k = 0; k < sizeof P / sizeof P[0]; k++)
        if (P[k].s == s && panel_rect(P[k].panel, &r)) {   /* on the title strip, right of its name */
            *x = r.right - sc(26);
            *y = r.top + sc(7);
            return 1;
        }
    if (s == MS_BEND || s == MS_WHEEL) {
        const RECT *w = s == MS_BEND ? &L.pitch : &L.modw;
        *x = (w->left + w->right) / 2.0f;
        *y = w->top - sc(7);
        return !IsRectEmpty(w);
    }
    if (s == MS_PAD_X || s == MS_PAD_Y || s == MS_PAD_GATE) {
        float f = s == MS_PAD_X ? 0.2f : s == MS_PAD_Y ? 0.47f : 0.74f;
        *x = L.pad.left + rw(&L.pad) * f;
        *y = L.pad.top + sc(10);
        return !IsRectEmpty(&L.pad);
    }
    if (s == MS_RIBBON) {
        *x = L.ribbon.left + sc(70);
        *y = (L.ribbon.top + L.ribbon.bottom) / 2.0f;
        return !IsRectEmpty(&L.ribbon);
    }
    if (qm_is_gen_src(s) && panel_rect("GENERATIVE", &r)) {   /* down GENERATIVE's right edge, named to the left */
        *x = r.right - sc(10);
        *y = r.top + sc(26) + gen * sc(17);
        if (*y > r.bottom - sc(8)) *y = r.bottom - sc(8);
        return 1;
    }
    return 0;
}
static int add_jack(ed_t *e, int src, int dst, float x, float y, const char *label) {
    for (int j = 0; j < e->cb.nj; j++)
        if (e->cb.jack[j].src == src && e->cb.jack[j].dst == dst) return j;
    if (e->cb.nj >= CB_JACKS) return -1;
    cb_jack_t *k = &e->cb.jack[e->cb.nj];
    k->src = (short)src;
    k->dst = (short)dst;
    k->x = x;
    k->y = y;
    snprintf(k->label, sizeof k->label, "%s", label);
    return e->cb.nj++;
}
static int jack_of_src(ed_t *e, int s) {
    for (int j = 0; j < e->cb.nj; j++)
        if (e->cb.jack[j].src == s && !e->cb.jack[j].dst) return j;
    return -1;
}

/* the cable: a cubic curve that hangs between its ends, its knob at t */
static void bez(const cb_cable_t *c, float sag, float t, float *x, float *y) {
    float dx = c->x1 - c->x0, ax = c->x0 + dx * 0.25f, ay = c->y0 + sag, bx = c->x0 + dx * 0.75f, by = c->y1 + sag, u = 1 - t;
    *x = u * u * u * c->x0 + 3 * u * u * t * ax + 3 * u * t * t * bx + t * t * t * c->x1;
    *y = u * u * u * c->y0 + 3 * u * u * t * ay + 3 * u * t * t * by + t * t * t * c->y1;
}
/* (2026-10-31) the point on the cable `dist` px along it from one end (0 source, 1 destination) */
static void along(const cb_cable_t *c, int end, float dist, float *x, float *y) {
    float ex = end ? c->x1 : c->x0, ey = end ? c->y1 : c->y0;
    for (int q = 1; q < CB_PTS; q++) {
        int p = end ? CB_PTS - 1 - q : q, o = end ? p + 1 : p - 1;
        float d = hypotf(c->pt[p][0] - ex, c->pt[p][1] - ey);
        if (d >= dist) {
            float od = hypotf(c->pt[o][0] - ex, c->pt[o][1] - ey), f = d > od ? (dist - od) / (d - od) : 0;
            *x = c->pt[o][0] + (c->pt[p][0] - c->pt[o][0]) * f;
            *y = c->pt[o][1] + (c->pt[p][1] - c->pt[o][1]) * f;
            return;
        }
    }
    *x = c->pt[CB_PTS / 2][0];
    *y = c->pt[CB_PTS / 2][1];
}
static void handles(cb_cable_t *c) {   /* the grab handles: clear of the jack (its own drag) and of the ring */
    along(c, 0, 15.0f * L.s, &c->hx[0], &c->hy[0]);
    along(c, 1, c->rad + 8.0f * L.s, &c->hx[1], &c->hy[1]);
}
static void curve(ed_t *e, int k) {
    cb_cable_t *c = &e->cb.cab[k];
    float dist = hypotf(c->x1 - c->x0, c->y1 - c->y0), low = c->y0 > c->y1 ? c->y0 : c->y1, limit = L.h - sc(16);
    static const float TRY[][2] = {{0, 0.5f}, {20, 0.5f}, {0, 0.4f}, {0, 0.6f}, {40, 0.5f}, {20, 0.4f}, {20, 0.6f},
                                   {0, 0.32f}, {0, 0.68f}, {60, 0.5f}, {40, 0.36f}, {40, 0.64f}, {0, 0.25f},
                                   {0, 0.75f}, {30, 0.25f}, {30, 0.75f}, {0, 0.18f}, {0, 0.82f}};
    enum { NTRY = sizeof TRY / sizeof TRY[0] };
    RECT scr = L.screen;
    InflateRect(&scr, sc(12), sc(12));
    int pick = -1, off_screen = -1;   /* the first place clear of everything, else the first off the centre screen */
    float sags[NTRY];
    for (int j = 0; j < NTRY && pick < 0; j++) {
        float sag = sc(36) + 0.2f * dist + TRY[j][0] * L.s, x, y;
        if (low + 0.75f * sag > limit) sag = (limit - low) / 0.75f;
        sags[j] = sag < 0 ? 0 : sag;
        bez(c, sags[j], TRY[j][1], &x, &y);
        POINT kp = {(int)x, (int)y};
        if (PtInRect(&scr, kp)) continue;   /* (its knob would hide the matrix or the help line) */
        if (off_screen < 0) off_screen = j;
        int clash = 0;   /* (too near another knob, or an end) */
        for (int o = 0; o < k && !clash; o++) clash = hypotf(e->cb.cab[o].kx - x, e->cb.cab[o].ky - y) < sc(24);
        for (int o = 0; o < e->cb.n && !clash; o++)
            clash = hypotf(e->cb.cab[o].x1 - x, e->cb.cab[o].y1 - y) < e->cb.cab[o].rad + sc(10) ||
                    hypotf(e->cb.cab[o].x0 - x, e->cb.cab[o].y0 - y) < sc(14);
        if (!clash) pick = j;
    }
    if (pick < 0) pick = off_screen >= 0 ? off_screen : 0;
    bez(c, sags[pick], TRY[pick][1], &c->kx, &c->ky);
    for (int p = 0; p < CB_PTS; p++) bez(c, sags[pick], p / (float)(CB_PTS - 1), &c->pt[p][0], &c->pt[p][1]);
    handles(c);
}

/* the cables and jacks for the rows now (the Synth in focus) */
static void geom(ed_t *e) {
    cb_ui_t *b = &e->cb;
    b->n = b->nj = 0;
    for (int k = 0; k < NBAY; k++) add_jack(e, BAY[k], 0, bay_x(k), bay_y(), qm_src_name(BAY[k]));
    for (unsigned k = 0; k < sizeof PANEL_SRC; k++) {   /* (2026-10-31) every panel source's jack, used or not */
        float x, y;
        if (spot(PANEL_SRC[k], 0, &x, &y)) add_jack(e, PANEL_SRC[k], 0, x, y, qm_src_name(PANEL_SRC[k]));
    }
    add_jack(e, 0, MD_PITCH, bay_x(NBAY) + sc(24), bay_y(), qm_dst_name(MD_PITCH));   /* (2026-10-31) Pitch always */
    int c = e->focus, gens = 0, dests = 1;
    for (int m = 0; m < NMATRIX; m++) {
        int s = row_src(e, m, c), d = row_dst(e, m, c);
        if (s <= 0 || d <= 0) continue;
        cb_cable_t *k = &b->cab[b->n];
        memset(k, 0, sizeof *k);
        k->row = (short)m;
        k->src = (short)s;
        k->dst = (short)d;
        k->on = (short)row_on(e, m, c);
        k->amt = real_at(e, cell(m, MX_AMT, c));
        int j = jack_of_src(e, s);
        if (j < 0) {   /* a place on the panel: a jack there */
            float x, y;
            if (!spot(s, gens, &x, &y)) continue;
            if (qm_is_gen_src(s)) gens++;
            j = add_jack(e, s, 0, x, y, qm_src_name(s));
        }
        if (j < 0) continue;
        k->x0 = b->jack[j].x;
        k->y0 = b->jack[j].y;
        k->ctl = (short)ctl_of_dst(d);
        if (k->ctl >= 0) {
            int i = k->ctl, kind = L.kind[i];
            if (kind == K_KNOB || kind == K_SMALL || kind == K_BIG) {
                float cx, cy, r;
                knob_geom(i, &cx, &cy, &r, NULL);
                k->x1 = cx;
                k->y1 = cy;
                k->rad = r + 2.5f * L.s;
            } else {
                k->x1 = (L.ctl[i].left + L.ctl[i].right) / 2.0f;
                k->y1 = (L.ctl[i].top + L.ctl[i].bottom) / 2.0f;
                k->rad = 7.0f * L.s;
            }
        } else {   /* no knob of its own: a jack in the bay, after the sources */
            int before = b->nj;
            int dj = add_jack(e, 0, d, bay_x(NBAY) + sc(24) + dests * sc(72), bay_y(), qm_dst_name(d));
            if (dj < 0) continue;
            if (b->nj > before) dests++;
            k->x1 = b->jack[dj].x;
            k->y1 = b->jack[dj].y;
            k->rad = 5.5f * L.s;
        }
        b->n++;
    }
    for (int k = 0; k < b->n; k++) curve(e, k);
}

static unsigned signature(ed_t *e) {   /* what the cables' look depends on (not the amounts, not the selection) */
    unsigned h = 2166136261u;
#define MIX(v) (h = (h ^ (unsigned)(v)) * 16777619u)
    MIX(e->focus);
    MIX(L.w);
    MIX(L.h);
    MIX(e->cb.hide);   /* (a cable whose end is dragged: left out of the layer) */
    for (int m = 0; m < NMATRIX; m++) {
        MIX(row_src(e, m, e->focus));
        MIX(row_dst(e, m, e->focus));
        MIX(row_on(e, m, e->focus));
    }
#undef MIX
    return h | 1u;
}
static int cable_of_row(ed_t *e, int row) {
    for (int k = 0; k < e->cb.n; k++)
        if (e->cb.cab[k].row == row) return k;
    return -1;
}
static void refresh(ed_t *e) {   /* the rows changed: the cables again, and all of the window repaints */
    unsigned s = signature(e);
    if (s == e->cb.sig) return;
    e->cb.sig = s;
    geom(e);
    if (e->cb.sel >= 0 && cable_of_row(e, e->cb.sel) < 0) e->cb.sel = -1;
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
}

/* ---- the cached layer ---- */
static DWORD fade(DWORD c, unsigned a) { return (c & 0x00FFFFFFu) | a << 24; }
static int trimmed(const cb_cable_t *c, gpt_t *out) {   /* its points, up to the ring round its destination */
    int n = 0;
    for (int p = 0; p < CB_PTS; p++) {
        float d = hypotf(c->pt[p][0] - c->x1, c->pt[p][1] - c->y1);
        if (d < c->rad && p > 0) {   /* the point where it meets the ring */
            float pd = hypotf(c->pt[p - 1][0] - c->x1, c->pt[p - 1][1] - c->y1), f = pd > d ? (pd - c->rad) / (pd - d) : 0;
            gpt_t q = {c->pt[p - 1][0] + (c->pt[p][0] - c->pt[p - 1][0]) * f, c->pt[p - 1][1] + (c->pt[p][1] - c->pt[p - 1][1]) * f};
            out[n++] = q;
            break;
        }
        gpt_t q = {c->pt[p][0], c->pt[p][1]};
        out[n++] = q;
    }
    return n;
}
static void draw_cable(const cb_cable_t *c, int selected) {
    gpt_t p[CB_PTS + 1];
    int n = trimmed(c, p);
    float s = L.s;
    DWORD col = qm_family(c->src);
    if (!c->on) col = fade(col, 0x78);
    if (selected) gfx_lines(p, n, 10.0f * s, 0x60FFFFFF);
    if (n >= 2) {
        gfx_lines(p, n, (c->on ? 6.4f : 4.4f) * s, c->on ? 0xE6101012 : 0x60101012);
        gfx_lines(p, n, (c->on ? 4.0f : 2.4f) * s, col);
        for (int k = 0; k < n; k++) { p[k].x -= 0.5f * s; p[k].y -= 0.8f * s; }
        if (c->on) gfx_lines(p, n, 0.9f * s, 0x55FFFFFF);   /* the rubber's sheen */
    }
    gfx_circle(c->x1, c->y1, c->rad, 3.4f * s, c->on ? 0xE0101012 : 0x60101012);   /* the plug round the control */
    gfx_circle(c->x1, c->y1, c->rad, 1.9f * s, col);
    gfx_disc(c->x0, c->y0, 4.4f * s, 0xFF101012);   /* the plug in its jack */
    gfx_disc(c->x0, c->y0, 3.1f * s, col);
}
static void draw_jack(const cb_jack_t *j) {
    float s = L.s;
    gfx_disc(j->x, j->y, 5.8f * s, 0xFF1A1A1D);
    gfx_circle(j->x, j->y, 5.2f * s, 1.5f * s, j->dst ? 0xFF8A8A92 : 0xFFC4C6CC);
    gfx_disc(j->x, j->y, 2.6f * s, 0xFF000000);
}
static void build(ed_t *e) {
    cb_ui_t *b = &e->cb;
    if (!b->layer || b->lw != L.w || b->lh != L.h) {
        free(b->layer);
        b->layer = malloc((size_t)L.w * L.h * 4);
        b->lw = L.w;
        b->lh = L.h;
        if (!b->layer) { b->lw = b->lh = 0; return; }
    }
    memset(b->layer, 0, (size_t)L.w * L.h * 4);   /* (the panel is dimmed as it is blended: skin_layer_blend) */
    void *bmp = NULL, *g = NULL;
    if (gfx_startup() && !GdipCreateBitmapFromScan0(L.w, L.h, L.w * 4, PIXFMT_PARGB, (BYTE *)b->layer, &bmp) && bmp &&
        !GdipGetImageGraphicsContext(bmp, &g) && g) {
        gfx_g = g;
        GdipSetSmoothingMode(g, 4);
        RECT bp = bay_plate(e);   /* the bay: a dark strip with the jacks, their names under them */
        gfx_rect((float)bp.left, (float)bp.top, (float)rw(&bp), (float)rh(&bp), 0xD8101113);
        gfx_frame((float)bp.left, (float)bp.top, (float)rw(&bp), (float)rh(&bp), 1.2f * L.s, 0xFF4A4A52);
        for (int j = 0; j < b->nj; j++) {
            const cb_jack_t *k = &b->jack[j];
            if (k->y < L.keys.top) {   /* a panel jack's name plate (the pad's, the generators') */
                if (k->src == MS_PAD_X || k->src == MS_PAD_Y || k->src == MS_PAD_GATE)
                    gfx_rect(k->x + 6 * L.s, k->y - 6 * L.s, (k->src == MS_PAD_GATE ? 30 : 12) * L.s, 12 * L.s, 0xD0101113);
                else if (qm_is_gen_src(k->src))
                    gfx_rect(k->x - 96 * L.s, k->y - 6 * L.s, 88 * L.s, 12 * L.s, 0xD0101113);
            }
            draw_jack(k);
        }
        for (int k = 0; k < b->n; k++)   /* (the faint ones under; one whose end is dragged: not) */
            if (!b->cab[k].on && b->cab[k].row + 1 != b->hide) draw_cable(&b->cab[k], 0);
        for (int k = 0; k < b->n; k++)
            if (b->cab[k].on && b->cab[k].row + 1 != b->hide) draw_cable(&b->cab[k], 0);
        GdipDeleteGraphics(g);
        gfx_g = NULL;
    }
    if (bmp) GdipDisposeImage(bmp);
    RECT front[2] = {L.tabline, L.help};   /* the screen's title line (CABLES) and help line in front of the cables */
    for (int r = 0; r < 2; r++)
        for (int y = front[r].top < 0 ? 0 : front[r].top; y < front[r].bottom && y < b->lh; y++)
            memset(&b->layer[(size_t)y * b->lw + front[r].left], 0, (size_t)rw(&front[r]) * 4);
    b->layer_sig = b->sig;
    b->builds++;
}

/* ---- the selected cable's label, and its X ---- */
static int text_w(const char *s, HFONT f) {
    static HDC m;
    if (!m) m = CreateCompatibleDC(NULL);
    SIZE z = {0, 0};
    if (m) {
        HGDIOBJ o = SelectObject(m, f);
        GetTextExtentPoint32A(m, s, (int)strlen(s), &z);
        SelectObject(m, o);
    }
    return z.cx;
}
static void label_text(ed_t *e, const cb_cable_t *c, char *out, int n) {
    int f = e->focus, m = c->row, v = MATRIX[m][MX_VIA] >= 0 ? opt_at(e, cell(m, MX_VIA, f)) : 0;
    int x = MATRIX[m][MX_XF] >= 0 ? opt_at(e, cell(m, MX_XF, f)) : 0;
    int k = snprintf(out, n, "%d  %s > %s  %+.0f%%", m + 1, qm_src_name(c->src), qm_dst_name(c->dst),
                     real_at(e, cell(m, MX_AMT, f)));
    if (v > 0 && k < n)
        k += snprintf(out + k, n - k, "   VIA %s %.0f%%", PARAMS[cell(m, MX_VIA, f)].opts[v],
                      MATRIX[m][MX_VIA_AMT] >= 0 ? real_at(e, cell(m, MX_VIA_AMT, f)) : 100.0f);
    if (x > 0 && k < n)
        k += snprintf(out + k, n - k, "   XFORM %s %.0f%%", PARAMS[cell(m, MX_XF, f)].opts[x],
                      MATRIX[m][MX_XF_AMT] >= 0 ? real_at(e, cell(m, MX_XF_AMT, f)) : 100.0f);
    if (!c->on && k < n) snprintf(out + k, n - k, "   (row off)");
}
/* (2026-10-31) the selected cable's settings box, by its knob: the row's name with ON / OFF and X on its top line, then
 * AMOUNT, VIA and XFORM, each with < value > (drag the value, or click < >), VIA and XFORM with their list */
typedef struct { RECT box, title, x, it[CBX_COUNT]; int row; } box_t;
static void title_text(ed_t *e, const cb_cable_t *c, char *out, int n) {
    (void)e;
    snprintf(out, n, "%d  %s > %s%s", c->row + 1, qm_src_name(c->src), qm_dst_name(c->dst), c->on ? "" : "  (row off)");
}
static int box_geom(ed_t *e, box_t *b) {   /* 0: nothing selected */
    int k = e->cb.sel >= 0 ? cable_of_row(e, e->cb.sel) : -1;
    if (k < 0) return 0;
    const cb_cable_t *c = &e->cb.cab[k];
    char t[160];
    title_text(e, c, t, sizeof t);
    int lab = sc(46), dd = sc(108), btn = sc(14), val = sc(40), gap = sc(3), pad = sc(6), lh = sc(18), rh_ = sc(17);
    int body = pad + lab + dd + gap + btn + gap + val + gap + btn + pad;
    int head = pad + text_w(t, font_cell) + sc(8) + sc(34) + sc(4) + sc(17) + sc(3);
    int w = body > head ? body : head, h = lh + 3 * rh_ + sc(5);
    /* beside its knob, right or left, level with it or below or above: the first place clear of the centre screen (its
     * help line explains the box's controls), else the first that fits */
    int left = 0, top = 0, found = 0;
    RECT scr = L.screen;
    InflateRect(&scr, sc(4), sc(4));
    for (int pass = 0; pass < 2 && !found; pass++)
        for (int p = 0; p < 6 && !found; p++) {
            int l = p % 2 ? (int)(c->kx - sc(16)) - w : (int)(c->kx + sc(16));
            int t_ = p < 2 ? (int)(c->ky - lh / 2) : p < 4 ? (int)(c->ky + sc(14)) : (int)(c->ky - sc(14)) - h;
            if (l < sc(4) || l + w > L.w - sc(4)) continue;
            if (t_ < sc(2)) t_ = sc(2);
            if (t_ + h > L.h - sc(2)) t_ = L.h - sc(2) - h;
            RECT r = {l, t_, l + w, t_ + h}, o;
            if (pass == 0 && IntersectRect(&o, &r, &scr)) continue;
            left = l;
            top = t_;
            found = 1;
        }
    if (!found) { left = sc(4); top = sc(2); }
    memset(b, 0, sizeof *b);
    b->row = c->row;
    SetRect(&b->box, left, top, left + w, top + h);
    SetRect(&b->x, left + w - sc(19), top + sc(2), left + w - sc(2), top + lh - sc(2));
    SetRect(&b->it[CBX_ON], b->x.left - sc(4) - sc(34), top + sc(2), b->x.left - sc(4), top + lh - sc(2));
    SetRect(&b->title, left + pad, top, b->it[CBX_ON].left - sc(4), top + lh);
    b->it[CBX_BG] = b->box;
    for (int r = 0; r < 3; r++) {   /* AMOUNT, VIA, XFORM */
        int y0 = top + lh + r * rh_ + sc(1), y1 = y0 + rh_ - sc(3), x = left + pad + lab;
        static const int LIST[3] = {-1, CBX_VIA, CBX_XF}, DN[3] = {CBX_AMT_DN, CBX_VIA_DN, CBX_XF_DN};
        if (LIST[r] >= 0) SetRect(&b->it[LIST[r]], x, y0, x + dd, y1);
        x += dd + gap;
        SetRect(&b->it[DN[r]], x, y0, x + btn, y1);
        SetRect(&b->it[DN[r] + 1], x + btn + gap, y0, x + btn + gap + val, y1);
        SetRect(&b->it[DN[r] + 2], x + btn + gap + val + gap, y0, x + 2 * btn + 2 * gap + val, y1);
    }
    int m = c->row;   /* (a column this matrix lacks: no control for it) */
    if (MATRIX_ON[m] < 0) SetRectEmpty(&b->it[CBX_ON]);
    if (MATRIX[m][MX_VIA] < 0) SetRectEmpty(&b->it[CBX_VIA]);
    if (MATRIX[m][MX_VIA_AMT] < 0) for (int i = CBX_VIA_DN; i <= CBX_VIA_UP; i++) SetRectEmpty(&b->it[i]);
    if (MATRIX[m][MX_XF] < 0) SetRectEmpty(&b->it[CBX_XF]);
    if (MATRIX[m][MX_XF_AMT] < 0) for (int i = CBX_XF_DN; i <= CBX_XF_UP; i++) SetRectEmpty(&b->it[i]);
    return 1;
}
static int label_rects(ed_t *e, RECT *lab, RECT *x) {   /* the box and its X (0: nothing selected) */
    box_t b;
    if (!box_geom(e, &b)) return 0;
    *lab = b.box;
    *x = b.x;
    return 1;
}
static int box_item_at(ed_t *e, POINT pt) {   /* the box's control under pt (CBX_*), or -1 */
    box_t b;
    if (!box_geom(e, &b) || !PtInRect(&b.box, pt)) return -1;
    for (int i = CBX_COUNT - 1; i > CBX_BG; i--)
        if (PtInRect(&b.it[i], pt)) return i;
    return CBX_BG;
}

/* ---- repaints ---- */
static void inval(ed_t *e, RECT r) { if (e->wnd) InvalidateRect(e->wnd, &r, FALSE); }
static void inval_knob(ed_t *e, int k) {
    RECT r = {(int)(e->cb.cab[k].kx - sc(14)), (int)(e->cb.cab[k].ky - sc(14)), (int)(e->cb.cab[k].kx + sc(14)),
              (int)(e->cb.cab[k].ky + sc(14))};
    inval(e, r);
}
static void inval_cable(ed_t *e, int k) {   /* the whole cable (its highlight), its knob */
    if (k < 0 || k >= e->cb.n) return;
    const cb_cable_t *c = &e->cb.cab[k];
    float x0 = c->x0, y0 = c->y0, x1 = c->x0, y1 = c->y0;
    for (int p = 0; p < CB_PTS; p++) {
        x0 = fminf(x0, c->pt[p][0]); x1 = fmaxf(x1, c->pt[p][0]);
        y0 = fminf(y0, c->pt[p][1]); y1 = fmaxf(y1, c->pt[p][1]);
    }
    RECT r = {(int)(x0 - c->rad - sc(8)), (int)(y0 - c->rad - sc(8)), (int)(x1 + c->rad + sc(8)), (int)(y1 + c->rad + sc(8))};
    inval(e, r);
    inval_knob(e, k);
}
static void inval_label(ed_t *e) {
    RECT lab, x, h = L.help;
    inval(e, e->cb.label_at);
    if (label_rects(e, &lab, &x)) { InflateRect(&lab, sc(2), sc(2)); inval(e, lab); e->cb.label_at = lab; }
    InflateRect(&h, sc(2), sc(6));
    if (e->mode != M_CABLE) inval(e, h);   /* (while a knob is dragged its label shows the value; the help line follows
                                            * at the window's timer, as for a knob turned on the panel) */
}
static void select_row(ed_t *e, int row) {
    if (row == e->cb.sel) return;
    inval_cable(e, cable_of_row(e, e->cb.sel));
    inval_label(e);
    e->cb.sel = row;
    inval_cable(e, cable_of_row(e, row));
    inval_label(e);
}

/* ---- the live parts, over the layer ---- */
static void ltext(const RECT *r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    skin_text(r, s, f, c, fmt | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}
static int seen(float x0, float y0, float x1, float y1, const RECT *clip) {
    RECT a = {(int)x0 - 2, (int)y0 - 2, (int)x1 + 2, (int)y1 + 2}, o;
    return IntersectRect(&o, &a, clip);
}
static void amount_knob(const cb_cable_t *c, float amt, int hot) {
    float s = L.s, r = 8.5f * s, x = c->kx, y = c->ky;
    float sweep = amt / 100.0f * 135.0f;
    sweep = sweep < -135 ? -135 : sweep > 135 ? 135 : sweep;
    DWORD face = amt > 0.05f ? 0xFF1C3A22 : amt < -0.05f ? 0xFF41191A : 0xFF2A2A2E;
    DWORD arc = amt >= 0 ? 0xFF7FE38A : 0xFFFF7468;
    gfx_disc(x, y, r + 2.0f * s, 0xF0060607);
    gfx_disc(x, y, r, c->on ? face : 0xFF1E1E20);
    gfx_circle(x, y, r + 0.6f * s, 1.3f * s, hot ? 0xFFFFFFFF : qm_family(c->src));
    gfx_arc(x, y, r - 2.8f * s, 0, sweep, 2.4f * s, c->on ? arc : fade(arc, 0x80));
    gpt_t p = gfx_polar(x, y, r - 2.4f * s, sweep);
    gfx_line(x, y, p.x, p.y, 1.5f * s, 0xFFF2F2F4);
    gpt_t m = gfx_polar(x, y, r - 2.8f * s, 0);   /* the knob's 0 */
    gfx_disc(m.x, m.y, 0.9f * s, 0xA0FFFFFF);
}
/* ---- (2026-10-31) a cable being dragged: drawn over the layer, only its own area repainted as it moves ---- */
static int live_cable(ed_t *e, cb_cable_t *t) {   /* the dragged cable now (0: none) */
    cb_ui_t *b = &e->cb;
    if (b->drag != CD_NEW && b->drag != CD_DST && b->drag != CD_SRC) return 0;
    memset(t, 0, sizeof *t);
    t->on = 1;
    t->rad = 4.0f * L.s;
    if (b->drag == CD_NEW) {
        int j = jack_of_src(e, b->dsrc);
        if (j < 0) return 0;
        t->src = (short)b->dsrc;
        t->x0 = e->cb.jack[j].x;
        t->y0 = e->cb.jack[j].y;
        t->x1 = (float)b->mx;
        t->y1 = (float)b->my;
    } else {
        int k = cable_of_row(e, b->drow);
        if (k < 0) return 0;
        *t = b->cab[k];
        if (b->drag == CD_DST) { t->x1 = (float)b->mx; t->y1 = (float)b->my; t->rad = 4.0f * L.s; }
        else { t->x0 = (float)b->mx; t->y0 = (float)b->my; }
    }
    float dist = hypotf(t->x1 - t->x0, t->y1 - t->y0), low = t->y0 > t->y1 ? t->y0 : t->y1, sag = sc(26) + 0.15f * dist;
    if (low + 0.75f * sag > L.h - sc(6)) sag = (L.h - sc(6) - low) / 0.75f;
    if (sag < 0) sag = 0;
    for (int p = 0; p < CB_PTS; p++) bez(t, sag, p / (float)(CB_PTS - 1), &t->pt[p][0], &t->pt[p][1]);
    return 1;
}
static RECT live_rect(ed_t *e) {   /* its area (empty: none) */
    cb_cable_t t;
    RECT r = {0, 0, 0, 0};
    if (!live_cable(e, &t)) return r;
    float x0 = t.x0, y0 = t.y0, x1 = t.x0, y1 = t.y0;
    for (int p = 0; p < CB_PTS; p++) {
        x0 = fminf(x0, t.pt[p][0]); x1 = fmaxf(x1, t.pt[p][0]);
        y0 = fminf(y0, t.pt[p][1]); y1 = fmaxf(y1, t.pt[p][1]);
    }
    float m = t.rad + sc(9);
    if (e->cb.act == CA_REMOVE) { x1 += sc(20); y0 -= sc(20); }   /* (the X by the mouse) */
    SetRect(&r, (int)(x0 - m), (int)(y0 - m), (int)(x1 + m) + 1, (int)(y1 + m) + 1);
    return r;
}
static void live_draw(ed_t *e, const RECT *clip) {
    cb_cable_t t;
    if (!live_cable(e, &t)) return;
    float s = L.s;
    if (!IsRectEmpty(&e->cb.ring)) {   /* the target under the mouse lit: white (connect, move), red (the matrix full) */
        RECT g = e->cb.ring;
        DWORD c = e->cb.act == CA_FULL ? 0xFFFF6A5A : 0xFFFFFFFF;
        float cx = (g.left + g.right) / 2.0f, cy = (g.top + g.bottom) / 2.0f, rr = rw(&g) / 2.0f;
        if (e->cb.ring_round) {
            gfx_circle(cx, cy, rr, 5.0f * s, (c & 0x00FFFFFFu) | 0x50000000u);
            gfx_circle(cx, cy, rr, 2.0f * s, c);
        } else {
            gfx_frame((float)g.left, (float)g.top, (float)rw(&g), (float)rh(&g), 2.0f * s, c);
        }
    }
    RECT r = live_rect(e), o;
    if (!IntersectRect(&o, &r, clip)) return;
    gpt_t p[CB_PTS];
    for (int k = 0; k < CB_PTS; k++) { p[k].x = t.pt[k][0]; p[k].y = t.pt[k][1]; }
    int removing = e->cb.act == CA_REMOVE;
    DWORD col = removing ? 0x90FF7468 : qm_family(t.src);
    gfx_lines(p, CB_PTS, 6.4f * s, removing ? 0x90101012 : 0xE6101012);
    gfx_lines(p, CB_PTS, 4.0f * s, col);
    gfx_disc(t.x0, t.y0, 4.4f * s, 0xFF101012);   /* the plugs: in the jack, at the far end */
    gfx_disc(t.x0, t.y0, 3.1f * s, col);
    if (e->cb.drag == CD_SRC && !removing) {   /* (its destination's ring stays where it is) */
        gfx_circle(t.x1, t.y1, t.rad, 3.4f * s, 0xE0101012);
        gfx_circle(t.x1, t.y1, t.rad, 1.9f * s, col);
    } else {
        gfx_disc(t.x1, t.y1, 4.4f * s, 0xFF101012);
        gfx_disc(t.x1, t.y1, 3.1f * s, col);
    }
    if (removing) {   /* an X where it would be let go */
        float x = (float)e->cb.mx + 10 * s, y = (float)e->cb.my - 10 * s, d = 4 * s;
        gfx_disc(x, y, 7 * s, 0xF06A2420);
        gfx_line(x - d, y - d, x + d, y + d, 1.6f * s, 0xFFFFFFFF);
        gfx_line(x - d, y + d, x + d, y - d, 1.6f * s, 0xFFFFFFFF);
    }
}

/* ---- (2026-10-31) the settings box ---- */
static int col_param(int m, int col, int c) {   /* Synth c's param of row m's column col (COL_ON: its ENABLE); -1 none */
    if (col == COL_ON) return MATRIX_ON[m] >= 0 ? synth_param(MATRIX_ON[m], c) : -1;
    return MATRIX[m][col] >= 0 ? synth_param(MATRIX[m][col], c) : -1;
}
static void small_button(const RECT *r, const char *t, int hot, int lit, int dim) {
    if (IsRectEmpty(r)) return;
    gfx_rect((float)r->left, (float)r->top, (float)rw(r), (float)rh(r), lit ? 0xFF1F5A2A : 0xFF0F2230);
    gfx_frame((float)r->left, (float)r->top, (float)rw(r), (float)rh(r), 1.0f, hot ? 0xFFFFFFFF : lit ? 0xFF7FE38A : 0xFF3E6273);
    ltext(r, t, font_tiny, hot ? RGB(255, 255, 255) : dim ? RGB(84, 112, 124) : RGB(150, 215, 232), DT_CENTER);
}
static void box_draw(ed_t *e, const RECT *clip) {
    box_t bx;
    cb_ui_t *b = &e->cb;
    int k = b->sel >= 0 ? cable_of_row(e, b->sel) : -1;
    if (k < 0 || !box_geom(e, &bx) || !seen((float)bx.box.left, (float)bx.box.top, (float)bx.box.right, (float)bx.box.bottom, clip))
        return;
    const cb_cable_t *c = &b->cab[k];
    int f = e->focus, m = c->row, hz = e->hover - Z_CB_BOX;
    float s = L.s;
    RECT lab = bx.box;
    gfx_rect((float)lab.left, (float)lab.top, (float)rw(&lab), (float)rh(&lab), 0xF40B1621);
    gfx_frame((float)lab.left, (float)lab.top, (float)rw(&lab), (float)rh(&lab), 1.0f, fade(qm_family(c->src), 0xFF));
    char t[200];
    title_text(e, c, t, sizeof t);
    ltext(&bx.title, t, font_cell, b->drag == CD_AMT ? RGB(255, 255, 255) : RGB(150, 215, 232), DT_LEFT);
    small_button(&bx.it[CBX_ON], c->on ? "ON" : "OFF", hz == CBX_ON, c->on, !c->on);
    RECT x = bx.x;
    int hot = e->hover == Z_CB_X;
    gfx_rect((float)x.left, (float)x.top, (float)rw(&x), (float)rh(&x), hot ? 0xFFB03A30 : 0xFF6A2420);
    float cx = (x.left + x.right) / 2.0f, cy = (x.top + x.bottom) / 2.0f, d = 3.5f * s;
    gfx_line(cx - d, cy - d, cx + d, cy + d, 1.6f * s, 0xFFFFFFFF);
    gfx_line(cx - d, cy + d, cx + d, cy - d, 1.6f * s, 0xFFFFFFFF);
    static const char *const NAME[3] = {"AMOUNT", "VIA", "XFORM"};
    static const int LIST[3] = {-1, CBX_VIA, CBX_XF}, DN[3] = {CBX_AMT_DN, CBX_VIA_DN, CBX_XF_DN};
    static const int COL[3] = {MX_AMT, MX_VIA_AMT, MX_XF_AMT}, OPT[3] = {-1, MX_VIA, MX_XF};
    for (int r = 0; r < 3; r++) {
        RECT v = bx.it[DN[r] + 1];
        if (IsRectEmpty(&v)) continue;
        int opt = OPT[r] >= 0 && MATRIX[m][OPT[r]] >= 0 ? opt_at(e, cell(m, OPT[r], f)) : 1, dim = !opt;
        RECT nr = {lab.left + sc(6), v.top, lab.left + sc(6) + sc(46), v.bottom};
        ltext(&nr, NAME[r], font_tiny, RGB(150, 215, 232), DT_LEFT);
        int i = cell(m, COL[r], f);
        float val = real_at(e, i);
        if (LIST[r] >= 0 && !IsRectEmpty(&bx.it[LIST[r]])) {   /* its list: the choice, and a small arrow */
            RECT l = bx.it[LIST[r]], lt = {l.left + sc(4), l.top, l.right - sc(12), l.bottom};
            int lh_ = hz == LIST[r];
            gfx_rect((float)l.left, (float)l.top, (float)rw(&l), (float)rh(&l), 0xFF0F2230);
            gfx_frame((float)l.left, (float)l.top, (float)rw(&l), (float)rh(&l), 1.0f, lh_ ? 0xFFFFFFFF : 0xFF3E6273);
            ltext(&lt, PARAMS[cell(m, OPT[r], f)].opts[opt], font_tiny, lh_ ? RGB(255, 255, 255) : RGB(150, 215, 232), DT_LEFT);
            float ax = l.right - sc(7), ay = (l.top + l.bottom) / 2.0f;
            gpt_t tri[3] = {{ax - 3 * s, ay - 1.5f * s}, {ax + 3 * s, ay - 1.5f * s}, {ax, ay + 2 * s}};
            gfx_poly(tri, 3, lh_ ? 0xFFFFFFFF : 0xFF96D7E8);
        } else if (r == 0) {   /* AMOUNT: a bar from the middle, as the knob's arc (green +, red -) */
            RECT bar = {nr.right, v.top + sc(4), bx.it[CBX_AMT_DN].left - sc(3), v.bottom - sc(4)};
            float mid = (bar.left + bar.right) / 2.0f, q = val / 100.0f,   /* (the bar: -100 .. +100%; beyond, full) */
                  w = rw(&bar) / 2.0f * (q < -1 ? -1 : q > 1 ? 1 : q);
            gfx_rect((float)bar.left, (float)bar.top, (float)rw(&bar), (float)rh(&bar), 0xFF1A2630);
            gfx_rect(w < 0 ? mid + w : mid, (float)bar.top, fabsf(w), (float)rh(&bar), val >= 0 ? 0xFF7FE38A : 0xFFFF7468);
            gfx_line(mid, (float)bar.top - s, mid, (float)bar.bottom + s, 1.0f, 0xFFE8E8EC);
        }
        char vt[24];
        snprintf(vt, sizeof vt, PARAMS[i].min < 0 ? "%+.0f%%" : "%.0f%%", val);
        int vh = hz == DN[r] + 1 || (b->drag == CD_AMT && b->col == COL[r]);
        gfx_rect((float)v.left, (float)v.top, (float)rw(&v), (float)rh(&v), 0xFF06121A);
        gfx_frame((float)v.left, (float)v.top, (float)rw(&v), (float)rh(&v), 1.0f, vh ? 0xFFFFFFFF : 0xFF3E6273);
        ltext(&v, vt, font_tiny, vh ? RGB(255, 255, 255) : dim ? RGB(84, 112, 124) : RGB(226, 228, 232), DT_CENTER);
        small_button(&bx.it[DN[r]], "<", hz == DN[r], 0, dim);
        small_button(&bx.it[DN[r] + 2], ">", hz == DN[r] + 2, 0, dim);
    }
    b->label_at = lab;
    InflateRect(&b->label_at, sc(2), sc(2));
}
void cb_draw(HDC dc, ed_t *e, const RECT *clip) {
    cb_ui_t *b = &e->cb;
    if (!b->on) return;
    refresh(e);
    if (!b->layer || b->layer_sig != b->sig) build(e);
    RECT keep[2] = {L.tabline, L.help};   /* (the screen's title and help lines stay bright) */
    skin_layer_blend(b->layer, b->lw, b->lh, clip, keep, 2);
    float s = L.s;
    int sk = b->sel >= 0 ? cable_of_row(e, b->sel) : -1;
    if (sk >= 0) {   /* the selected one: bright, over the others (behind the screen's title and help lines) */
        int saved = SaveDC(dc);
        ExcludeClipRect(dc, L.tabline.left, L.tabline.top, L.tabline.right, L.tabline.bottom);
        ExcludeClipRect(dc, L.help.left, L.help.top, L.help.right, L.help.bottom);
        gfx_begin(dc);
        draw_cable(&b->cab[sk], 1);
        gfx_end();
        RestoreDC(dc, saved);
    }
    gfx_begin(dc);
    skin_bind(gfx_g);
    for (int j = 0; j < b->nj; j++) {   /* the jacks' names */
        const cb_jack_t *k = &b->jack[j];
        RECT r;
        COLORREF c = k->dst ? RGB(170, 176, 186) : RGB(226, 228, 232);
        if (k->src == MS_BEND || k->src == MS_WHEEL) continue;   /* (2026-10-31: always shown; PITCH / MOD say it) */
        if (k->y >= L.keys.top) SetRect(&r, (int)(k->x - sc(35)), (int)(k->y + sc(7)), (int)(k->x + sc(35)), (int)(k->y + sc(19)));
        else if (k->src == MS_PAD_X || k->src == MS_PAD_Y || k->src == MS_PAD_GATE)
            SetRect(&r, (int)(k->x + 7 * s), (int)(k->y - 6 * s), (int)(k->x + (k->src == MS_PAD_GATE ? 36 : 18) * s), (int)(k->y + 6 * s));
        else if (qm_is_gen_src(k->src)) SetRect(&r, (int)(k->x - 94 * s), (int)(k->y - 6 * s), (int)(k->x - 9 * s), (int)(k->y + 6 * s));
        else continue;   /* (an LFO, an envelope, a wheel: its panel says what it is) */
        if (!seen((float)r.left, (float)r.top, (float)r.right, (float)r.bottom, clip)) continue;
        const char *t = k->src == MS_PAD_X ? "X" : k->src == MS_PAD_Y ? "Y" : k->src == MS_PAD_GATE ? "GATE" : k->label;
        UINT fmt = k->y >= L.keys.top ? DT_CENTER : qm_is_gen_src(k->src) ? DT_RIGHT : DT_LEFT;
        ltext(&r, t, font_tiny, e->hover == Z_CB_JACK + j ? RGB(255, 255, 255) : c, fmt);
    }
    {   /* CABLES OFF at the bay's left */
        RECT o = off_rect();
        if (seen((float)o.left, (float)o.top, (float)o.right, (float)o.bottom, clip)) {
            int hot = e->hover == Z_CB_OFF;
            gfx_rect((float)o.left, (float)o.top, (float)rw(&o), (float)rh(&o), hot ? 0xFF3A3A40 : 0xFF26262A);
            gfx_frame((float)o.left, (float)o.top, (float)rw(&o), (float)rh(&o), 1.0f, hot ? 0xFFFFFFFF : 0xFF8A8A92);
            ltext(&o, "CABLES OFF", font_tiny, hot ? RGB(255, 255, 255) : RGB(226, 228, 232), DT_CENTER);
        }
    }
    for (int k = 0; k < b->n; k++) {   /* the amount knobs */
        const cb_cable_t *c = &b->cab[k];
        if (c->row + 1 == b->hide || !seen(c->kx - 12 * s, c->ky - 12 * s, c->kx + 12 * s, c->ky + 12 * s, clip)) continue;
        float amt = real_at(e, cell(c->row, MX_AMT, e->focus));
        b->cab[k].amt = amt;
        amount_knob(c, amt, e->hover == Z_CB_KNOB + k || e->cb.sel == c->row);
    }
    for (int k = 0; k < b->n; k++) {   /* (2026-10-31) the grab handles near each end (drag one to re-patch) */
        const cb_cable_t *c = &b->cab[k];
        if (c->row + 1 == b->hide) continue;
        for (int end = 0; end < 2; end++) {
            if (!seen(c->hx[end] - 6 * s, c->hy[end] - 6 * s, c->hx[end] + 6 * s, c->hy[end] + 6 * s, clip)) continue;
            int hot = e->hover == Z_CB_HANDLE + 2 * k + end;
            gfx_disc(c->hx[end], c->hy[end], (hot ? 4.6f : 3.4f) * s, 0xF0101012);
            gfx_disc(c->hx[end], c->hy[end], (hot ? 3.4f : 2.3f) * s, hot ? 0xFFFFFFFF : 0xFFE8E8EC);
        }
    }
    gfx_end();
    if (b->drag == CD_NEW || b->drag == CD_DST || b->drag == CD_SRC) {   /* (behind the screen's title and help lines) */
        int saved = SaveDC(dc);
        ExcludeClipRect(dc, L.tabline.left, L.tabline.top, L.tabline.right, L.tabline.bottom);
        ExcludeClipRect(dc, L.help.left, L.help.top, L.help.right, L.help.bottom);
        gfx_begin(dc);
        live_draw(e, clip);
        gfx_end();
        RestoreDC(dc, saved);
    }
    gfx_begin(dc);
    skin_bind(gfx_g);
    box_draw(e, clip);
    gfx_end();
    skin_bind(NULL);
}

/* ---- the mouse ---- */
static float seg_dist(float px, float py, const float *a, const float *b) {
    float dx = b[0] - a[0], dy = b[1] - a[1], l = dx * dx + dy * dy, t = l > 0 ? ((px - a[0]) * dx + (py - a[1]) * dy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(px - (a[0] + t * dx), py - (a[1] + t * dy));
}
int cb_hit(ed_t *e, POINT pt) {
    if (e->cb.probe) return -1;   /* (cables.c asks the panel what is under a dragged cable: ed_hit) */
    RECT bt = cb_button_rect();
    if (e->screen == SCR_MATRIX && PtInRect(&bt, pt)) return Z_CB_BTN;
    if (!e->cb.on) return -1;
    refresh(e);
    float x = (float)pt.x, y = (float)pt.y, s = L.s;
    RECT lab, xb, off = off_rect();
    if (label_rects(e, &lab, &xb)) {   /* the selected cable's settings box: its X, its controls, else it eats the click */
        if (PtInRect(&xb, pt)) return Z_CB_X;
        int it = box_item_at(e, pt);
        if (it >= 0) return Z_CB_BOX + it;
    }
    if (PtInRect(&off, pt)) return Z_CB_OFF;
    for (int k = e->cb.n - 1; k >= 0; k--)
        if (hypotf(e->cb.cab[k].kx - x, e->cb.cab[k].ky - y) < 11 * s) return Z_CB_KNOB + k;
    int nearest = -1;   /* (2026-10-31) the grab handles: the nearest (cables from one jack start close together) */
    float best = 5.5f * s;
    for (int k = 0; k < e->cb.n; k++)
        for (int end = 0; end < 2; end++) {
            float d = hypotf(e->cb.cab[k].hx[end] - x, e->cb.cab[k].hy[end] - y);
            if (d < best) { best = d; nearest = Z_CB_HANDLE + 2 * k + end; }
        }
    if (nearest >= 0) return nearest;
    for (int j = 0; j < e->cb.nj; j++)
        if (hypotf(e->cb.jack[j].x - x, e->cb.jack[j].y - y) < 7.5f * s) return Z_CB_JACK + j;
    if (!e->qm.src)   /* (while quick assign waits for a knob, the knobs under the cables take the click) */
        for (int k = e->cb.n - 1; k >= 0; k--)
            for (int p = 0; p + 1 < CB_PTS; p++)
                if (seg_dist(x, y, e->cb.cab[k].pt[p], e->cb.cab[k].pt[p + 1]) < 4.5f * s) return Z_CB_WIRE + k;
    RECT bp = bay_plate(e);
    if (PtInRect(&bp, pt)) return Z_CB_BAY;
    return -1;
}
static void edit_begin(ed_t *e, int i, int end) {
    if (e->host && i >= 0) e->host(e->fx, end ? audioMasterEndEdit : audioMasterBeginEdit, i, 0, NULL, 0);
}
static void put(ed_t *e, int i, float v) {
    if (i < 0 || fabsf(e->fx->getParameter(e->fx, i) - v) < 1e-7f) return;
    lib_touch(e, NULL);
    e->fx->setParameter(e->fx, i, v);
    if (e->host) e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
}
static void put_once(ed_t *e, int i, float v) {   /* one edit, told to the host as a click on a knob is */
    if (i < 0 || fabsf(e->fx->getParameter(e->fx, i) - v) < 1e-7f) return;
    edit_begin(e, i, 0);
    put(e, i, v);
    edit_begin(e, i, 1);
}
static float norm_of(int i, float v) {   /* an option's number or a value (%) -> the param's 0..1 */
    if (PARAMS[i].nopts > 1) return v <= 0 ? 0.0f : v >= PARAMS[i].nopts - 1 ? 1.0f : v / (PARAMS[i].nopts - 1);
    return amt_norm(i, v);
}
static void peer_rows(ed_t *e, int row, int *rows) {   /* the same connection in each Synth edited together (-1: none) */
    int s0 = row_src(e, row, e->focus), d0 = row_dst(e, row, e->focus);
    for (int c = 0; c < NSYNTHS; c++) {
        rows[c] = -1;
        if (c == e->focus) { rows[c] = row; continue; }
        if (!(e->focus_mask >> c & 1u)) continue;
        for (int m = 0; m < NMATRIX && rows[c] < 0; m++)
            if (row_src(e, m, c) == s0 && row_dst(e, m, c) == d0) rows[c] = m;
    }
}
static void say(ed_t *e, const char *fmt, ...) {   /* what a drag or the box did, on the help line for a while */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->cb.msg, sizeof e->cb.msg, fmt, ap);
    va_end(ap);
    e->cb.msg_at = GetTickCount();
    RECT h = L.help;
    InflateRect(&h, sc(2), sc(6));
    inval(e, h);
}
/* one edit of row's column col (COL_ON: its ENABLE) to v (an option's number or a value in %), in the Synths edited
 * together too: one undo step, told to the host as a knob is, the MATRIX page's own params */
static void edit_col(ed_t *e, int row, int col, float v, const char *what) {
    int rows[NSYNTHS];
    peer_rows(e, row, rows);
    lib_hold(e, 1);
    lib_touch(e, what);
    for (int c = 0; c < NSYNTHS; c++) {
        int i = rows[c] >= 0 ? col_param(rows[c], col, c) : -1;
        if (i >= 0) put_once(e, i, norm_of(i, v));
    }
    lib_hold(e, -1);
    inval_label(e);
    if (e->screen == SCR_MATRIX) inval(e, L.screen);
}

/* ---- dragging a value: the amount knob, the box's AMOUNT / VIA / XFORM values (col) ---- */
static void value_drag(ed_t *e, int row, int col, int y) {
    int k = cable_of_row(e, row);
    if (k < 0) return;
    const cb_cable_t *c = &e->cb.cab[k];
    select_row(e, row);
    peer_rows(e, row, e->cb.rows);
    static const char *const W[MX_COLS] = {"", "", "amount", "", "VIA amount", "", "XFORM amount"};
    char what[80];
    snprintf(what, sizeof what, "Cable %s %s > %s", W[col], qm_src_name(c->src), qm_dst_name(c->dst));
    lib_hold(e, 1);   /* the drag: one undo step */
    lib_touch(e, what);
    e->cb.col = col;
    for (int s = 0; s < NSYNTHS; s++)
        if (e->cb.rows[s] >= 0) edit_begin(e, cell(e->cb.rows[s], col, s), 0);
    e->cb.drag_y = y;
    e->cb.drag_from = real_at(e, cell(row, col, e->focus));
    e->cb.drag = CD_AMT;
    e->mode = M_CABLE;
    SetCapture(e->wnd);
}
static void drag_start(ed_t *e, int k, int y) { value_drag(e, e->cb.cab[k].row, MX_AMT, y); }

/* ---- (2026-10-31) dragging a cable: from a source's jack (a new one), or a cable's end (re-patched) ---- */
static int jack_at(ed_t *e, POINT pt, int sources) {   /* a source's (or a destination's) jack under pt, or -1 */
    for (int j = 0; j < e->cb.nj; j++)
        if ((sources ? e->cb.jack[j].src > 0 : e->cb.jack[j].dst > 0) &&
            hypotf(e->cb.jack[j].x - pt.x, e->cb.jack[j].y - pt.y) < 9.0f * L.s)
            return j;
    return -1;
}
static int matrix_full(ed_t *e, int s, int d) {   /* the focused Synth has no row for s -> d and no free one */
    for (int m = 0; m < NMATRIX; m++)
        if (row_dst(e, m, e->focus) == 0 || (row_src(e, m, e->focus) == s && row_dst(e, m, e->focus) == d)) return 0;
    return 1;
}
static void ring_of(ed_t *e, int h, RECT *r, int *round) {   /* where panel hit h is drawn, to light it */
    int i = base_of(h);
    *round = 0;
    if (e->hit_screen) { *r = e->hit_rect; InflateRect(r, sc(2), sc(2)); return; }
    if (L.kind[i] == K_KNOB || L.kind[i] == K_SMALL || L.kind[i] == K_BIG) {
        float cx, cy, rr;
        knob_geom(i, &cx, &cy, &rr, NULL);
        rr += 4.5f * L.s;
        SetRect(r, (int)(cx - rr), (int)(cy - rr), (int)(cx + rr), (int)(cy + rr));
        *round = 1;
        return;
    }
    *r = L.ctl[i];
    InflateRect(r, sc(3), sc(3));
}
static void aim(ed_t *e, int x, int y) {   /* what letting go at x, y would do: e->cb.act, its target, its light */
    cb_ui_t *b = &e->cb;
    POINT pt = {x, y};
    RECT ring = {0, 0, 0, 0};
    int act = CA_NONE, tsrc = 0, tdst = 0, round = 1, h = -1;
    int k = b->drag == CD_NEW ? -1 : cable_of_row(e, b->drow);
    RECT bp = bay_plate(e);
    if (b->drag == CD_SRC) {
        int j = jack_at(e, pt, 1);
        if (j >= 0) {
            tsrc = b->jack[j].src;
            act = k >= 0 && tsrc == b->cab[k].src ? CA_SAME : CA_MOVE;
            float r = 9.0f * L.s;
            SetRect(&ring, (int)(b->jack[j].x - r), (int)(b->jack[j].y - r), (int)(b->jack[j].x + r), (int)(b->jack[j].y + r));
        } else {
            h = ed_hit(e, x, y);
            act = h < 0 || PtInRect(&bp, pt) ? CA_REMOVE : CA_NOT_SRC;
        }
    } else {
        int j = jack_at(e, pt, 0);
        if (j >= 0) {
            tdst = b->jack[j].dst;
            float r = 9.0f * L.s;
            SetRect(&ring, (int)(b->jack[j].x - r), (int)(b->jack[j].y - r), (int)(b->jack[j].x + r), (int)(b->jack[j].y + r));
        } else {
            h = ed_hit(e, x, y);
            int d = h >= 0 && h < NPARAMS ? qm_dst_of(base_of(h)) : -1;
            if (d > 0) {
                tdst = d;
                ring_of(e, h, &ring, &round);
            } else if (h >= 0 && h < NPARAMS) act = CA_NOT_DST;
            else if (b->drag == CD_DST && (h < 0 || PtInRect(&bp, pt))) act = CA_REMOVE;
        }
        if (tdst > 0 && b->drag == CD_NEW) act = matrix_full(e, b->dsrc, tdst) ? CA_FULL : CA_CONNECT;
        else if (tdst > 0) act = k >= 0 && tdst == b->cab[k].dst ? CA_SAME : CA_MOVE;
    }
    if (act != b->act || tsrc != b->tsrc || tdst != b->tdst || h != b->thit) {   /* the help line says it */
        RECT hl = L.help;
        InflateRect(&hl, sc(2), sc(6));
        inval(e, hl);
    }
    if (!EqualRect(&ring, &b->ring)) {
        RECT o = b->ring, n = ring;
        InflateRect(&o, sc(7), sc(7));
        InflateRect(&n, sc(7), sc(7));
        inval(e, o);
        inval(e, n);
    }
    b->act = act;
    b->tsrc = tsrc;
    b->tdst = tdst;
    b->thit = h;
    b->ring = ring;
    b->ring_round = round;
}
static void live_move(ed_t *e, int x, int y) {   /* the cable follows the mouse: its old and new areas repaint */
    inval(e, e->cb.live_at);
    e->cb.mx = x;
    e->cb.my = y;
    aim(e, x, y);
    e->cb.live_at = live_rect(e);
    inval(e, e->cb.live_at);
}
static void press_source(ed_t *e, int s, int x, int y) {   /* a press on a source's jack: a click, or a drag from it */
    e->cb.drag = CD_PRESS;
    e->cb.dsrc = s;
    e->cb.px = e->cb.mx = x;
    e->cb.py = e->cb.my = y;
    e->mode = M_CABLE;
    SetCapture(e->wnd);
}
static void press_end(ed_t *e, int k, int end, int x, int y) {   /* a press on a cable's grab handle: its end follows */
    const cb_cable_t *c = &e->cb.cab[k];
    select_row(e, -1);   /* (the box would be in the way) */
    e->cb.drag = end ? CD_DST : CD_SRC;
    e->cb.drow = c->row;
    e->cb.dsrc = c->src;
    e->cb.px = x;
    e->cb.py = y;
    e->cb.act = CA_SAME;
    e->cb.hide = c->row + 1;   /* (the layer drawn once without it, while its end is in the hand) */
    e->mode = M_CABLE;
    SetCapture(e->wnd);
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
    live_move(e, x, y);
}
static void repatch(ed_t *e, int row, int col, int v) {   /* row's source or destination (col) to v: one undo step */
    int s = row_src(e, row, e->focus), d = row_dst(e, row, e->focus);
    char what[96];
    snprintf(what, sizeof what, "Re-patch %s > %s", qm_src_name(col == MX_SRC ? v : s), qm_dst_name(col == MX_DST ? v : d));
    edit_col(e, row, col, (float)v, what);
    say(e, "Row %d is now %s > %s (UNDO: back as it was).", row + 1, qm_src_name(col == MX_SRC ? v : s),
        qm_dst_name(col == MX_DST ? v : d));
}
static void drop(ed_t *e, int kind) {   /* the mouse let go after a drag: what aim() said */
    cb_ui_t *b = &e->cb;
    int act = b->act;
    if (kind == CD_NEW) {
        int how = 0, row = act == CA_CONNECT ? qm_connect(e, b->dsrc, b->tdst, (float)NEW_AMT, &how) : -1;
        if (row >= 0) {
            refresh(e);
            select_row(e, row);
            char also[48] = "";
            for (int c = 0, n = 0; c < NSYNTHS; c++)
                if (how >> c & 2) n += snprintf(also + n, sizeof also - n, n ? "+%d" : "; not in Synth %d (its matrix is full)", c + 1);
            if (how & 1)
                say(e, "%s > %s connected at %+d%% (row %d%s). Drag its middle knob, or use the box, for the amount.",
                    qm_src_name(b->dsrc), qm_dst_name(b->tdst), NEW_AMT, row + 1, also);
            else
                say(e, "%s > %s was connected already (row %d%s): its settings are in the box.", qm_src_name(b->dsrc),
                    qm_dst_name(b->tdst), row + 1, also);
        } else if (act == CA_FULL || (act == CA_CONNECT && row < 0)) {
            say(e, "All %d matrix rows of Synth %d are in use: %s > %s was not connected. Free a row first (click a cable, "
                "then its X).", NMATRIX, e->focus + 1, qm_src_name(b->dsrc), qm_dst_name(b->tdst));
        } else {
            say(e, "Nothing connected: let go over a knob (it lights up) or a destination jack in the bay.");
        }
        return;
    }
    if (act == CA_MOVE) repatch(e, b->drow, kind == CD_DST ? MX_DST : MX_SRC, kind == CD_DST ? b->tdst : b->tsrc);
    else if (act == CA_REMOVE) {
        qm_remove_row(e, b->drow);
        say(e, "Row %d removed (UNDO brings it back).", b->drow + 1);
    } else if (act != CA_SAME) say(e, "Nothing changed: that is not a %s.", kind == CD_DST ? "destination" : "source");
}

void cb_toggle(ed_t *e) {
    e->cb.on = !e->cb.on;
    e->cb.sel = -1;
    e->cb.sig = 0;
    SetRectEmpty(&e->cb.label_at);
    if (!e->cb.on) {
        free(e->cb.layer);
        e->cb.layer = NULL;
        e->cb.lw = e->cb.lh = 0;
    } else {
        refresh(e);
    }
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
}
static int menu_pick(ed_t *e, int i, const RECT *at) {   /* a list of param i's options: the one picked, or -1 */
    int choice;
    if (e->qm.answer) {   /* (load_test answers it: option + 1) */
        choice = e->qm.answer;
        e->qm.answer = 0;
    } else {
        HMENU m = CreatePopupMenu();
        int cur = opt_at(e, i);
        for (int o = 0; o < PARAMS[i].nopts; o++)
            AppendMenuA(m, MF_STRING | (o == cur ? MF_CHECKED : 0) | (o && o % 18 == 0 ? MF_MENUBARBREAK : 0),
                        (UINT_PTR)(o + 1), PARAMS[i].opts[o]);
        POINT pt = {at->left, at->bottom};
        ClientToScreen(e->wnd, &pt);
        choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
        DestroyMenu(m);
    }
    return choice >= 1 && choice <= PARAMS[i].nopts ? choice - 1 : -1;
}
static void box_press(ed_t *e, int it, int y) {   /* a click on the settings box's control it */
    int m = e->cb.sel, f = e->focus, k = cable_of_row(e, m);
    if (m < 0 || k < 0) return;
    const cb_cable_t *c = &e->cb.cab[k];
    char what[96];
    box_t bx;
    box_geom(e, &bx);
    switch (it) {
    case CBX_AMT: value_drag(e, m, MX_AMT, y); return;
    case CBX_VIA_AMT: value_drag(e, m, MX_VIA_AMT, y); return;
    case CBX_XF_AMT: value_drag(e, m, MX_XF_AMT, y); return;
    case CBX_AMT_DN: case CBX_AMT_UP: case CBX_VIA_DN: case CBX_VIA_UP: case CBX_XF_DN: case CBX_XF_UP: {
        int col = it <= CBX_AMT_UP ? MX_AMT : it <= CBX_VIA_UP ? MX_VIA_AMT : MX_XF_AMT;
        int up = it == CBX_AMT_UP || it == CBX_VIA_UP || it == CBX_XF_UP, i = cell(m, col, f);
        float v = real_at(e, i) + (up ? BOX_STEP : -BOX_STEP);
        v = v < PARAMS[i].min ? PARAMS[i].min : v > PARAMS[i].max ? PARAMS[i].max : v;
        snprintf(what, sizeof what, "Cable %s %s > %s", col == MX_AMT ? "amount" : col == MX_VIA_AMT ? "VIA amount" : "XFORM amount",
                 qm_src_name(c->src), qm_dst_name(c->dst));
        edit_col(e, m, col, v, what);
        return;
    }
    case CBX_ON:
        snprintf(what, sizeof what, "Cable %s %s > %s", c->on ? "off" : "on", qm_src_name(c->src), qm_dst_name(c->dst));
        edit_col(e, m, COL_ON, c->on ? 0.0f : 1.0f, what);
        say(e, "Row %d %s (ON / OFF: its ENABLE on the MATRIX page).", m + 1, c->on ? "switched off: it does nothing now" : "on again");
        refresh(e);
        return;
    case CBX_VIA: case CBX_XF: {
        int col = it == CBX_VIA ? MX_VIA : MX_XF, i = cell(m, col, f), o = menu_pick(e, i, &bx.it[it]);
        if (o < 0 || o == opt_at(e, i)) return;
        snprintf(what, sizeof what, "Cable %s %s", it == CBX_VIA ? "VIA" : "XFORM", PARAMS[i].opts[o]);
        edit_col(e, m, col, (float)o, what);
        return;
    }
    }
}
int cb_press(ed_t *e, int z, int x, int y) {
    if (z == Z_CB_BTN) { cb_toggle(e); return 1; }
    if (!e->cb.on) return 0;
    if (z == Z_CB_OFF) { cb_toggle(e); return 1; }
    if (z == Z_CB_BAY) return 1;
    if (z == Z_CB_X) {   /* the selected cable's row goes (one undo step: quickmod.c's REMOVE) */
        int row = e->cb.sel;
        select_row(e, -1);
        if (row >= 0) qm_remove_row(e, row);
        refresh(e);
        return 1;
    }
    if (z >= Z_CB_BOX && z < Z_CB_BOX + CBX_COUNT) { box_press(e, z - Z_CB_BOX, y); return 1; }
    if (z >= Z_CB_KNOB && z < Z_CB_KNOB + e->cb.n) { drag_start(e, z - Z_CB_KNOB, y); return 1; }
    if (z >= Z_CB_HANDLE && z < Z_CB_HANDLE + 2 * e->cb.n) { press_end(e, (z - Z_CB_HANDLE) / 2, (z - Z_CB_HANDLE) % 2, x, y); return 1; }
    if (z >= Z_CB_WIRE && z < Z_CB_WIRE + e->cb.n) {   /* select it (again: not) */
        int row = e->cb.cab[z - Z_CB_WIRE].row;
        select_row(e, e->cb.sel == row ? -1 : row);
        return 1;
    }
    if (z >= Z_CB_JACK && z < Z_CB_JACK + e->cb.nj) {
        const cb_jack_t *j = &e->cb.jack[z - Z_CB_JACK];
        if (j->src > 0) press_source(e, j->src, x, y);   /* let go there: quick assign from it; dragged: a new cable */
        else
            for (int k = 0; k < e->cb.n; k++)   /* a destination's jack: its (first) cable */
                if (e->cb.cab[k].dst == j->dst) { select_row(e, e->cb.cab[k].row); break; }
        return 1;
    }
    if (e->cb.sel >= 0) select_row(e, -1);   /* a click elsewhere: the panel's, and the selection goes */
    return 0;
}
void cb_drag(ed_t *e, int x, int y, int fine) {
    cb_ui_t *b = &e->cb;
    if (b->drag == CD_PRESS) {   /* moved far enough: a new cable from the source pressed */
        if (abs(x - b->px) <= DRAG_PX && abs(y - b->py) <= DRAG_PX) return;
        if (b->sel >= 0) select_row(e, -1);
        b->drag = CD_NEW;
        b->act = -1;
    }
    if (b->drag == CD_NEW || b->drag == CD_DST || b->drag == CD_SRC) { live_move(e, x, y); return; }
    if (b->drag != CD_AMT) return;
    float a = b->drag_from + (b->drag_y - y) * (fine ? 0.2f : 1.0f);
    if (!fine) a = roundf(a);
    for (int s = 0; s < NSYNTHS; s++) {
        int m = b->rows[s];
        if (m >= 0) put(e, cell(m, b->col, s), amt_norm(cell(m, b->col, s), a));
    }
    int k = cable_of_row(e, b->sel);
    if (k >= 0) inval_knob(e, k);
    box_t bx;   /* (just the box's line for this value, not all of it) */
    if (box_geom(e, &bx)) {
        RECT r = bx.it[b->col == MX_AMT ? CBX_AMT : b->col == MX_VIA_AMT ? CBX_VIA_AMT : CBX_XF_AMT];
        r.left = bx.box.left;
        InflateRect(&r, sc(1), sc(1));
        inval(e, r);
    }
}
void cb_release(ed_t *e, int up, int x, int y) {
    cb_ui_t *b = &e->cb;
    int kind = b->drag;
    b->drag = CD_NONE;
    e->mode = M_NONE;
    if (kind == CD_AMT) {
        for (int s = 0; s < NSYNTHS; s++)
            if (b->rows[s] >= 0) edit_begin(e, cell(b->rows[s], b->col, s), 1);
        lib_hold(e, -1);   /* the undo step */
        int k = cable_of_row(e, b->sel);
        if (k >= 0) inval_knob(e, k);
        inval_label(e);
        if (e->screen == SCR_MATRIX) inval(e, L.screen);
        return;
    }
    RECT hl = L.help;
    InflateRect(&hl, sc(2), sc(6));
    inval(e, hl);
    e->hover = -1;   /* (what was under the mouse when the press began is not there now) */
    if (kind == CD_PRESS) {   /* a plain click on a source's jack: quick assign from it (then click a knob), as before */
        if (up) qm_pick(e, b->dsrc);
        return;
    }
    if (kind != CD_NEW && kind != CD_DST && kind != CD_SRC) return;
    inval(e, b->live_at);
    RECT o = b->ring;
    InflateRect(&o, sc(7), sc(7));
    inval(e, o);
    if (up) {
        b->drag = kind;   /* (aim and drop read it) */
        aim(e, x, y);
        b->drag = CD_NONE;
        drop(e, kind);
    } else if (kind == CD_NEW) say(e, "Nothing connected.");
    SetRectEmpty(&b->live_at);
    SetRectEmpty(&b->ring);
    b->act = CA_NONE;
    if (b->hide) { b->hide = 0; if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }
    refresh(e);
}
int cb_key(ed_t *e, int vk) {
    if (vk != VK_ESCAPE || !e->cb.on || e->mode == M_CABLE) return 0;
    cb_toggle(e);
    return 1;
}
void cb_tick(ed_t *e) {   /* a row changed elsewhere (the MATRIX page, the host, undo): the cables again */
    if (e->cb.msg[0] && GetTickCount() - e->cb.msg_at > MSG_MS && e->mode != M_CABLE) {   /* the message runs out */
        e->cb.msg[0] = 0;
        RECT h = L.help;
        InflateRect(&h, sc(2), sc(6));
        inval(e, h);
    }
    if (!e->cb.on || e->mode == M_CABLE) return;
    refresh(e);
    for (int k = 0; k < e->cb.n; k++) {   /* an amount moved: just its knob (and the box) */
        float a = real_at(e, cell(e->cb.cab[k].row, MX_AMT, e->focus));
        if (a != e->cb.cab[k].amt) {
            e->cb.cab[k].amt = a;
            inval_knob(e, k);
            if (e->cb.cab[k].row == e->cb.sel) inval_label(e);
        }
    }
}
void cb_hover(ed_t *e, int z) {   /* the mouse moved onto or off z: it repaints (it lights) */
    if (z == Z_CB_BTN) { RECT r = cb_button_rect(); InflateRect(&r, sc(2), sc(2)); inval(e, r); return; }
    if (!e->cb.on || z < Z_CB || z >= Z_CB_END) return;
    if (z == Z_CB_OFF) { RECT r = off_rect(); InflateRect(&r, sc(2), sc(2)); inval(e, r); }
    else if (z >= Z_CB_KNOB && z < Z_CB_KNOB + e->cb.n) inval_knob(e, z - Z_CB_KNOB);
    else if (z >= Z_CB_HANDLE && z < Z_CB_HANDLE + 2 * e->cb.n) {
        const cb_cable_t *c = &e->cb.cab[(z - Z_CB_HANDLE) / 2];
        int end = (z - Z_CB_HANDLE) % 2;
        RECT r = {(int)(c->hx[end] - sc(7)), (int)(c->hy[end] - sc(7)), (int)(c->hx[end] + sc(7)), (int)(c->hy[end] + sc(7))};
        inval(e, r);
    } else if (z >= Z_CB_JACK && z < Z_CB_JACK + e->cb.nj) {
        const cb_jack_t *j = &e->cb.jack[z - Z_CB_JACK];
        RECT r = {(int)(j->x - sc(100)), (int)(j->y - sc(10)), (int)(j->x + sc(40)), (int)(j->y + sc(22))};
        inval(e, r);
    } else inval_label(e);
}
void cb_free(ed_t *e) {
    free(e->cb.layer);
    e->cb.layer = NULL;
    e->cb.lw = e->cb.lh = 0;
    e->cb.sig = e->cb.layer_sig = 0;
}

/* ---- the help line ---- */
static int drag_help(ed_t *e, char *out, int n) {   /* (2026-10-31) while a cable is dragged: what letting go would do */
    cb_ui_t *b = &e->cb;
    int k = b->drag == CD_NEW ? -1 : cable_of_row(e, b->drow);
    const char *src = qm_src_name(b->drag == CD_NEW ? b->dsrc : k >= 0 ? b->cab[k].src : 0);
    const char *here = b->thit >= 0 && b->thit < NPARAMS ? PARAMS[fp(e, base_of(b->thit))].name : "This";
    if (b->drag == CD_NEW) {
        switch (b->act) {
        case CA_CONNECT:
            snprintf(out, n, "Let go: %s > %s, connected at %+d%% (Synth %d, a free matrix row).\nThen drag the cable's "
                     "middle knob for the amount. Let go anywhere else: nothing changes.", src, qm_dst_name(b->tdst), NEW_AMT,
                     e->focus + 1);
            return 1;
        case CA_FULL:
            snprintf(out, n, "All %d matrix rows of Synth %d are in use: %s can't be connected to %s.\nFree a row first: "
                     "click a cable, then its X (or set a row's DESTINATION to Off on the MATRIX page).", NMATRIX,
                     e->focus + 1, src, qm_dst_name(b->tdst));
            return 1;
        case CA_NOT_DST:
            snprintf(out, n, "%s has no matrix destination: %s can't move it.\nLet go over another knob, or anywhere "
                     "else to connect nothing.", here, src);
            return 1;
        }
        snprintf(out, n, "A new cable from %s: let go over a knob (it lights up) or a destination jack in the bay to "
                 "connect it.\nLet go anywhere else: nothing is connected.", src);
        return 1;
    }
    int row = b->drow + 1;
    const char *dst = qm_dst_name(k >= 0 ? b->cab[k].dst : 0);
    switch (b->act) {
    case CA_MOVE:
        if (b->drag == CD_DST)
            snprintf(out, n, "Let go: row %d moves from %s to %s (%s > %s).\nOne undo step.", row, dst, qm_dst_name(b->tdst),
                     src, qm_dst_name(b->tdst));
        else
            snprintf(out, n, "Let go: row %d takes %s instead of %s (%s > %s).\nOne undo step.", row, qm_src_name(b->tsrc),
                     src, qm_src_name(b->tsrc), dst);
        return 1;
    case CA_REMOVE:
        snprintf(out, n, "Let go here: the cable is removed (row %d free again). UNDO brings it back.\nOver %s: it moves "
                 "there.", row, b->drag == CD_DST ? "another knob or destination jack" : "another source's jack");
        return 1;
    case CA_SAME:
        snprintf(out, n, "%s > %s, row %d: drag this end to %s.\nLet it go on empty panel: the cable is removed.", src, dst,
                 row, b->drag == CD_DST ? "another knob (it lights up) or a destination jack" : "another source's jack");
        return 1;
    }
    snprintf(out, n, "Not a %s: let go here and the cable stays as it was.\nOver %s: it moves there; on empty panel: it "
             "is removed.", b->drag == CD_DST ? "destination" : "source",
             b->drag == CD_DST ? "a knob or a destination jack" : "a source's jack");
    return 1;
}
static const char *box_help(int it) {
    switch (it) {
    case CBX_AMT: return "AMOUNT: how much the source moves the destination. Drag up or down (1% a pixel, Shift: fine), or "
                         "click < > (5% a click). Green: positive; red: negative.";
    case CBX_AMT_DN: case CBX_AMT_UP: return "< >: the amount 5% lower / higher (below 0 the source works the other way round).";
    case CBX_VIA: return "VIA: a second source that scales this cable (the mod wheel, velocity, an envelope ...). Click for "
                         "the list; None: always full.";
    case CBX_VIA_AMT: case CBX_VIA_DN: case CBX_VIA_UP:
        return "VIA AMOUNT: how much VIA scales it (100%: fully; negative: the other way). Drag the value, or click < > (5%).";
    case CBX_XF: return "XFORM: shapes the source (limits, squared, cubed). Click for the list; None: as it is.";
    case CBX_XF_AMT: case CBX_XF_DN: case CBX_XF_UP:
        return "XFORM AMOUNT: how much of the XFORM's shape. Drag the value, or click < > (5%).";
    case CBX_ON: return "ON / OFF: this row on or off (its ENABLE on the MATRIX page); off, the cable is faint and does nothing.";
    }
    return "This cable's settings (the same as its row on the MATRIX page). Click elsewhere to close; X removes the cable.";
}
int cb_help(ed_t *e, char *out, int n) {
    int z = e->hover;
    if (z == Z_CB_BTN) {
        snprintf(out, n, "CABLES: %s the matrix as patch cables over the panel: drag from a source's jack to a knob for a "
                 "new one, drag a cable's end dots to re-patch it, click a cable for its settings. Click again, CABLES OFF or "
                 "Esc: hidden.", e->cb.on ? "hides" : "shows");
        return 1;
    }
    if (!e->cb.on) return 0;
    if (e->mode == M_CABLE && (e->cb.drag == CD_NEW || e->cb.drag == CD_DST || e->cb.drag == CD_SRC)) return drag_help(e, out, n);
    int in = z >= Z_CB && z < Z_CB_END;
    if (e->qm.src && !in && e->mode != M_CABLE) return 0;   /* (quick assign's own help while it waits for a knob) */
    char t[200] = "";
    int k = z >= Z_CB_KNOB && z < Z_CB_KNOB + e->cb.n ? z - Z_CB_KNOB : z >= Z_CB_WIRE && z < Z_CB_WIRE + e->cb.n ? z - Z_CB_WIRE : -1;
    if (e->mode == M_CABLE && e->cb.drag == CD_AMT && e->cb.sel >= 0 && cable_of_row(e, e->cb.sel) >= 0) {
        const cb_cable_t *c = &e->cb.cab[cable_of_row(e, e->cb.sel)];
        static const char *const W[MX_COLS] = {"", "", "", "", "   VIA amount", "", "   XFORM amount"};
        snprintf(out, n, "%s > %s%s:  %+.1f%%   (Synth %d row %d)\nDrag up for more, down for less (the amount below 0: "
                 "red, the other way round); Shift: fine. Let go: one undo step.", qm_src_name(c->src), qm_dst_name(c->dst),
                 W[e->cb.col], real_at(e, cell(c->row, e->cb.col, e->focus)), e->focus + 1, c->row + 1);
        return 1;
    }
    if (e->mode == M_CABLE) return 0;   /* (a press on a jack, not moved yet) */
    if (z >= Z_CB_BOX && z < Z_CB_BOX + CBX_COUNT) { snprintf(out, n, "%s", box_help(z - Z_CB_BOX)); return 1; }
    if (e->cb.msg[0] && GetTickCount() - e->cb.msg_at <= MSG_MS) {   /* what the last drag or edit did */
        snprintf(out, n, "%s\nDrag from a source's jack to a knob: a new cable. Drag a cable's end dot: re-patch it. Click a "
                 "cable: its settings.", e->cb.msg);
        return 1;
    }
    if (k >= 0) {
        label_text(e, &e->cb.cab[k], t, sizeof t);
        snprintf(out, n, "Row %s\n%s", t, z >= Z_CB_KNOB && z < Z_CB_KNOB + CB_MAX
                 ? "Drag this knob up or down for the amount (green: positive, red: negative; Shift: fine). A click "
                   "selects the cable: its box has the settings and an X to remove it."
                 : "Click to select this cable (again: not): a box with its settings and an X. Its knob sets the amount; "
                   "the dots near its ends re-patch it.");
        return 1;
    }
    if (z >= Z_CB_HANDLE && z < Z_CB_HANDLE + 2 * e->cb.n) {
        const cb_cable_t *c = &e->cb.cab[(z - Z_CB_HANDLE) / 2];
        if ((z - Z_CB_HANDLE) % 2)
            snprintf(out, n, "%s > %s: drag this end to another knob or a destination jack to move the cable there.\nLet it "
                     "go on empty panel: the cable is removed (UNDO brings it back).", qm_src_name(c->src), qm_dst_name(c->dst));
        else
            snprintf(out, n, "%s > %s: drag this end to another source's jack to take that source instead.\nLet it go on "
                     "empty panel: the cable is removed (UNDO brings it back).", qm_src_name(c->src), qm_dst_name(c->dst));
        return 1;
    }
    if (z == Z_CB_X) {
        snprintf(out, n, "X: remove this cable (its matrix row is free again; in the Synths edited together too). UNDO "
                 "brings it back.");
        return 1;
    }
    if (z == Z_CB_OFF) { snprintf(out, n, "CABLES OFF: hides the cables (as CABLES on the MATRIX page, or Esc)."); return 1; }
    if (z >= Z_CB_JACK && z < Z_CB_JACK + e->cb.nj) {
        const cb_jack_t *j = &e->cb.jack[z - Z_CB_JACK];
        if (j->dst) snprintf(out, n, "%s: a destination with no knob on the panel. Drop a cable here to connect it; a click "
                             "selects its cable.", j->label);
        else snprintf(out, n, "%s: a source. Drag from it to a knob: a new cable. Or click it, then click knobs to connect "
                      "(quick assign; DONE or Esc when finished).", j->label);
        return 1;
    }
    if (z < 0 || z == Z_CB_BAY) {
        snprintf(out, n, "CABLES: Synth %d's %d connection%s. Drag from a source's jack to a knob for a new one; drag a "
                 "cable's end dot to re-patch it; click a cable for its settings. Esc or CABLES OFF: hidden.", e->focus + 1,
                 e->cb.n, e->cb.n == 1 ? "" : "s");
        return 1;
    }
    return 0;
}

/* ---- load_test ---- */
static intptr_t xy(float x, float y) { return (intptr_t)lroundf(x) | (intptr_t)lroundf(y) << 16; }
intptr_t cb_test(ed_t *e, intptr_t op, char *buf, float opt) {
    (void)buf;
    int k = (int)lroundf(opt);
    if (e->cb.on) refresh(e);
    switch (op) {
    case CBT_ON: return e->cb.on;
    case CBT_N: return e->cb.on ? e->cb.n : 0;
    case CBT_ROW: return k >= 0 && k < e->cb.n ? e->cb.cab[k].row : -1;
    case CBT_END_XY: {
        int c = k / 2;
        if (c < 0 || c >= e->cb.n) return -1;
        return k % 2 ? xy(e->cb.cab[c].x1, e->cb.cab[c].y1) : xy(e->cb.cab[c].x0, e->cb.cab[c].y0);
    }
    case CBT_KNOB_XY: return k >= 0 && k < e->cb.n ? xy(e->cb.cab[k].kx, e->cb.cab[k].ky) : -1;
    case CBT_X_XY: { RECT l, x; return label_rects(e, &l, &x) ? xy((x.left + x.right) / 2.0f, (x.top + x.bottom) / 2.0f) : -1; }
    case CBT_SEL: return e->cb.sel;
    case CBT_BTN_XY: { RECT r = cb_button_rect(); return e->screen == SCR_MATRIX ? xy((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f) : -1; }
    case CBT_CTL_XY: {
        if (k < 0 || k >= NPARAMS || IsRectEmpty(&L.ctl[k])) return -1;
        float cx, cy, r;
        if (L.kind[k] == K_KNOB || L.kind[k] == K_SMALL || L.kind[k] == K_BIG) { knob_geom(k, &cx, &cy, &r, NULL); return xy(cx, cy); }
        return xy((L.ctl[k].left + L.ctl[k].right) / 2.0f, (L.ctl[k].top + L.ctl[k].bottom) / 2.0f);
    }
    case CBT_BUILDS: return e->cb.builds;
    case CBT_JACK_XY: { int j = jack_of_src(e, k); return j >= 0 ? xy(e->cb.jack[j].x, e->cb.jack[j].y) : -1; }
    case CBT_WIRE_XY: return k >= 0 && k < e->cb.n ? xy(e->cb.cab[k].pt[CB_PTS / 4][0], e->cb.cab[k].pt[CB_PTS / 4][1]) : -1;
    case CBT_OFF_XY: { RECT o = off_rect(); return e->cb.on ? xy((o.left + o.right) / 2.0f, (o.top + o.bottom) / 2.0f) : -1; }
    case CBT_HANDLE_XY: { int c = k / 2; return c >= 0 && c < e->cb.n ? xy(e->cb.cab[c].hx[k % 2], e->cb.cab[c].hy[k % 2]) : -1; }
    case CBT_BOX_XY: {
        box_t b;
        if (!box_geom(e, &b) || k < 0 || k >= CBX_COUNT || IsRectEmpty(&b.it[k])) return -1;
        return xy((b.it[k].left + b.it[k].right) / 2.0f, (b.it[k].top + b.it[k].bottom) / 2.0f);
    }
    case CBT_DRAG: return e->cb.drag;
    case CBT_LIVE: return rw(&e->cb.live_at) | (intptr_t)rh(&e->cb.live_at) << 16;
    case CBT_DJACK_XY:
        for (int j = 0; j < e->cb.nj; j++)
            if (!e->cb.jack[j].src && e->cb.jack[j].dst == k) return xy(e->cb.jack[j].x, e->cb.jack[j].y);
        return -1;
    }
    return 0;
}
