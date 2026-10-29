/* The MORE pages' pictures on the desktop editor's centre screen (see editor.c), drawn under a page's items in the
 * screen's own colours: an envelope's shape with its curves (the three envelope pages), an LFO's wave with its
 * variation, polarity, start phase and smoothing (the LFO pages), the two filters' routing, modes and response curves
 * (FILTERS), the mixer's levels as a bar graph with the filters each source goes to (MIXER), and the box with the
 * bouncing balls and particles moving in it (BOUNCING BALLS, PARTICLES; 2026-10-29: the one moving picture, read from
 * the engine's "phys_view" and repainted by pics_tick while its page shows).
 *
 * Every picture is worked out from the focused Synth's params each time the page is painted, with the engine's own
 * shapes where they are in headers (src/env.h's stage curve, src/lfo.h's waves), so it shows what plays. A page is
 * repainted when one of its items changes (editor.c on_screen), and each picture reads only its page's items.
 * UI thread only. */
#include <complex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#pragma GCC diagnostic push   /* (the pictures use only some of gfx.h's shapes) */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "gfx.h"
#pragma GCC diagnostic pop
#include "skin.h"
#include "../src/env.h"
#include "../src/lfo.h"
#include "hostrate.h"
#pragma GCC diagnostic push   /* (only its sizes: the box's line, emitter and ball radius) */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "../src/physgen.h"
#pragma GCC diagnostic pop

#define T_LCD RGB(150, 215, 232)       /* as editor_draw.c */
#define T_LCD_DIM RGB(90, 130, 145)
#define C_RULE 0xFF27404E             /* frames and rules */
#define C_GRID 0x2896D7E8
#define C_GLOW 0x3887DFFF             /* the curves: a glow under a bright line, as the oscillators' wave screens */
#define C_TRACE 0xFFA8ECFF
#define C_FILL 0x1C87DFFF
#define C_LIT 0xFF96D7E8
#define C_DIM 0xFF1C2C36

static void txt(HDC dc, RECT r, const char *s, COLORREF c, UINT fmt) {
    fmt |= DT_SINGLELINE | DT_NOPREFIX;
    if (skin_text(&r, s, font_tiny, c, fmt)) return;
    SelectObject(dc, font_tiny);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt);
}

