/* Getting around the centre screen (2026-10-30), the window's side only (no params, nothing in the audio path):
 *
 * BACK / FWD, at the two ends of the preset line (the patch name sits between them in its PRESET box, with the
 * preset's own < > inside that box): BACK shows the page shown before, as a web browser's back button does, at the
 * sub-page it was left on; FWD goes forward again after BACK. The history is the window's (16 pages each way), never
 * saved. It is taken from what the screen shows, whichever button or key changed it (nav_note runs before each of the
 * window's messages), so every way of changing the page counts. Moving through one page's sub-pages is not a new
 * step: BACK leaves the page. Shortcuts: Alt+Left / Alt+Right and the mouse's back / forward buttons.
 *
 * The generator tabs: on the GENERATIVE hub page, on each generator's own page and on the overview, the column-heads
 * line is a row of tabs, HUB, one per generator page (HUB_TAB from gen_layout.py, in the hub's order: new generators
 * join by themselves) and ALL. A tab's dot is lit while its generator (either one, on the page EUCLID and TURING share)
 * is on in the Synth in focus. The page's one-line note moves to the help line.
 *
 * The overview (ALL, or ALL RUNNING on the hub page): every generator that is on in one Synth (the one in focus, or the
 * one picked with SYNTH 1 2 3 at the title line's right end), each drawn small and moving in a tile (more_pics.c's
 * pictures; EUCLID's ring of steps and TURING's register drawn here, from their settings: the engine doesn't tell the
 * window their step). Generators that are off are left out, so the ones playing get the room; the tabs' dots show
 * which are off. A click on a tile opens that generator's page (in that Synth). The tiles repaint at the rate timer's
 * pace, a third of its ticks (about 22 a second), only while the overview shows. UI thread only. */
#include <stdio.h>
#include <string.h>
#include "editor.h"
#pragma GCC diagnostic push   /* (only some of gfx.h's shapes) */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "gfx.h"
#pragma GCC diagnostic pop
#include "skin.h"
#include "../src/seq.h"

#define T_LCD RGB(150, 215, 232)       /* as editor_draw.c */
#define T_LCD_DIM RGB(90, 130, 145)
#define T_HOT RGB(255, 255, 255)
#define T_INK RGB(11, 22, 33)          /* text on a lit button */
#define C_RULE 0xFF27404E
#define C_EDGE 0xFF3E6273
#define C_LIT 0xFF96D7E8
#define C_TRACE 0xFFA8ECFF
#define C_GLOW 0x3887DFFF
#define C_DIM 0xFF1C2C36
#define C_BTN 0xFF0F2230

static void txt(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    fmt |= DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER;
    if (skin_text(&r, s, f, c, fmt)) return;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt);
}
static void button(HDC dc, RECT r, const char *label, int lit, int hot) {   /* as editor_draw.c's screen_button */
    gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), lit ? C_LIT : C_BTN);
    gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : lit ? C_LIT : C_EDGE);
    txt(dc, r, label, font_tiny, lit ? T_INK : hot ? T_HOT : T_LCD, DT_CENTER);
}

