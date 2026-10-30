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
 *
 * Drawing: the dimming veil, the bay, the jacks and the cables are drawn into a cached layer (premultiplied ARGB, the
 * window's size, anti-aliased with GDI+) only when the rows, the Synth in focus or the window's size change; each paint
 * blends the part it repaints (skin_layer_blend) and draws the knobs, the labels and the selected cable on top. A knob
 * dragged repaints only itself, its label and the help line. UI thread only; the audio thread sees only setParameter,
 * as from a host. */
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
}

/* the cables and jacks for the rows now (the Synth in focus) */
static void geom(ed_t *e) {
    cb_ui_t *b = &e->cb;
    b->n = b->nj = 0;
    for (int k = 0; k < NBAY; k++) add_jack(e, BAY[k], 0, bay_x(k), bay_y(), qm_src_name(BAY[k]));
    int c = e->focus, gens = 0, dests = 0;
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
        for (int k = 0; k < b->n; k++) if (!b->cab[k].on) draw_cable(&b->cab[k], 0);   /* (the faint ones under) */
        for (int k = 0; k < b->n; k++) if (b->cab[k].on) draw_cable(&b->cab[k], 0);
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
static int label_rects(ed_t *e, RECT *lab, RECT *x) {   /* 0: nothing selected */
    int k = e->cb.sel >= 0 ? cable_of_row(e, e->cb.sel) : -1;
    if (k < 0) return 0;
    const cb_cable_t *c = &e->cb.cab[k];
    char t[200];
    label_text(e, c, t, sizeof t);
    int w = text_w(t, font_cell) + sc(14) + sc(20), h = sc(18);
    int left = (int)(c->kx + sc(16));
    if (left + w > L.w - sc(30)) left = (int)(c->kx - sc(16)) - w;
    if (left < sc(4)) left = sc(4);
    int top = (int)(c->ky - h / 2);
    if (top < sc(2)) top = sc(2);
    if (top + h > L.h - sc(2)) top = L.h - sc(2) - h;
    SetRect(lab, left, top, left + w, top + h);
    SetRect(x, left + w - sc(19), top + sc(2), left + w - sc(2), top + h - sc(2));
    return 1;
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
        if (!seen(c->kx - 12 * s, c->ky - 12 * s, c->kx + 12 * s, c->ky + 12 * s, clip)) continue;
        float amt = real_at(e, cell(c->row, MX_AMT, e->focus));
        b->cab[k].amt = amt;
        amount_knob(c, amt, e->hover == Z_CB_KNOB + k || e->cb.sel == c->row);
    }
    RECT lab, x;
    if (label_rects(e, &lab, &x) && seen((float)lab.left, (float)lab.top, (float)lab.right, (float)lab.bottom, clip)) {
        char t[200];
        label_text(e, &b->cab[sk], t, sizeof t);
        gfx_rect((float)lab.left, (float)lab.top, (float)rw(&lab), (float)rh(&lab), 0xF40B1621);
        gfx_frame((float)lab.left, (float)lab.top, (float)rw(&lab), (float)rh(&lab), 1.0f, fade(qm_family(b->cab[sk].src), 0xFF));
        RECT tr = {lab.left + sc(6), lab.top, x.left - sc(4), lab.bottom};
        ltext(&tr, t, font_cell, e->mode == M_CABLE ? RGB(255, 255, 255) : RGB(150, 215, 232), DT_LEFT);
        int hot = e->hover == Z_CB_X;
        gfx_rect((float)x.left, (float)x.top, (float)rw(&x), (float)rh(&x), hot ? 0xFFB03A30 : 0xFF6A2420);
        float cx = (x.left + x.right) / 2.0f, cy = (x.top + x.bottom) / 2.0f, d = 3.5f * s;
        gfx_line(cx - d, cy - d, cx + d, cy + d, 1.6f * s, 0xFFFFFFFF);
        gfx_line(cx - d, cy + d, cx + d, cy - d, 1.6f * s, 0xFFFFFFFF);
        b->label_at = lab;
        InflateRect(&b->label_at, sc(2), sc(2));
    }
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
    RECT bt = cb_button_rect();
    if (e->screen == SCR_MATRIX && PtInRect(&bt, pt)) return Z_CB_BTN;
    if (!e->cb.on) return -1;
    refresh(e);
    float x = (float)pt.x, y = (float)pt.y, s = L.s;
    RECT lab, xb, off = off_rect();
    if (label_rects(e, &lab, &xb)) {
        if (PtInRect(&xb, pt)) return Z_CB_X;
        if (PtInRect(&lab, pt)) return Z_CB_WIRE + cable_of_row(e, e->cb.sel);
    }
    if (PtInRect(&off, pt)) return Z_CB_OFF;
    for (int k = e->cb.n - 1; k >= 0; k--)
        if (hypotf(e->cb.cab[k].kx - x, e->cb.cab[k].ky - y) < 11 * s) return Z_CB_KNOB + k;
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
static void drag_start(ed_t *e, int k, int y) {
    const cb_cable_t *c = &e->cb.cab[k];
    select_row(e, c->row);
    for (int s = 0; s < NSYNTHS; s++) {   /* the same connection in the Synths edited together */
        e->cb.rows[s] = -1;
        if (s == e->focus) { e->cb.rows[s] = c->row; continue; }
        if (!(e->focus_mask >> s & 1u)) continue;
        for (int m = 0; m < NMATRIX && e->cb.rows[s] < 0; m++)
            if (row_src(e, m, s) == c->src && row_dst(e, m, s) == c->dst) e->cb.rows[s] = m;
    }
    char what[80];
    snprintf(what, sizeof what, "Cable amount %s > %s", qm_src_name(c->src), qm_dst_name(c->dst));
    lib_hold(e, 1);   /* the drag: one undo step */
    lib_touch(e, what);
    for (int s = 0; s < NSYNTHS; s++)
        if (e->cb.rows[s] >= 0) edit_begin(e, cell(e->cb.rows[s], MX_AMT, s), 0);
    e->cb.drag_y = y;
    e->cb.drag_from = real_at(e, cell(c->row, MX_AMT, e->focus));
    e->mode = M_CABLE;
    SetCapture(e->wnd);
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
int cb_press(ed_t *e, int z, int x, int y) {
    (void)x;
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
    if (z >= Z_CB_KNOB && z < Z_CB_KNOB + e->cb.n) { drag_start(e, z - Z_CB_KNOB, y); return 1; }
    if (z >= Z_CB_WIRE && z < Z_CB_WIRE + e->cb.n) {   /* select it (again: not) */
        int row = e->cb.cab[z - Z_CB_WIRE].row;
        select_row(e, e->cb.sel == row ? -1 : row);
        return 1;
    }
    if (z >= Z_CB_JACK && z < Z_CB_JACK + e->cb.nj) {
        const cb_jack_t *j = &e->cb.jack[z - Z_CB_JACK];
        if (j->src > 0) qm_pick(e, j->src);   /* quick assign from it: then a knob */
        else
            for (int k = 0; k < e->cb.n; k++)   /* a destination's jack: its (first) cable */
                if (e->cb.cab[k].dst == j->dst) { select_row(e, e->cb.cab[k].row); break; }
        return 1;
    }
    if (e->cb.sel >= 0) select_row(e, -1);   /* a click elsewhere: the panel's, and the selection goes */
    return 0;
}
void cb_drag(ed_t *e, int y, int fine) {
    float a = e->cb.drag_from + (e->cb.drag_y - y) * (fine ? 0.2f : 1.0f);
    if (!fine) a = roundf(a);
    for (int s = 0; s < NSYNTHS; s++) {
        int m = e->cb.rows[s];
        if (m >= 0) put(e, cell(m, MX_AMT, s), amt_norm(cell(m, MX_AMT, s), a));
    }
    int k = cable_of_row(e, e->cb.sel);
    if (k >= 0) inval_knob(e, k);
    inval_label(e);
}
void cb_release(ed_t *e) {
    for (int s = 0; s < NSYNTHS; s++)
        if (e->cb.rows[s] >= 0) edit_begin(e, cell(e->cb.rows[s], MX_AMT, s), 1);
    e->mode = M_NONE;
    lib_hold(e, -1);   /* the undo step */
    int k = cable_of_row(e, e->cb.sel);
    if (k >= 0) inval_knob(e, k);
    inval_label(e);
}
int cb_key(ed_t *e, int vk) {
    if (vk != VK_ESCAPE || !e->cb.on || e->mode == M_CABLE) return 0;
    cb_toggle(e);
    return 1;
}
void cb_tick(ed_t *e) {   /* a row changed elsewhere (the MATRIX page, the host, undo): the cables again */
    if (!e->cb.on || e->mode == M_CABLE) return;
    refresh(e);
    for (int k = 0; k < e->cb.n; k++) {   /* an amount moved: just its knob (and the label) */
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
    else if (z >= Z_CB_JACK && z < Z_CB_JACK + e->cb.nj) {
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
int cb_help(ed_t *e, char *out, int n) {
    int z = e->hover;
    if (z == Z_CB_BTN) {
        snprintf(out, n, "CABLES: %s the matrix as patch cables over the panel: each active row of the Synth in focus, "
                 "from its source to the knob it moves, with an amount knob in the middle (drag it). Click a cable for "
                 "its row and an X to remove it. Click again, CABLES OFF or Esc: hidden.", e->cb.on ? "hides" : "shows");
        return 1;
    }
    if (!e->cb.on) return 0;
    int in = z >= Z_CB && z < Z_CB_END;
    if (e->qm.src && !in && e->mode != M_CABLE) return 0;   /* (quick assign's own help while it waits for a knob) */
    char t[200] = "";
    int k = z >= Z_CB_KNOB && z < Z_CB_KNOB + e->cb.n ? z - Z_CB_KNOB : z >= Z_CB_WIRE && z < Z_CB_WIRE + e->cb.n ? z - Z_CB_WIRE : -1;
    if (e->mode == M_CABLE && e->cb.sel >= 0 && cable_of_row(e, e->cb.sel) >= 0) {
        const cb_cable_t *c = &e->cb.cab[cable_of_row(e, e->cb.sel)];
        snprintf(out, n, "%s > %s:  %+.1f%%   (Synth %d row %d)\nDrag up for a positive amount (green), down for a negative "
                 "one (red); Shift: fine. Let go: one undo step.", qm_src_name(c->src), qm_dst_name(c->dst),
                 real_at(e, cell(c->row, MX_AMT, e->focus)), e->focus + 1, c->row + 1);
        return 1;
    }
    if (k >= 0) {
        label_text(e, &e->cb.cab[k], t, sizeof t);
        snprintf(out, n, "Row %s\n%s", t, z >= Z_CB_KNOB && z < Z_CB_KNOB + CB_MAX
                 ? "Drag this knob up or down for the amount (green: positive, red: negative; Shift: fine). A click "
                   "selects the cable: its X removes the row."
                 : "Click to select this cable (again: not); its label's X removes the row. Its knob sets the amount.");
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
        if (j->dst) snprintf(out, n, "%s: a destination with no knob on the panel. Click: select its cable.", j->label);
        else snprintf(out, n, "%s: a source. Click it, then click a knob to connect it (drag for the amount): a new cable. "
                      "DONE on the screen (or Esc) when finished.", j->label);
        return 1;
    }
    if (z < 0 || z == Z_CB_BAY) {
        snprintf(out, n, "CABLES: Synth %d's %d connection%s. Drag a cable's knob for its amount; click a cable to see its "
                 "row (X removes it). New ones: click a source's jack (or a DEST button), then a knob. Esc or CABLES OFF: "
                 "hidden.", e->focus + 1, e->cb.n, e->cb.n == 1 ? "" : "s");
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
    }
    return 0;
}