static int ix(const char *pre, const char *suffix) {   /* the panel index of key pre + suffix, -1 if none */
    char k[48];
    snprintf(k, sizeof k, "%s%s", pre, suffix);
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static float val(ed_t *e, int i, float dflt) { return i >= 0 ? real_of(e, i) : dflt; }   /* in its own units */
static int opt(ed_t *e, int i) { return i >= 0 ? option_of(e, i) : 0; }
static const char *opt_name(ed_t *e, int i) { return i >= 0 && PARAMS[i].nopts ? PARAMS[i].opts[option_of(e, i)] : ""; }

static void trace(const gpt_t *p, int n) {
    gfx_lines(p, n, 3.6f * L.s, C_GLOW);
    gfx_lines(p, n, 1.3f * L.s, C_TRACE);
}
static void dotted_v(float x, float y0, float y1) {
    for (float y = y0; y < y1; y += 4.0f * L.s) gfx_line(x, y, x, fminf(y + 1.5f * L.s, y1), 1.0f, C_GRID);
}

/* ---- an envelope: delay, attack, hold, decay, sustain, release. Each stage as wide as its time knob turns (the
 * knobs are logarithmic, so short and long stages both show), the sustain a fixed plateau (none with LOOP: the decay
 * goes straight to the release), each timed stage bent by its curve exactly as env.h bends it ---- */
static float stage_u(float x, float bend) {   /* env.h: progress through a stage bent by -1 (slow start) .. +1 */
    double tau = (double)bend * ENV_TAU_MAX;
    return fabs(tau) < 1e-6 ? x : (float)(expm1(-tau * x) / expm1(-tau));
}

static void pic_env(HDC dc, ed_t *e, const char *p, RECT a) {
    float s = val(e, ix(p, "s"), 0.0f) / 100.0f;
    float bend[3] = {-val(e, ix(p, "acurve"), 0.0f) / 100.0f, val(e, ix(p, "dcurve"), 50.0f) / 100.0f,
                     val(e, ix(p, "rcurve"), 50.0f) / 100.0f};
    int loop = opt(e, ix(p, "loop")), latch = opt(e, ix(p, "latch")), sync = opt(e, ix(p, "sync"));
    float dly = val(e, ix(p, "delay"), 0.0f), hold = val(e, ix(p, "hold"), 0.0f);
    float w[6] = {dly > 0.0f ? 0.04f + dly / 100.0f : 0.0f, 0.04f + val(e, ix(p, "a"), 0.0f) / 100.0f,
                  hold > 0.0f ? 0.04f + hold / 100.0f : 0.0f, 0.04f + val(e, ix(p, "d"), 50.0f) / 100.0f,
                  loop ? 0.0f : 0.45f, 0.04f + val(e, ix(p, "r"), 50.0f) / 100.0f};
    static const char *const NAME[6] = {"DLY", "A", "H", "D", "S", "R"};
    float total = 0.0f;
    for (int k = 0; k < 6; k++) total += w[k];
    float x0 = a.left + sc(6), x1 = a.right - sc(6), top = a.top + sc(5), bot = a.bottom - sc(15);
    float hgt = bot - top, px = (x1 - x0) / total;
    gfx_line(x0, bot, x1, bot, 1.0f, C_RULE);
    gfx_line(x0, top, x1, top, 1.0f, C_GRID);
    enum { N = 48 };
    gpt_t pts[6 * N + 2];
    int n = 0;
    float x = x0;
    for (int k = 0; k < 6; k++) {
        float wk = w[k] * px;
        if (wk <= 0.0f) continue;
        for (int j = (n == 0 ? 0 : 1); j <= N; j++) {
            float t = (float)j / N, lv;
            switch (k) {
            case 0: lv = 0.0f; break;
            case 1: lv = stage_u(t, bend[0]); break;
            case 2: lv = 1.0f; break;
            case 3: lv = 1.0f - stage_u(t, bend[1]) * (1.0f - s); break;
            case 4: lv = s; break;
            default: lv = s * (1.0f - stage_u(t, bend[2])); break;
            }
            pts[n].x = x + wk * t;
            pts[n].y = bot - lv * hgt;
            n++;
        }
        RECT lr = {(int)x, (int)bot + sc(1), (int)(x + wk), a.bottom};
        if (wk >= sc(9)) txt(dc, lr, NAME[k], T_LCD_DIM, DT_CENTER | DT_VCENTER);
        x += wk;
        if (k < 5 && x < x1 - 1.0f) dotted_v(x, top, bot);
    }
    pts[n].x = pts[n - 1].x; pts[n].y = bot;   /* the fill's closing corners */
    pts[n + 1].x = x0; pts[n + 1].y = bot;
    gfx_poly(pts, n + 2, C_FILL);
    trace(pts, n);
    char tags[48] = "";
    snprintf(tags, sizeof tags, "%s%s%s", loop ? "LOOP  " : "", latch ? "LATCH  " : "", sync ? "SYNC" : "");
    RECT tr = {a.left, (int)bot + sc(1), a.right - sc(6), a.bottom};
    if (tags[0]) txt(dc, tr, tags, T_LCD, DT_RIGHT | DT_VCENTER);
}

/* ---- an LFO: two cycles from its start phase, its wave and variation through lfo.h's own lfo_shape, unipolar or
 * bipolar (the zero line), the smoothing as the engine's one-pole at this rate (synced rates as at 120 BPM) ---- */
static void pic_lfo(HDC dc, ed_t *e, const char *p, RECT a) {
    int wi = ix(p, "wave"), wave = opt(e, wi), uni = opt(e, ix(p, "polarity")) == 1;
    float var = val(e, ix(p, "var"), 0.0f) / 100.0f, ph = val(e, ix(p, "phase"), 0.0f) / 100.0f;
    float sm = val(e, ix(p, "smooth"), 0.0f) / 100.0f;
    char rate[64] = "";
    int ri = ix(p, "rate");
    if (ri >= 0) param_text(e, ri, rate, sizeof rate);
    float hz = 2.0f;   /* the rate, for the smoothing only */
    int d;
    if (strstr(rate, "Hz")) hz = (float)atof(rate);
    else if (sscanf(rate, "1/%d", &d) == 1 && d > 0) hz = 2.0f * d / 4.0f;   /* a note value at 120 BPM */
    else if (strstr(rate, "bar")) hz = 0.5f / (4.0f * (float)fmax(atof(rate), 1.0));
    enum { PER = 120, CYC = 2 };
    float coef = 1.0f;
    if (sm > 0.0f && hz > 0.0f)   /* engine.c apply_lfos: 1 kHz at 1% down to 0.1 Hz at 100%, per picture step */
        coef = 1.0f - expf(-6.2831853f * 1000.0f * powf(10.0f, -4.0f * sm) / (hz * PER));
    lfo_t l;
    lfo_init(&l, 0x2545F491u);
    lfo_reset(&l, ph);
    for (int j = 0; j < 3 * PER; j++) {   /* three cycles first: the random values and the smoothing settle */
        lfo_smooth(&l, lfo_shape(&l, wave, var), coef);
        lfo_advance(&l, 1.0f / PER);
    }
    lfo_reset(&l, ph);
    float x0 = a.left + sc(6), x1 = a.right - sc(6), top = a.top + sc(5), bot = a.bottom - sc(15);
    float zero = uni ? bot : (top + bot) / 2;
    gfx_line(x0, zero, x1, zero, 1.0f, C_RULE);
    gfx_line(x0, top, x1, top, 1.0f, C_GRID);
    if (!uni) gfx_line(x0, bot, x1, bot, 1.0f, C_GRID);
    dotted_v((x0 + x1) / 2, top, bot);   /* where the second cycle starts */
    gpt_t pts[CYC * PER + 1];
    for (int j = 0; j <= CYC * PER; j++) {
        float y = lfo_smooth(&l, lfo_shape(&l, wave, var), coef);
        if (j < CYC * PER) lfo_advance(&l, 1.0f / PER);
        y = y < -1.0f ? -1.0f : y > 1.0f ? 1.0f : y;
        pts[j].x = x0 + (x1 - x0) * j / (CYC * PER);
        pts[j].y = bot - (y + 1.0f) * 0.5f * (bot - top);
    }
    trace(pts, CYC * PER + 1);
    char t[96];
    RECT lr = {(int)x0, (int)bot + sc(1), (int)((x0 + x1) / 2), a.bottom};
    if (var > 0.0f) snprintf(t, sizeof t, "%s, variation %.0f%%", opt_name(e, wi), var * 100.0f);
    else snprintf(t, sizeof t, "%s", opt_name(e, wi));
    txt(dc, lr, t, T_LCD_DIM, DT_LEFT | DT_VCENTER);
    RECT rr = {(int)((x0 + x1) / 2), (int)bot + sc(1), (int)x1, a.bottom};
    snprintf(t, sizeof t, "%s   %s   start %.0f%%", rate, uni ? "UNIPOLAR" : "BIPOLAR", ph * 100.0f);
    txt(dc, rr, t, T_LCD, DT_RIGHT | DT_VCENTER);
}

/* ---- the filters: each as a box with its mode and its response (magnitude over 20 Hz - 20 kHz, -36 .. +18 dB),
 * fed by the mixer (IN), into the MIX between them; in series the SVF also feeds the ladder ---- */
static float complex svf2(float x, float q, int mode) {   /* one SVF element at f / cutoff = x */
    float complex s = I * x, den = 1.0f + s / q + s * s;
    switch (mode) {
    case 1: return (s / q) / den;           /* BP */
    case 2: return s * s / den;             /* HP */
    case 3: return (1.0f + s * s) / den;    /* notch */
    default: return 1.0f / den;             /* LP */
    }
}
static float svf_db(ed_t *e, float f) {
    float fc = 20.0f * powf(1000.0f, val(e, ix("svf_", "cut"), 100.0f) / 100.0f);
    float q = 0.5f * powf(20.0f, val(e, ix("svf_", "res"), 0.0f) / 100.0f);   /* up to about +20 dB of peak */
    float fb = fc * exp2f(4.0f * val(e, ix("svf_", "space"), 0.0f) / 100.0f);   /* element B: +-4 octaves */
    int mode = opt(e, ix("svf_", "mode")), series = !strcmp(opt_name(e, ix("svf_", "pair")), "Series");
    float complex h = series ? svf2(f / fc, sqrtf(q), mode) * svf2(f / fb, sqrtf(q), mode)
                             : 0.5f * (svf2(f / fc, q, mode) + svf2(f / fb, q, mode));
    return 20.0f * log10f(cabsf(h) + 1e-6f);
}
static float lad_db(ed_t *e, float f) {
    float fc = 20.0f * powf(1000.0f, val(e, ix("flt_", "cut"), 100.0f) / 100.0f);
    float k = 3.8f * val(e, ix("flt_", "res"), 0.0f) / 100.0f;
    int poles = 4 - opt(e, ix("flt_", "slope")), hp = opt(e, ix("flt_", "mode")) == 1;   /* 24, 18, 12, 6 dB */
    float x = hp ? fc / f : f / fc;   /* (high-pass: the low-pass mirrored about the cutoff) */
    float complex p = 1.0f / (1.0f + I * x), p4 = p * p * p * p, tap = 1.0f;
    for (int m = 0; m < poles; m++) tap *= p;
    return 20.0f * log10f(cabsf(tap / (1.0f + k * p4)) + 1e-6f);
}

static void filter_box(HDC dc, ed_t *e, RECT b, int ladder) {
    gfx_frame((float)b.left, (float)b.top, (float)rw(&b), (float)rh(&b), 1.0f, 0xFF3E6273);
    char t[48];
    if (ladder) snprintf(t, sizeof t, "LADDER %s %ddB", opt_name(e, ix("flt_", "mode")), atoi(opt_name(e, ix("flt_", "slope"))));
    else snprintf(t, sizeof t, "SVF %s %ddB", opt_name(e, ix("svf_", "mode")),
                  !strcmp(opt_name(e, ix("svf_", "pair")), "Series") ? 24 : 12);   /* (the slope in dB) */
    RECT lr = {b.left + sc(4), b.top, b.left + sc(92), b.bottom};
    txt(dc, lr, t, T_LCD, DT_LEFT | DT_VCENTER);
    float x0 = b.left + sc(94), x1 = b.right - sc(3), y0 = b.top + sc(2), y1 = b.bottom - sc(2);
    enum { N = 96 };
    gpt_t pts[N + 1];
    for (int j = 0; j <= N; j++) {
        float f = 20.0f * powf(1000.0f, (float)j / N), db = ladder ? lad_db(e, f) : svf_db(e, f);
        db = db < -36.0f ? -36.0f : db > 18.0f ? 18.0f : db;
        pts[j].x = x0 + (x1 - x0) * j / N;
        pts[j].y = y0 + (18.0f - db) / 54.0f * (y1 - y0);
    }
    gfx_line(x0, y0 + 18.0f / 54.0f * (y1 - y0), x1, y0 + 18.0f / 54.0f * (y1 - y0), 1.0f, C_GRID);   /* 0 dB */
    trace(pts, N + 1);
}

static void arrow(float x0, float y0, float x1, float y1, DWORD c) {   /* a line ending in a small arrowhead */
    gfx_line(x0, y0, x1, y1, 1.2f, c);
    float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy), h = 3.5f * L.s;
    if (d < 1.0f) return;
    dx /= d; dy /= d;
    gpt_t t[3] = {{x1, y1}, {x1 - h * dx - h * 0.6f * dy, y1 - h * dy + h * 0.6f * dx},
                  {x1 - h * dx + h * 0.6f * dy, y1 - h * dy - h * 0.6f * dx}};
    gfx_poly(t, 3, c);
}