/* ---- the history ---- */
static int known_screen(ed_t *e, int s) {   /* a page BACK may show again */
    if (s >= 0) return s < NPAGES;
    if (s == SCR_QMOD) return e->qm.src != 0;   /* (MOD FROM: only while a source is still picked) */
    return s == SCR_MATRIX || s == SCR_INDEX || s == SCR_RANDOM || s == SCR_SEQ || s == SCR_SETUP || s == SCR_GEN ||
           s == SCR_SETTINGS || s == SCR_GENALL || lib_screen(s);
}
static void push(nav_pos_t *st, int *n, nav_pos_t p) {   /* full: the oldest goes */
    if (*n == NAV_HIST) { memmove(st, st + 1, sizeof *st * (NAV_HIST - 1)); (*n)--; }
    st[(*n)++] = p;
}
void nav_note(ed_t *e) {
    nav_ui_t *v = &e->nav;
    nav_pos_t now = {(short)e->screen, (short)e->sub};
    if (!v->ready) { v->cur = now; v->ready = 1; return; }
    if (now.screen == v->cur.screen) { v->cur.sub = now.sub; return; }   /* (a sub-page: the same step) */
    push(v->back, &v->nb, v->cur);
    v->nf = 0;   /* (a new page: what FWD had goes, as in a browser) */
    v->cur = now;
}
static int usable(ed_t *e, const nav_pos_t *st, int n) {   /* the entries BACK / FWD could show */
    int k = 0;
    for (int j = 0; j < n; j++) k += known_screen(e, st[j].screen) && st[j].screen != e->screen;
    return k;
}
int nav_can(ed_t *e, int fwd) {
    nav_note(e);
    return fwd ? usable(e, e->nav.fwd, e->nav.nf) : usable(e, e->nav.back, e->nav.nb);
}
static void open_pos(ed_t *e, nav_pos_t p) {   /* each page as its own button opens it (the library reads its lists) */
    if (lib_screen(p.screen)) lib_open(e, p.screen);
    else if (p.screen == SCR_SEQ) seq_open(e, -1);
    else if (p.screen == SCR_SETTINGS) settings_open(e);
    else ed_show(e, p.screen, 0);
    int subs = screen_subs(e);
    e->sub = p.sub < subs ? p.sub : subs - 1;
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
}
void nav_go(ed_t *e, int fwd) {
    nav_ui_t *v = &e->nav;
    nav_note(e);
    nav_pos_t *from = fwd ? v->fwd : v->back, *to = fwd ? v->back : v->fwd;
    int *nfrom = fwd ? &v->nf : &v->nb, *nto = fwd ? &v->nb : &v->nf;
    while (*nfrom > 0) {
        nav_pos_t p = from[--*nfrom];
        if (!known_screen(e, p.screen) || p.screen == e->screen) continue;   /* (gone, or the page shown now) */
        push(to, nto, v->cur);
        open_pos(e, p);
        v->cur.screen = (short)e->screen;
        v->cur.sub = (short)e->sub;
        return;
    }
}

/* ---- the generator tabs ---- */
static int ntabs(void) { return NHUB_TAB > 0 && HUB_TAB[0].page >= 0 ? NHUB_TAB + 2 : 0; }
static int tab_screen(int k) { return k == 0 ? SCR_GEN : k <= NHUB_TAB ? HUB_TAB[k - 1].page : SCR_GENALL; }
static const char *tab_label(int k) { return k == 0 ? "HUB" : k <= NHUB_TAB ? HUB_TAB[k - 1].label : "ALL"; }
int nav_tabs_shown(ed_t *e) {
    for (int k = 0; k < ntabs(); k++)
        if (tab_screen(k) == e->screen) return 1;
    return 0;
}
int nav_gen_param(int i) {
    for (int r = 0; r < NHUB; r++)
        if (HUB[r].on >= 0 && HUB[r].on == i) return 1;
    return 0;
}
static int tab_dot(ed_t *e, int k) {   /* a generator page's tab: one of its generators on in the Synth in focus */
    if (k < 1 || k > NHUB_TAB) return 0;
    for (int r = 0; r < NHUB; r++)
        if (HUB[r].page == HUB_TAB[k - 1].page && hub_on(e, r, e->focus)) return 1;
    return 0;
}
static RECT tab_rect(int k) {   /* HUB and ALL narrow (no dot), the generators' share the rest */
    int n = ntabs(), gap = sc(3), ends = sc(26), bw = n > 2 ? (rw(&L.head) - 2 * ends - gap * (n - 1)) / (n - 2) : 0;
    int x = L.head.left + (k ? ends + gap + (k - 1) * (bw + gap) : 0), w = k == 0 || k == n - 1 ? ends : bw;
    if (k == n - 1) x = L.head.right - ends;
    RECT r = {x, L.head.top + sc(1), x + w, L.head.bottom - sc(1)};
    return r;
}
void nav_tabs_draw(HDC dc, ed_t *e) {
    gfx_begin(dc);
    skin_bind(gfx_g);
    for (int k = 0; k < ntabs(); k++) {
        RECT r = tab_rect(k);
        int cur = tab_screen(k) == e->screen, hot = e->hover == Z_NAV_TAB + k, dot = tab_dot(e, k);
        gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), cur ? C_LIT : C_BTN);
        gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : cur ? C_LIT : C_EDGE);
        RECT t = r;
        if (k >= 1 && k <= NHUB_TAB) t.right -= sc(3);   /* (a little room for the dot) */
        txt(dc, t, tab_label(k), font_tiny, cur ? T_INK : hot ? T_HOT : T_LCD, DT_CENTER);
        if (k >= 1 && k <= NHUB_TAB) {   /* the dot, at the right end: lit while it plays, a faint ring while it is off */
            float dx = r.right - 3.5f * L.s, dy = (r.top + r.bottom) / 2.0f;
            if (dot && !cur) gfx_disc(dx, dy, 3.4f * L.s, C_GLOW);
            if (dot) gfx_disc(dx, dy, 1.9f * L.s, cur ? 0xFF0B1621 : C_TRACE);
            else gfx_circle(dx, dy, 1.6f * L.s, 1.0f, cur ? 0x800B1621 : 0xFF3E6273);
        }
    }
    gfx_end();
    skin_bind(NULL);
}

/* ---- BACK / FWD: a framed button each, a hooked arrow over its name; dim with nowhere to go ---- */
void nav_top_draw(HDC dc, ed_t *e) {
    gfx_begin(dc);
    skin_bind(gfx_g);
    for (int f = 0; f < 2; f++) {
        RECT r = f ? L.fwd : L.back;
        int can = nav_can(e, f), hot = can && e->hover == (f ? Z_NAV_FWD : Z_NAV_BACK);
        DWORD c = !can ? 0xFF2E4A58 : hot ? 0xFFFFFFFF : C_LIT;
        gfx_rect((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), hot ? 0xFF16324A : C_BTN);
        gfx_frame((float)r.left, (float)r.top, (float)rw(&r), (float)rh(&r), 1.0f, hot ? 0xFFFFFFFF : can ? C_EDGE : C_RULE);
        /* the arrow: a hook (a turn back) then a head, mirrored for FWD */
        float cx = (r.left + r.right) / 2.0f, cy = r.top + 12.0f * L.s, d = f ? -1.0f : 1.0f, u = L.s;
        gpt_t hook[4] = {{cx + d * 7 * u, cy + 5 * u}, {cx + d * 7 * u, cy}, {cx + d * 3 * u, cy - 3 * u},
                         {cx - d * 3 * u, cy - 3 * u}};
        gfx_lines(hook, 4, 1.8f * u, c);
        gpt_t head[3] = {{cx - d * 8 * u, cy - 3 * u}, {cx - d * 3 * u, cy - 7.5f * u}, {cx - d * 3 * u, cy + 1.5f * u}};
        gfx_poly(head, 3, c);
        RECT lr = {r.left, r.bottom - sc(14), r.right, r.bottom - sc(2)};
        txt(dc, lr, f ? "FWD" : "BACK", font_tiny, !can ? RGB(46, 74, 88) : hot ? T_HOT : T_LCD, DT_CENTER);
    }
    gfx_end();
    skin_bind(NULL);
}

/* ---- the overview ---- */
static int ov_synth(ed_t *e) { return e->nav.ov_synth >= 0 && e->nav.ov_synth < NSYNTHS ? e->nav.ov_synth : e->focus; }
static int ov_items(ed_t *e, int *rows) {   /* the HUB rows on in the overview's Synth */
    int n = 0, c = ov_synth(e);
    for (int r = 0; r < NHUB; r++)
        if (hub_on(e, r, c)) rows[n++] = r;
    return n;
}
static void ov_grid(int n, int *cols, int *nrows) {
    *cols = n <= 3 ? (n < 1 ? 1 : n) : n == 4 ? 2 : n <= 9 ? 3 : 4;
    *nrows = (n + *cols - 1) / *cols;
    if (*nrows < 1) *nrows = 1;
}
static RECT ov_area(void) {
    RECT a = {L.row[0].left, L.row[0].top + sc(3), L.row[0].right, L.row[ROWS - 1].bottom};
    return a;
}
static RECT tile_rect(int k, int n) {
    int cols, nr, gap = sc(6);
    ov_grid(n, &cols, &nr);
    RECT a = ov_area();
    int tw = (rw(&a) - gap * (cols - 1)) / cols, th = (rh(&a) - gap * (nr - 1)) / nr;
    int x = a.left + (k % cols) * (tw + gap), y = a.top + (k / cols) * (th + gap);
    RECT r = {x, y, x + tw, y + th};
    return r;
}
static RECT syn_rect(int c) {   /* SYNTH 1 2 3, at the overview's title line's right end */
    int w = sc(18), gap = sc(3), x = L.tabline.right - (NSYNTHS - c) * (w + gap) + gap;
    RECT r = {x, L.tabline.top + sc(1), x + w, L.tabline.bottom - sc(1)};
    return r;
}
static RECT all_rect(void) {   /* the hub page's ALL RUNNING: the last row's right end */
    RECT r = {L.row[0].right - sc(104), L.row[ROWS - 1].top + sc(2), L.row[0].right, L.row[ROWS - 1].bottom - sc(2)};
    return r;
}