static void pic_filters(HDC dc, ed_t *e, RECT a) {
    int series = opt(e, ix("flt_", "route")) == 1, link = opt(e, ix("flt_", "link")) == 1;
    float mix = val(e, ix("flt_", "mix"), 100.0f) / 100.0f;
    int bx = a.left + sc(28), bw = sc(226), gap = sc(10), bh = (rh(&a) - gap - sc(4)) / 2;
    RECT top = {bx, a.top + sc(2), bx + bw, a.top + sc(2) + bh}, low = {bx, top.bottom + gap, bx + bw, top.bottom + gap + bh};
    float ym = (a.top + a.bottom) / 2.0f, ys = (top.top + top.bottom) / 2.0f, yl = (low.top + low.bottom) / 2.0f;
    RECT in = {a.left, (int)ym - sc(7), a.left + sc(16), (int)ym + sc(7)};
    txt(dc, in, "IN", T_LCD_DIM, DT_LEFT | DT_VCENTER);
    float xs = a.left + sc(17), xf = bx - sc(8);   /* the input splits to both filters (the mixer's SVF / LADDER) */
    gfx_line(xs, ym, xf, ym, 1.2f, C_LIT);
    gfx_line(xf, ys, xf, yl, 1.2f, C_LIT);
    arrow(xf, ys, (float)bx, ys, C_LIT);
    arrow(xf, yl, (float)bx, yl, C_LIT);
    filter_box(dc, e, top, 0);
    filter_box(dc, e, low, 1);
    if (series) {   /* the SVF's output into the ladder too */
        float x = bx + bw - sc(30);
        arrow(x, (float)top.bottom, x, (float)low.top, C_TRACE);
    }
    float nx = bx + bw + sc(22), r = 6.0f * L.s;   /* the MIX: each line as bright as its share */
    DWORD cs = (DWORD)(0x40 + 0xBF * (1.0f - mix)) << 24 | 0x96D7E8, cl = (DWORD)(0x40 + 0xBF * mix) << 24 | 0x96D7E8;
    gfx_line((float)(bx + bw), ys, nx - r * 0.7f, ys, 1.2f, cs);
    gfx_line(nx - r * 0.7f, ys, nx, ym - r, 1.2f, cs);
    gfx_line((float)(bx + bw), yl, nx - r * 0.7f, yl, 1.2f, cl);
    gfx_line(nx - r * 0.7f, yl, nx, ym + r, 1.2f, cl);
    gfx_circle(nx, ym, r, 1.2f, C_LIT);
    gfx_line(nx - r * 0.5f, ym, nx + r * 0.5f, ym, 1.2f, C_LIT);
    gfx_line(nx, ym - r * 0.5f, nx, ym + r * 0.5f, 1.2f, C_LIT);
    char t[32];
    int tx = (int)(nx + r + sc(6));
    RECT r1 = {tx, top.top, a.right, top.bottom}, r2 = {tx, (int)ym - sc(8), a.right, (int)ym + sc(8)};
    RECT r3 = {tx, low.top, a.right, low.bottom};
    txt(dc, r1, series ? "SERIES" : "PARALLEL", T_LCD, DT_LEFT | DT_VCENTER);
    snprintf(t, sizeof t, "MIX %.0f%%", mix * 100.0f);
    txt(dc, r2, t, T_LCD, DT_LEFT | DT_VCENTER);
    if (link) txt(dc, r3, "LINKED", T_LCD, DT_LEFT | DT_VCENTER);
}