static int key_ix(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static int has_key(int i, const char *part) { return i >= 0 && strstr(PARAMS[i].key, part) != NULL; }
static void euclid_tile(HDC dc, ed_t *e, RECT a) {   /* EUCLID: the ring of steps, lit = a hit (as the panel's ring) */
    int pi[3] = {key_ix("gen_steps"), key_ix("gen_hits"), key_ix("gen_rot")};
    if (pi[0] < 0 || pi[1] < 0 || pi[2] < 0) return;
    int n = (int)lroundf(real_of(e, pi[0])), hits = (int)lroundf(real_of(e, pi[1]));
    n = n < 1 ? 1 : n > EU_MAX ? EU_MAX : n;
    hits = hits < 0 ? 0 : hits > n ? n : hits;
    int rot = (int)lroundf(real_of(e, pi[2])) % n;
    uint8_t pat[EU_MAX];
    euclid(n, hits, pat);
    float cx = (a.left + a.right) / 2.0f, cy = (a.top + a.bottom) / 2.0f;
    float R = fminf((float)rw(&a), (float)rh(&a)) / 2.0f - 4.0f * L.s, dot = fminf(3.0f * L.s, 3.1416f * R / n * 0.6f);
    if (R < 4.0f) return;
    gfx_circle(cx, cy, R, 1.0f, C_RULE);
    for (int k = 0; k < n; k++) {
        gpt_t p = gfx_polar(cx, cy, R, 360.0f * k / n);
        int hit = pat[(k - rot + n) % n];
        if (hit) gfx_disc(p.x, p.y, dot * 1.9f, C_GLOW);
        gfx_disc(p.x, p.y, dot, hit ? C_TRACE : 0xFF2E4A58);
    }
    char t[16];
    snprintf(t, sizeof t, "%d/%d", hits, n);
    RECT c = {(int)(cx - R), (int)(cy - sc(7)), (int)(cx + R), (int)(cy + sc(7))};
    txt(dc, c, t, font_tiny, T_LCD, DT_CENTER);
}
static void turing_tile(HDC dc, ed_t *e, RECT a) {   /* TURING: its register's cells (LENGTH) and how often one CHANGEs */
    int li = key_ix("gen_tm_len"), ci = key_ix("gen_tm_change");
    if (li < 0) return;
    int n = (int)lroundf(real_of(e, li));
    n = n < 1 ? 1 : n > 16 ? 16 : n;
    float ch = ci >= 0 ? real_of(e, ci) / 100.0f : 0.0f;
    float cw = fminf((rw(&a) - sc(8)) / (float)n, (float)sc(12)), x0 = (a.left + a.right) / 2.0f - cw * n / 2;
    float cy = (a.top + a.bottom) / 2.0f - sc(5), h = fminf(cw, (float)sc(12));
    for (int k = 0; k < n; k++) {   /* the loop: the last cell feeds the first */
        gfx_rect(x0 + k * cw + 1.0f, cy - h / 2, cw - 2.0f, h, k == 0 ? 0xFF2E5060 : C_DIM);
        gfx_frame(x0 + k * cw + 1.0f, cy - h / 2, cw - 2.0f, h, 1.0f, C_EDGE);
    }
    gpt_t loop[4] = {{x0 + n * cw - 2.0f, cy + h / 2 + 2.0f * L.s}, {x0 + n * cw - 2.0f, cy + h / 2 + 5.0f * L.s},
                     {x0 + 2.0f, cy + h / 2 + 5.0f * L.s}, {x0 + 2.0f, cy + h / 2 + 2.0f * L.s}};
    gfx_lines(loop, 4, 1.0f, C_LIT);
    float bw = cw * n, by = cy + h / 2 + sc(9);
    gfx_rect(x0, by, bw, 3.0f * L.s, C_DIM);
    gfx_rect(x0, by, bw * ch, 3.0f * L.s, C_LIT);
    char t[32];
    snprintf(t, sizeof t, "%d steps, change %.0f%%", n, ch * 100.0f);
    RECT tr = {a.left, (int)(by + sc(3)), a.right, (int)(by + sc(15))};
    if (tr.bottom <= a.bottom + sc(2)) txt(dc, tr, t, font_tiny, T_LCD_DIM, DT_CENTER);
}

void nav_overview_draw(HDC dc, ed_t *e) {
    int rows[64], n = ov_items(e, rows), c = ov_synth(e);
    gfx_begin(dc);
    skin_bind(gfx_g);
    char t[96];
    snprintf(t, sizeof t, "GENERATORS RUNNING IN SYNTH %d:  %d", c + 1, n);
    RECT tr = {L.tabline.left, L.tabline.top, syn_rect(0).left - sc(40), L.tabline.bottom};
    txt(dc, tr, t, font_tiny, T_LCD_DIM, DT_LEFT | DT_END_ELLIPSIS);
    RECT sl = {syn_rect(0).left - sc(38), L.tabline.top, syn_rect(0).left - sc(3), L.tabline.bottom};
    txt(dc, sl, "SYNTH", font_tiny, T_LCD_DIM, DT_RIGHT);
    for (int k = 0; k < NSYNTHS; k++) {
        char d[2] = {(char)('1' + k), 0};
        button(dc, syn_rect(k), d, k == c, e->hover == Z_NAV_SYN + k);
    }
    if (!n) {
        RECT m = ov_area();
        m.bottom = m.top + sc(40);
        snprintf(t, sizeof t, "Nothing is running in Synth %d.", c + 1);
        txt(dc, m, t, font_cell, T_LCD, DT_CENTER);
        OffsetRect(&m, 0, sc(20));
        txt(dc, m, "Switch a generator ON on the HUB tab or on its own page.", font_tiny, T_LCD_DIM, DT_CENTER);
    }
    int focus = e->focus;
    unsigned mask = e->focus_mask;
    e->focus = c;   /* (the pictures read the focused Synth: the overview's, while they are drawn) */
    e->focus_mask = 1u << c;
    for (int k = 0; k < n; k++) {
        int r = rows[k], hot = e->hover == Z_NAV_TILE + r;
        RECT b = tile_rect(k, n);
        gfx_rect((float)b.left, (float)b.top, (float)rw(&b), (float)rh(&b), 0xFF0D1C27);
        gfx_frame((float)b.left, (float)b.top, (float)rw(&b), (float)rh(&b), 1.0f, hot ? 0xFFFFFFFF : C_EDGE);
        RECT lr = {b.left + sc(5), b.top + sc(1), b.right - sc(14), b.top + sc(13)};
        txt(dc, lr, HUB[r].label, font_tiny, hot ? T_HOT : T_LCD, DT_LEFT | DT_END_ELLIPSIS);
        RECT ar = {b.right - sc(13), b.top + sc(1), b.right - sc(4), b.top + sc(13)};
        txt(dc, ar, ">", font_tiny, T_LCD_DIM, DT_RIGHT);
        RECT pa = {b.left + sc(3), b.top + sc(13), b.right - sc(3), b.bottom - sc(3)};
        int page = HUB[r].page, on = HUB[r].on;
        if (has_key(on, "euclid")) euclid_tile(dc, e, pa);
        else if (has_key(on, "gen_tm")) turing_tile(dc, e, pa);
        else {
            gfx_end();   /* (more_pics.c draws on its own surface) */
            skin_bind(NULL);
            pics_tile(dc, e, page, has_key(on, "balls") ? 1 : has_key(on, "parts") ? 2 : 0, pa);
            gfx_begin(dc);
            skin_bind(gfx_g);
        }
    }
    e->focus = focus;
    e->focus_mask = mask;
    gfx_end();
    skin_bind(NULL);
}

void nav_hub_draw(HDC dc, ed_t *e) {
    gfx_begin(dc);
    skin_bind(gfx_g);
    button(dc, all_rect(), "ALL RUNNING  >", 0, e->hover == Z_NAV_ALL);
    gfx_end();
    skin_bind(NULL);
}

void nav_tick(ed_t *e) {   /* the rate timer (RATE_TIMER_MS): the tiles, a third of its ticks, while they show */
    static unsigned n;
    if (!e->wnd || e->screen != SCR_GENALL || ++n % 3) return;
    int rows[64];
    if (!ov_items(e, rows)) return;
    RECT a = ov_area();
    InvalidateRect(e->wnd, &a, FALSE);
    e->nav.ticks++;
}

/* ---- clicks ---- */
int nav_hit(ed_t *e, POINT pt) {
    if (nav_tabs_shown(e))
        for (int k = 0; k < ntabs(); k++) {
            RECT r = tab_rect(k);
            if (PtInRect(&r, pt)) return Z_NAV_TAB + k;
        }
    if (e->screen == SCR_GEN) {
        RECT r = all_rect();
        if (PtInRect(&r, pt)) return Z_NAV_ALL;
    }
    if (e->screen != SCR_GENALL) return -1;
    for (int c = 0; c < NSYNTHS; c++) {
        RECT r = syn_rect(c);
        if (PtInRect(&r, pt)) return Z_NAV_SYN + c;
    }
    int rows[64], n = ov_items(e, rows);
    for (int k = 0; k < n; k++) {
        RECT r = tile_rect(k, n);
        if (PtInRect(&r, pt)) return Z_NAV_TILE + rows[k];
    }
    return -1;
}
int nav_press(ed_t *e, int z) {
    if (z < Z_NAV_BACK || z >= Z_NAV_END) return 0;
    if (z == Z_NAV_BACK || z == Z_NAV_FWD) nav_go(e, z == Z_NAV_FWD);
    else if (z == Z_NAV_ALL) ed_show(e, SCR_GENALL, 0);
    else if (z >= Z_NAV_SYN && z < Z_NAV_SYN + NSYNTHS) e->nav.ov_synth = z - Z_NAV_SYN;
    else if (z >= Z_NAV_TAB && z < Z_NAV_TAB + ntabs()) {
        int s = tab_screen(z - Z_NAV_TAB);
        if (s != e->screen) ed_show(e, s, 0);
    } else if (z >= Z_NAV_TILE && z < Z_NAV_TILE + NHUB && HUB[z - Z_NAV_TILE].page >= 0) {
        int c = ov_synth(e);   /* its page, in the Synth the overview showed */
        if (c != e->focus) ed_focus(e, c, 0);
        ed_show(e, HUB[z - Z_NAV_TILE].page, 0);
    }
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
    return 1;
}

/* ---- help ---- */
const char *nav_zone_help(ed_t *e, int z) {
    static char t[240];
    if (z == Z_NAV_BACK || z == Z_NAV_FWD) {
        int f = z == Z_NAV_FWD, n = nav_can(e, f);
        if (!n) return f ? "FWD: forward again to the page you left with BACK (nothing to go forward to now)."
                         : "BACK: the page shown before this one (nothing yet: open another page first).";
        const nav_pos_t *st = f ? e->nav.fwd : e->nav.back;
        int i = (f ? e->nav.nf : e->nav.nb) - 1;
        while (i > 0 && (!known_screen(e, st[i].screen) || st[i].screen == e->screen)) i--;
        int s = st[i].screen;
        const char *name = s >= 0 && s < NPAGES ? PAGES[s].title : s == SCR_MATRIX ? "the MATRIX" : s == SCR_INDEX ? "the MORE list"
            : s == SCR_RANDOM ? "RANDOM" : s == SCR_SEQ ? "the step editor" : s == SCR_SETUP ? "SETUP" : s == SCR_GEN ? "GENERATIVE"
            : s == SCR_SETTINGS ? "SETTINGS" : s == SCR_GENALL ? "the generators overview" : s == SCR_QMOD ? "MOD FROM" : "the library";
        snprintf(t, sizeof t, "%s: %s %s (%d page%s %s). Only the screen changes, never the sound: the PRESET box "
                 "beside it has the preset arrows. Keys: Alt+%s, or the mouse's %s button.", f ? "FWD" : "BACK",
                 f ? "on to" : "back to", name, n, n == 1 ? "" : "s", f ? "ahead" : "back", f ? "Right" : "Left",
                 f ? "forward" : "back");
        return t;
    }
    if (z == Z_NAV_ALL) return "ALL RUNNING: every generator that is on, drawn small and moving on one page (also the ALL "
                               "tab). Click one there for its page.";
    if (z >= Z_NAV_SYN && z < Z_NAV_SYN + NSYNTHS) return "SYNTH 1 / 2 / 3: which Synth's generators the overview shows "
                                                           "(only this page: the panel keeps its Synth).";
    if (z >= Z_NAV_TAB && z < Z_NAV_TAB + ntabs()) {
        int k = z - Z_NAV_TAB;
        if (k == 0) return "HUB: the GENERATIVE page, every generator's ON, KEY and SCALE.";
        if (k > NHUB_TAB) return "ALL: every generator that is on, drawn small and moving on one page; click one for its page.";
        snprintf(t, sizeof t, "%s: the %s page. The dot is lit while it is on in the Synth in focus.", HUB_TAB[k - 1].label,
                 PAGES[HUB_TAB[k - 1].page].title);
        return t;
    }
    if (z >= Z_NAV_TILE && z < Z_NAV_TILE + NHUB) {
        snprintf(t, sizeof t, "%s: click for its page (in Synth %d).", HUB[z - Z_NAV_TILE].label, ov_synth(e) + 1);
        return t;
    }
    return NULL;
}
void nav_help(ed_t *e, char *out, int n) {
    snprintf(out, n, "Every generator that is on in Synth %d, moving as it plays (EUCLID and TURING: their pattern). Click "
                     "one for its page; SYNTH 1 2 3 (top right) for another Synth's. The tabs' dots: which are on.",
             ov_synth(e) + 1);
}

/* ---- load_test (effVendorSpecific TEST_NAV) ---- */
static intptr_t mid(RECT r) { return IsRectEmpty(&r) ? -1 : (intptr_t)((r.left + r.right) / 2 + 65536 * ((r.top + r.bottom) / 2)); }
intptr_t nav_test(ed_t *e, intptr_t op, char *buf, float opt) {
    (void)buf;
    int k = (int)lroundf(opt), rows[64], n = ov_items(e, rows);
    nav_note(e);
    switch (op) {
    case NT_BACK_N: return nav_can(e, 0);
    case NT_FWD_N: return nav_can(e, 1);
    case NT_TABS: return nav_tabs_shown(e);
    case NT_TILES: return e->screen == SCR_GENALL ? n : 0;
    case NT_TILE_XY: return e->screen == SCR_GENALL && k >= 0 && k < n ? mid(tile_rect(k, n)) : -1;
    case NT_TILE_ROW: return k >= 0 && k < n ? rows[k] : -1;
    case NT_TAB_XY: return k >= 0 && k < ntabs() ? mid(tab_rect(k)) : -1;
    case NT_TAB_DOT: return tab_dot(e, k);
    case NT_OV_SYNTH: return ov_synth(e);
    case NT_TICKS: return (intptr_t)e->nav.ticks;
    case NT_XY:
        if (k == Z_NAV_BACK) return mid(L.back);
        if (k == Z_NAV_FWD) return mid(L.fwd);
        if (k == Z_NAV_ALL) return mid(all_rect());
        if (k >= Z_NAV_SYN && k < Z_NAV_SYN + NSYNTHS) return mid(syn_rect(k - Z_NAV_SYN));
        return -1;
    }
    return 0;
}