/* ---- the mixer: a bar per source, as tall as its level, its left half lit when it goes to the SVF and its right
 * half when it goes to the ladder (neither: not heard) ---- */
static void pic_mixer(HDC dc, ed_t *e, RECT a) {
    static const struct { const char *pre, *name; } CH[] = {
        {"o1_", "OSC 1"}, {"o2_", "OSC 2"}, {"o3_", "OSC 3"}, {"mac_", "MACRO"}, {"ring_", "RING"}, {"noise_", "NOISE"}};
    int n = 0, have[6];
    for (int c = 0; c < 6; c++)
        if ((have[c] = ix(CH[c].pre, "level") >= 0)) n++;
    if (!n) return;
    float x0 = a.left + sc(6), cw = (a.right - sc(6) - x0) / n, top = a.top + sc(4), bot = a.bottom - sc(15);
    gfx_line(x0, bot, a.right - sc(6.0f), bot, 1.0f, C_RULE);
    for (int c = 0, k = 0; c < 6; c++) {
        if (!have[c]) continue;
        float lv = val(e, ix(CH[c].pre, "level"), 0.0f) / 100.0f, cx = x0 + cw * (k++ + 0.5f), bw = cw * 0.22f;
        int svf = opt(e, ix(CH[c].pre, "svf")), lad = opt(e, ix(CH[c].pre, "lad")), h = (int)((bot - top) * lv);
        gfx_rect(cx - bw, top, 2 * bw, bot - top, C_DIM);   /* the track */
        if (h > 0) {
            gfx_rect(cx - bw, bot - h, bw - 0.5f, (float)h, svf ? C_LIT : 0xFF3E6273);
            gfx_rect(cx + 0.5f, bot - h, bw - 0.5f, (float)h, lad ? 0xFFE8F6FA : 0xFF3E6273);
        }
        char t[24];
        snprintf(t, sizeof t, "%s  %s%s", CH[c].name, svf ? "S" : "-", lad ? "L" : "-");
        RECT lr = {(int)(cx - cw / 2), (int)bot + sc(1), (int)(cx + cw / 2), a.bottom};
        txt(dc, lr, t, svf || lad ? T_LCD : T_LCD_DIM, DT_CENTER | DT_VCENTER);
    }
}

/* ---- the bouncing balls and particles (2026-10-29): the box as the engine has it now (the "phys_view" readout,
 * read without a lock: hostrate.c hr_peek_view), the walls and the LINE lit as they play, each ball (lit when it just
 * played), the emitter and the particles; beside it what plays. Repainted by pics_tick while the page shows ---- */
static void pic_phys(HDC dc, ed_t *e, RECT a) {
    char v[1024] = "";
    int nb = 0, np = 0, wall = 0, used = 0;
    if (e->hr && hr_peek_view(e->hr, e->focus, v, sizeof v) > 0) sscanf(v, "%d %d %d;%n", &nb, &np, &wall, &used);
    int parts = opt(e, ix("phys_", "parts_on")), balls = opt(e, ix("phys_", "balls_on"));
    int line = parts && opt(e, ix("phys_", "trigger")) == PHT_LINE;
    float side = (float)(a.bottom - a.top) - sc(6.0f), x0 = a.left + sc(8.0f), y0 = a.top + sc(3.0f);
    gfx_rect(x0, y0, side, side, C_DIM);
    for (float g = 0.25f; g < 1.0f; g += 0.25f) {   /* a faint grid */
        gfx_line(x0 + g * side, y0, x0 + g * side, y0 + side, 1.0f, C_GRID);
        gfx_line(x0, y0 + g * side, x0 + side, y0 + g * side, 1.0f, C_GRID);
    }
    gfx_frame(x0, y0, side, side, 1.0f, C_RULE);
    static const float W[4][4] = {{0, 1, 1, 1}, {0, 0, 1, 0}, {0, 0, 0, 1}, {1, 0, 1, 1}};   /* floor, ceiling, left, right */
    for (int k = 0; k < 4; k++)
        if (wall >> k & 1)
            gfx_line(x0 + W[k][0] * side, y0 + W[k][1] * side, x0 + W[k][2] * side, y0 + W[k][3] * side, 2.5f * L.s, C_TRACE);
    if (line) {
        float ly = y0 + (1.0f - PH_LINE_Y) * side;
        if (wall >> 4 & 1) gfx_line(x0, ly, x0 + side, ly, 2.0f * L.s, C_TRACE);
        else for (float x = x0; x < x0 + side; x += 5.0f * L.s) gfx_line(x, ly, fminf(x + 2.5f * L.s, x0 + side), ly, 1.0f, C_LIT);
    }
    if (parts) gfx_circle(x0 + PH_EMIT_X * side, y0 + (1.0f - PH_EMIT_Y) * side, 3.0f * L.s, 1.0f, C_LIT);
    const char *p = v + used;
    for (int i = 0; i < nb + np; i++) {
        int x, y, f, u;
        if (sscanf(p, "%d,%d,%d;%n", &x, &y, &f, &u) != 3) break;
        p += u;
        float cx = x0 + x * 0.001f * side, cy = y0 + (1.0f - y * 0.001f) * side;
        float r = i < nb ? fmaxf(PH_BALL_R * side, 2.5f * L.s) : 1.6f * L.s;
        if (f) gfx_disc(cx, cy, r * 2.0f, C_GLOW);
        gfx_disc(cx, cy, r, f ? C_TRACE : i < nb ? C_LIT : 0xFF6FA9BC);
    }
    char t[96];
    RECT lr = {(int)(x0 + side + sc(12.0f)), a.top + sc(2), a.right - sc(4), a.top + sc(16)};
    static const char *const PLAY[] = {"crossing the line", "colliding", "dying"};
    if (!balls && !parts) snprintf(t, sizeof t, "Off: switch BALLS or PARTICLES on");
    else snprintf(t, sizeof t, "%s%s%s", balls ? "BALLS" : "", balls && parts ? " + " : "", parts ? "PARTICLES" : "");
    txt(dc, lr, t, T_LCD, DT_LEFT | DT_VCENTER);
    OffsetRect(&lr, 0, sc(15));
    if (balls) {
        snprintf(t, sizeof t, "%d ball%s: walls play the key", nb, nb == 1 ? "" : "s");
        txt(dc, lr, t, T_LCD_DIM, DT_LEFT | DT_VCENTER);
        OffsetRect(&lr, 0, sc(15));
    }
    if (parts) {
        snprintf(t, sizeof t, "%d particle%s, playing %s", np, np == 1 ? "" : "s", PLAY[opt(e, ix("phys_", "trigger")) % 3]);
        txt(dc, lr, t, T_LCD_DIM, DT_LEFT | DT_VCENTER);
        OffsetRect(&lr, 0, sc(15));
    }
    snprintf(t, sizeof t, "Key %s %s, %s", opt_name(e, ix("key_", "root")), opt_name(e, ix("key_", "scale")),
             opt(e, ix("phys_", "keys")) ? "from the held keys" : "free");
    txt(dc, lr, t, T_LCD_DIM, DT_LEFT | DT_VCENTER);
}

/* the rate timer (every RATE_TIMER_MS): the balls' box, every other tick (about 33 a second), only its own area */
void pics_tick(ed_t *e) {
    static unsigned n;
    if (!e->wnd || e->screen < 0 || e->screen >= NPAGES || PAGES[e->screen].pic != PIC_PHYS || ++n % 2) return;
    RECT a = {L.row[0].left, L.row[ROWS - PIC_ROWS].top, L.row[0].right, L.row[ROWS - 1].bottom};
    InvalidateRect(e->wnd, &a, FALSE);
}

void pics_draw(HDC dc, ed_t *e, int page, RECT a) {
    if (page < 0 || page >= NPAGES || !PAGES[page].pic) return;
    gfx_begin(dc);
    skin_bind(gfx_g);
    gfx_line((float)a.left, (float)a.top, (float)a.right, (float)a.top, 1.0f, C_RULE);   /* under the items */
    a.top += sc(2);
    switch (PAGES[page].pic) {
    case PIC_ENV: pic_env(dc, e, PAGES[page].pic_key, a); break;
    case PIC_LFO: pic_lfo(dc, e, PAGES[page].pic_key, a); break;
    case PIC_FILTER: pic_filters(dc, e, a); break;
    case PIC_MIXER: pic_mixer(dc, e, a); break;
    case PIC_PHYS: pic_phys(dc, e, a); break;
    }
    gfx_end();
    skin_bind(NULL);
}
