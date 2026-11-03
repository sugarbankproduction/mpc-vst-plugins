/* The window's size and PANEL ZOOM (2026-11-03, owner), on the rail under the keys; the plugin window only (the web page has
 * its own camera).
 *
 * SIZE: - and + (10% a click), the corner's grip (drag it; it lands when you let go) and FIT (as large as the screen allows,
 * never over 100%, as it always was). Settings.txt keeps it (window_size). The host is asked to resize its
 * window (audioMasterSizeWindow): a host's own frame can't resize a VST2, this is the plugin's way. A host that says no
 * shows the new size when the window next opens.
 *
 * PANEL ZOOM: click it, then a panel: the panel fills the window, drawn at the larger size, so sharp. Or the mouse, anywhere:
 * hold the middle button and drag to pan; the wheel with the middle button or Ctrl held, or over an empty part of the panel, zooms
 * around the pointer. While zoomed a bar stays along the window's bottom edge (2026-11-03, owner: the widest, nearest target; the
 * camera keeps the panel above it): each Synth to select (double-click: solo it alone), its M and S, < > the panels in turn, - +
 * the zoom, BACK the whole instrument. The whole panel is laid out at the larger scale and this window, a child of the host's, grows
 * with it and slides so the panel shows: the host's window clips the rest. So every click, repaint and menu works in the same
 * coordinates as ever, nothing else knows. A zoom step is one of a few sizes: another panel at the same one only slides.
 *
 * QUICK: a pan moves the window and draws only what comes into view. A new size means a new bake of the panel (up to about 1 s),
 * so the wheel stretches the picture already there at once, and the sharp one comes when the wheel stops (130 ms); a size
 * baked before is ready at once (skin.c).
 *
 * The layout is one per DLL (L), so a second Moo Won window open at once follows the first one's size and zoom. */
#include <math.h>
#include <stdio.h>
#include "editor.h"

#ifdef __EMSCRIPTEN__
float view_base(float fit) { return fit; }
float view_zoom(void) { return 1.0f; }
void view_open(void) {}
void view_settings_changed(ed_t *e) { (void)e; }
int view_hit(ed_t *e, POINT pt) { (void)e; (void)pt; return -1; }
int view_press(ed_t *e, int z, int x, int y) { (void)e; (void)z; (void)x; (void)y; return 0; }
void view_drag(ed_t *e, int x, int y) { (void)e; (void)x; (void)y; }
void view_release(ed_t *e, int commit) { (void)e; (void)commit; }
void view_draw(HDC dc, ed_t *e) { (void)dc; (void)e; }
void view_tick(ed_t *e) { (void)e; }
void view_commit(ed_t *e) { (void)e; }
int view_previewing(void) { return 0; }
int view_wheel(ed_t *e, int x, int y, int dir, int forced, int z) { (void)e; (void)x; (void)y; (void)dir; (void)forced; (void)z; return 0; }
void view_pan_start(ed_t *e, int x, int y) { (void)e; (void)x; (void)y; }
void view_pan_end(ed_t *e) { (void)e; }
const char *view_help(int z) { (void)z; return NULL; }
int winv_rect(int z, RECT *r) { (void)z; (void)r; return 0; }
#else

static float zoom = 1.0f;
static int ox, oy, panel = -1, armed, drag_pct, drag_dx;
static int panning, pan_ox, pan_oy;
static POINT pan_at;
static unsigned bar_seen;
static RECT slide_was;
static const float STEPS[] = {1.15f, 1.3f, 1.5f, 1.75f, 2.0f, 2.4f};
enum { MIN_PCT = 50, MAX_PCT = 200, MIN_AREA = 4000, BIG_PANEL = 300000 };
#define MAX_SCALE 2.5f

float view_base(float fit) {
    int p = settings_ui_size();
    return p > 0 ? (float)p * 0.01f : fit;
}
float view_zoom(void) { return zoom; }

/* ---- the panels ---- */
static int npanels(void) { return NPANELS + 1; }   /* (the last: the keyboard, not a panel in the layout) */
static int panel_box(int i, int *x, int *y, int *w, int *h) {   /* base units; 0: not one to frame */
    if (i == NPANELS) {
        *x = ZONE_KEYS[0];
        *y = ZONE_RIBBON[1] - 2;
        *w = ZONE_KEYS[2];
        *h = ZONE_KEYS[1] + ZONE_KEYS[3] - *y;
        return 1;
    }
    const lay_panel_t *p = &PANELS[i];
    *x = p->x; *y = p->y; *w = p->w; *h = p->h;
    return p->style != 2 && p->w * p->h >= MIN_AREA;
}
static int panel_at(int bx, int by) {   /* the smallest panel there, or -1 */
    int best = -1, area = 0, x, y, w, h;
    for (int i = 0; i < npanels(); i++)
        if (panel_box(i, &x, &y, &w, &h) && bx >= x && by >= y && bx < x + w && by < y + h && (best < 0 || w * h < area)) {
            best = i;
            area = w * h;
        }
    return best;
}
static int panel_step(int from, int d) {   /* the next one for < >: the big blocks hold smaller panels, those are skipped */
    int n = npanels(), x, y, w, h;
    for (int k = 1, i = from; k <= n; k++) {
        i = (i + d + n) % n;
        if (panel_box(i, &x, &y, &w, &h) && w * h < BIG_PANEL) return i;
    }
    return from;
}

/* ---- every open window takes the layout ---- */
static int bar_h(void) { return (int)(26.0f * L.vw / BASE_W); }   /* the bar keeps its size on the screen, whatever the zoom */
static void clamped(int *x, int *y) {   /* the panel's bottom can come up over the bar */
    int mx = L.w - L.vw, my = L.h - (L.vh - bar_h());
    if (zoom == 1.0f) mx = my = 0;
    *x = *x < 0 ? 0 : *x > mx ? mx : *x;
    *y = *y < 0 ? 0 : *y > my ? my : *y;
}
static RECT bar_all(void) {
    RECT r = {ox, oy + L.vh - bar_h(), ox + L.vw, oy + L.vh};
    return r;
}
static void place(ed_t *e) {
    if (!e->wnd) return;
    SetWindowPos(e->wnd, NULL, -ox, -oy, L.w, L.h, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(e->wnd, NULL, FALSE);
}
static void place_and_size(ed_t *e) {
    if (!e->wnd) return;
    if (e->host) e->host(e->fx, audioMasterSizeWindow, L.vw, L.vh, NULL, 0);
    place(e);
}
static void zoom_to(float k, int x, int y) {
    if (k != zoom) {
        zoom = k;
        editor_relayout();
    }
    clamped(&x, &y);
    ox = x;
    oy = y;
    ed_each(place);
}
static void zoom_off(void) {
    panel = -1;
    armed = 0;
    if (zoom != 1.0f || ox || oy) zoom_to(1.0f, 0, 0);
}
static void slide(ed_t *e) {   /* the picture moves; only the bar is drawn again, old place and new */
    if (!e->wnd) return;
    SetWindowPos(e->wnd, NULL, -ox, -oy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT now = bar_all();
    InvalidateRect(e->wnd, &slide_was, FALSE);
    InvalidateRect(e->wnd, &now, FALSE);
    UpdateWindow(e->wnd);
}
static void pan_to(int x, int y) {
    clamped(&x, &y);
    if (x == ox && y == oy) return;
    slide_was = bar_all();
    ox = x;
    oy = y;
    ed_each(slide);
}
static void pv_free(void);
void view_open(void) {   /* a window opens: the whole instrument */
    pv_free();
    panel = -1;
    armed = 0;
    ox = oy = 0;
    if (zoom != 1.0f) {
        zoom = 1.0f;
        editor_relayout();
    }
}
void view_settings_changed(ed_t *e) {   /* WINDOW SIZE changed */
    (void)e;
    panel = -1;
    armed = 0;
    zoom = 1.0f;
    ox = oy = 0;
    editor_relayout();
    ed_each(place_and_size);
}
static void frame_panel(int i) {
    int x, y, w, h;
    if (!panel_box(i, &x, &y, &w, &h)) return;
    float s0 = (float)L.vw / BASE_W, bar = (float)bar_h();
    float fit = fminf((float)L.vw / (w * s0), ((float)L.vh - bar) / (h * s0)) * 0.97f, k = 1.0f;
    for (unsigned j = 0; j < sizeof STEPS / sizeof STEPS[0]; j++)
        if (STEPS[j] <= fit && s0 * STEPS[j] <= MAX_SCALE) k = STEPS[j];
    panel = i;
    if (k != zoom) {
        zoom = k;
        editor_relayout();
    }
    float s = L.s;
    zoom_to(k, (int)lroundf((x + w * 0.5f) * s - L.vw * 0.5f), (int)lroundf((y + h * 0.5f) * s - (L.vh - bar) * 0.5f));
}

/* ---- the buttons ---- */
static RECT rail(int part) {   /* the rail's, right of the keyboard's hint, left of our name */
    static const short W[] = {16, 42, 16, 30, 86};
    int gap = sc(3), top = L.keys.bottom + sc(1), bottom = L.h - sc(1), x = L.keys.right - sc(70);
    for (int k = 4; k >= part; k--) {
        x -= sc(W[k]);
        if (k > part) x -= gap;
    }
    RECT r = {x, top, x + sc(W[part]), bottom};
    return r;
}
static RECT grip(void) {
    RECT r = {L.w - sc(18), L.h - sc(18), L.w, L.h};
    return r;
}
/* zoomed: the bar's parts, on the screen the same size at any zoom; the Synths from the left, the camera from the right */
static RECT strip(int k) {
    float s0 = (float)L.vw / BASE_W;
    RECT all = bar_all(), r = all;
    int pad = (int)(2.0f * s0);
    r.top += pad;
    r.bottom -= pad;
#define U(v) ((int)((v) * s0))
    if (k >= VW_SYN1) {   /* SYNTH n | M | S */
        int c = (k - VW_SYN1) % 3, part = (k - VW_SYN1) / 3, x = all.left + U(4) + c * U(120);
        r.left = x + (part == 0 ? 0 : part == 1 ? U(64) : U(88));
        r.right = r.left + (part == 0 ? U(62) : U(22));
        return r;
    }
    static const short W[] = {26, 170, 26, 26, 26, 56};   /* < title > - + BACK */
    static const char ORDER[] = {VW_PREV, VW_TITLE, VW_NEXT, VW_ZOUT, VW_ZIN, VW_BACK};
    int x = all.right - U(4);
    for (int j = 5; j >= 0; j--) {
        x -= U(W[j]);
        if (ORDER[j] == k) {
            r.left = x;
            r.right = x + U(W[j]);
            return r;
        }
        x -= U(3);
    }
#undef U
    return r;
}
int winv_rect(int z, RECT *r) {
    int k = z - Z_WINV;
    if (k < 0 || k >= VW_COUNT) return 0;
    if (zoom != 1.0f) {
        if (k < VW_PREV) return 0;
        *r = strip(k);
        return 1;
    }
    if (k >= VW_PREV) return 0;
    *r = k == VW_GRIP ? grip() : rail(k);
    return 1;
}
int view_hit(ed_t *e, POINT pt) {
    (void)e;
    for (int z = Z_WINV; z < Z_WINV + VW_COUNT; z++) {
        RECT r;
        if (winv_rect(z, &r) && PtInRect(&r, pt)) return z;
    }
    if (zoom != 1.0f) {   /* the bar's gaps are its own: nothing under it takes a click */
        RECT r = bar_all();
        if (PtInRect(&r, pt)) return Z_WINV + VW_TITLE;
    }
    return -1;
}
static int pct_now(void) { return (int)lroundf(100.0f * L.vw / BASE_W); }
static void repaint(ed_t *e) {
    RECT r;
    if (zoom != 1.0f) {
        r = bar_all();
        InvalidateRect(e->wnd, &r, FALSE);
        return;
    }
    for (int z = Z_WINV; z < Z_WINV + VW_COUNT; z++)
        if (winv_rect(z, &r)) InvalidateRect(e->wnd, &r, FALSE);
}
int view_wheel(ed_t *e, int x, int y, int dir, int forced, int z);
int view_press(ed_t *e, int z, int x, int y) {   /* 1: the press was ours */
    int k = z - Z_WINV;
    if (view_previewing()) {   /* (the picture is stretched: this press only lands it) */
        view_commit(e);
        return 1;
    }
    if (k < 0 || k >= VW_COUNT) {
        if (!armed) return 0;
        armed = 0;   /* PANEL ZOOM waiting: this press only says which panel */
        repaint(e);
        int i = panel_at((int)((float)x / L.s), (int)((float)y / L.s));
        if (i >= 0) frame_panel(i);
        return 1;
    }
    switch (k) {
    case VW_MINUS: case VW_PLUS: {
        int p = (pct_now() + 5) / 10 * 10 + (k == VW_PLUS ? 10 : -10);
        settings_ui_size_set(e, p < MIN_PCT ? MIN_PCT : p > MAX_PCT ? MAX_PCT : p);
        break;
    }
    case VW_SIZE: case VW_FIT: settings_ui_size_set(e, 0); break;
    case VW_ZOOM:
        armed = !armed;
        repaint(e);
        break;
    case VW_GRIP:
        drag_dx = L.w - x;
        drag_pct = pct_now();
        e->mode = M_VIEW;
        SetCapture(e->wnd);
        break;
    case VW_PREV: case VW_NEXT:
        frame_panel(panel_step(panel, k == VW_NEXT ? 1 : -1));
        break;
    case VW_BACK: zoom_off(); break;
    case VW_ZIN: case VW_ZOUT: {
        RECT a = bar_all();
        view_wheel(e, ox + L.vw / 2, a.top / 2 + oy / 2, k == VW_ZIN ? 1 : -1, 1, -1);
        break;
    }
    case VW_SYN1: case VW_SYN1 + 1: case VW_SYN1 + 2: synth_bar_press(e, k - VW_SYN1, 0); repaint(e); break;
    case VW_M1: case VW_M1 + 1: case VW_M1 + 2: synth_bar_press(e, k - VW_M1, 1); repaint(e); break;
    case VW_S1: case VW_S1 + 1: case VW_S1 + 2: synth_bar_press(e, k - VW_S1, 2); repaint(e); break;
    default: break;
    }
    return 1;
}
void view_drag(ed_t *e, int x, int y) {
    if (panning) {
        POINT at = {x, y};
        ClientToScreen(e->wnd, &at);
        pan_to(pan_ox - (at.x - pan_at.x), pan_oy - (at.y - pan_at.y));
        return;
    }
    (void)y;
    int p = (int)lroundf(100.0f * (float)(x + drag_dx) / BASE_W / 5.0f) * 5;
    p = p < MIN_PCT ? MIN_PCT : p > MAX_PCT ? MAX_PCT : p;
    SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
    if (p == drag_pct) return;
    drag_pct = p;
    RECT r = rail(VW_SIZE);
    InvalidateRect(e->wnd, &r, FALSE);
}
/* ---- the mouse's middle button and wheel ---- */
void view_pan_start(ed_t *e, int x, int y) {
    view_commit(e);
    if (zoom == 1.0f || e->mode != M_NONE) return;
    pan_at.x = x;
    pan_at.y = y;
    ClientToScreen(e->wnd, &pan_at);
    pan_ox = ox;
    pan_oy = oy;
    panning = 1;
    e->mode = M_VIEW;
    SetCapture(e->wnd);
    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
}
void view_pan_end(ed_t *e) {
    if (panning) view_release(e, 0);
}
/* the quick stretch: the picture of what shows, moved and scaled to where the wheel asks */
static HDC pv_dc;
static HBITMAP pv_bm;
static HGDIOBJ pv_old;
static int pv_on, pv_w, pv_h;
static float pv_k0, pv_r, pv_tx, pv_ty;
int view_previewing(void) { return pv_on; }
static void pv_free(void) {
    if (pv_dc) {
        SelectObject(pv_dc, pv_old);
        DeleteDC(pv_dc);
        DeleteObject(pv_bm);
    }
    pv_dc = NULL;
    pv_bm = NULL;
    pv_on = 0;
}
static int pv_start(ed_t *e) {
    pv_w = L.vw;
    pv_h = L.vh - (zoom != 1.0f ? bar_h() : 0);
    HDC dc = GetDC(e->wnd);
    pv_dc = CreateCompatibleDC(dc);
    pv_bm = CreateCompatibleBitmap(dc, pv_w, pv_h);
    ReleaseDC(e->wnd, dc);
    if (!pv_dc || !pv_bm) { pv_free(); return 0; }
    pv_old = SelectObject(pv_dc, pv_bm);
    if (!skin_snapshot(pv_dc, ox, oy, pv_w, pv_h)) { pv_free(); return 0; }
    pv_k0 = zoom;
    pv_r = 1.0f;
    pv_tx = pv_ty = 0.0f;
    pv_on = 1;
    return 1;
}
static void pv_show(ed_t *e) {
    HDC dc = GetDC(e->wnd);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, ox, oy, ox + pv_w, oy + pv_h);
    int dw = (int)lroundf(pv_w * pv_r), dh = (int)lroundf(pv_h * pv_r), dx = ox + (int)lroundf(pv_tx), dy = oy + (int)lroundf(pv_ty);
    if (pv_r < 1.0f) {
        RECT all = {ox, oy, ox + pv_w, oy + pv_h};
        FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchBlt(dc, dx, dy, dw, dh, pv_dc, 0, 0, pv_w, pv_h, SRCCOPY);
    RestoreDC(dc, saved);
    ReleaseDC(e->wnd, dc);
}
void view_commit(ed_t *e) {
    if (!pv_on) return;
    float k = pv_k0 * pv_r;
    int nx = (int)lroundf(ox * pv_r - pv_tx), ny = (int)lroundf(oy * pv_r - pv_ty);
    pv_free();
    KillTimer(e->wnd, 3);
    armed = 0;
    panel = -1;
    if (fabsf(k - 1.0f) < 0.001f) zoom_off();
    else zoom_to(k, nx, ny);
    if (panning) {
        pan_ox = ox;
        pan_oy = oy;
        GetCursorPos(&pan_at);
    }
    if (e->wnd) UpdateWindow(e->wnd);
}
int view_wheel(ed_t *e, int x, int y, int dir, int forced, int z) {   /* 1: the wheel zoomed */
    POINT pt = {x, y};
    if (!forced && (z >= 0 || PtInRect(&L.screen, pt))) return 0;   /* over a control it does the control's thing */
    float s0 = (float)L.vw / BASE_W, now = pv_on ? pv_k0 * pv_r : zoom, k = now;
    if (dir > 0) {
        for (unsigned j = 0; j < sizeof STEPS / sizeof STEPS[0] && k == now; j++)
            if (STEPS[j] > now + 0.001f && s0 * STEPS[j] <= MAX_SCALE) k = STEPS[j];
    } else {
        k = 1.0f;
        for (unsigned j = 0; j < sizeof STEPS / sizeof STEPS[0]; j++)
            if (STEPS[j] < now - 0.001f) k = STEPS[j];
    }
    if (fabsf(k - now) < 0.001f) return 1;
    if (!pv_on && !pv_start(e)) {   /* (no picture to stretch: straight to the sharp one) */
        panel = -1;
        if (k == 1.0f) zoom_off();
        else {
            float bx = (float)x / L.s, by = (float)y / L.s;
            int sx = x - ox, sy = y - oy;
            zoom = k;
            editor_relayout();
            zoom_to(k, (int)lroundf(bx * L.s) - sx, (int)lroundf(by * L.s) - sy);
        }
        return 1;
    }
    /* the place under the pointer stays there; the picture never leaves a gap the panel wouldn't */
    float r = k / pv_k0, sx = (float)(x - ox), sy = (float)(y - oy), f = r / pv_r;
    float tx = sx - (sx - pv_tx) * f, ty = sy - (sy - pv_ty) * f;
    float base_w = (float)L.w, base_h = (float)L.h;
    float hi_x = ox * r, lo_x = ox * r - (base_w * r - L.vw), hi_y = oy * r, lo_y = oy * r - (base_h * r - pv_h);
    tx = tx > hi_x ? hi_x : tx < lo_x ? lo_x : tx;
    ty = ty > hi_y ? hi_y : ty < lo_y ? lo_y : ty;
    pv_r = r;
    pv_tx = tx;
    pv_ty = ty;
    pv_show(e);
    SetTimer(e->wnd, 3, 130, NULL);
    return 1;
}
void view_tick(ed_t *e) {   /* the bar follows what changed elsewhere: the Synth in focus, a mute, a solo */
    if (zoom == 1.0f || !e->wnd) return;
    unsigned sig = e->focus_mask;
    for (int c = 0; c < 3; c++) sig = sig * 4u + (unsigned)synth_muted(e, c) * 2u + (unsigned)synth_soloed(e, c);
    if (sig == bar_seen) return;
    bar_seen = sig;
    RECT r = bar_all();
    InvalidateRect(e->wnd, &r, FALSE);
}
void view_release(ed_t *e, int commit) {
    e->mode = M_NONE;
    if (panning) {
        panning = 0;
        return;
    }
    int p = drag_pct;
    drag_pct = 0;
    if (commit && p != pct_now()) settings_ui_size_set(e, p);
    else repaint(e);
}

/* ---- drawing, over everything ---- */
static HFONT bar_font;
static int bar_font_h;
static void button_in(HDC dc, RECT r, const char *t, HFONT f, COLORREF fillc, COLORREF edge, COLORREF textc) {
    HBRUSH b = CreateSolidBrush(fillc);
    FillRect(dc, &r, b);
    DeleteObject(b);
    b = CreateSolidBrush(edge);
    FrameRect(dc, &r, b);
    DeleteObject(b);
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textc);
    DrawTextA(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
static void button(HDC dc, RECT r, const char *t, int lit, int hot) {
    button_in(dc, r, t, font_tiny, lit ? RGB(255, 150, 40) : hot ? RGB(0x5A, 0x34, 0x10) : RGB(0x1C, 0x14, 0x0E), RGB(255, 150, 40),
              lit ? RGB(0, 0, 0) : RGB(240, 205, 150));
}
static void bar_button(HDC dc, int k, const char *t, COLORREF lit, int on, int hot) {
    RECT r = strip(k);
    button_in(dc, r, t, bar_font, on ? lit : hot ? RGB(0x3A, 0x3A, 0x40) : RGB(0x24, 0x24, 0x28), on ? lit : RGB(0x6A, 0x6A, 0x72),
              on ? RGB(0, 0, 0) : RGB(235, 235, 235));
}
void view_draw(HDC dc, ed_t *e) {
    int h = e->hover;
    RECT clip, area, tmp;   /* (a repaint elsewhere: nothing to do) */
    GetClipBox(dc, &clip);
    if (zoom != 1.0f) area = bar_all();
    else SetRect(&area, rail(VW_MINUS).left, grip().top, L.w, L.h);
    if (!IntersectRect(&tmp, &clip, &area)) return;
    if (zoom != 1.0f) {
        int fh = (int)(13.0f * L.vw / BASE_W);
        fh = fh < 9 ? 9 : fh;
        if (fh != bar_font_h || !bar_font) {
            if (bar_font) DeleteObject(bar_font);
            bar_font = CreateFontA(-fh, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            bar_font_h = fh;
        }
        HBRUSH b = CreateSolidBrush(RGB(0x12, 0x12, 0x14));
        FillRect(dc, &area, b);
        DeleteObject(b);
        for (int c = 0; c < 3; c++) {
            char t[16];
            snprintf(t, sizeof t, "SYNTH %d", c + 1);
            bar_button(dc, VW_SYN1 + c, t, RGB(150, 215, 232), (e->focus_mask >> c & 1u) != 0, h == Z_WINV + VW_SYN1 + c);
            bar_button(dc, VW_M1 + c, "M", RGB(235, 70, 60), synth_muted(e, c), h == Z_WINV + VW_M1 + c);
            bar_button(dc, VW_S1 + c, "S", RGB(255, 200, 60), synth_soloed(e, c), h == Z_WINV + VW_S1 + c);
        }
        char title[64];
        if (panel >= 0 && panel < NPANELS) {
            snprintf(title, sizeof title, "%s", PANELS[panel].title);
            for (char *c = title; *c; c++) if (*c == '|') *c = '/';
        } else if (panel == NPANELS) {
            snprintf(title, sizeof title, "KEYBOARD");
        } else {
            snprintf(title, sizeof title, "ZOOM %d%%", (int)lroundf(zoom * 100.0f));
        }
        bar_button(dc, VW_PREV, "<", 0, 0, h == Z_WINV + VW_PREV);
        bar_button(dc, VW_TITLE, title, 0, 0, 0);
        bar_button(dc, VW_NEXT, ">", 0, 0, h == Z_WINV + VW_NEXT);
        bar_button(dc, VW_ZOUT, "-", 0, 0, h == Z_WINV + VW_ZOUT);
        bar_button(dc, VW_ZIN, "+", 0, 0, h == Z_WINV + VW_ZIN);
        bar_button(dc, VW_BACK, "BACK", RGB(255, 150, 40), 1, h == Z_WINV + VW_BACK);
        return;
    }
    char t[16];
    snprintf(t, sizeof t, "%d%%", drag_pct ? drag_pct : pct_now());
    button(dc, rail(VW_MINUS), "-", 0, h == Z_WINV + VW_MINUS);
    button(dc, rail(VW_SIZE), t, drag_pct != 0, h == Z_WINV + VW_SIZE);
    button(dc, rail(VW_PLUS), "+", 0, h == Z_WINV + VW_PLUS);
    button(dc, rail(VW_FIT), "FIT", settings_ui_size() == 0, h == Z_WINV + VW_FIT);
    button(dc, rail(VW_ZOOM), armed ? "CLICK A PANEL" : "PANEL ZOOM", armed, h == Z_WINV + VW_ZOOM);
    RECT g = grip();   /* the grip: three short diagonals in the corner */
    HPEN pen = CreatePen(PS_SOLID, 1, h == Z_WINV + VW_GRIP ? RGB(255, 150, 40) : RGB(240, 205, 150));
    HGDIOBJ was = SelectObject(dc, pen);
    for (int k = 1; k <= 3; k++) {
        int d = sc(4.0f * k);
        MoveToEx(dc, g.right - sc(2), g.bottom - sc(2) - d, NULL);
        LineTo(dc, g.right - sc(2) - d, g.bottom - sc(2));
    }
    SelectObject(dc, was);
    DeleteObject(pen);
}
const char *view_help(int z) {
    switch (z - Z_WINV) {
    case VW_MINUS: return "-: the whole window 10% smaller. Your host is asked to resize; if it can't, the size shows when the window next opens.";
    case VW_PLUS: return "+: the whole window 10% larger. Your host is asked to resize; if it can't, the size shows when the window next opens.";
    case VW_SIZE: return "The window's size, a percent of the panel's full size. Click: FIT.";
    case VW_FIT: return "FIT: as large as the screen allows, up to 100%. Lit: the window follows the screen.";
    case VW_ZOOM: return "PANEL ZOOM: click this, then a panel: it fills the window, sharp, and still works as usual. Or the wheel over an empty part of the panel, or with Ctrl or the middle button held, zooms at the pointer; hold the middle button and drag to pan.";
    case VW_SYN1: case VW_SYN1 + 1: case VW_SYN1 + 2: return "SYNTH: click to work on this Synth; double-click to solo it alone (again: solo off).";
    case VW_M1: case VW_M1 + 1: case VW_M1 + 2: return "M: mute this Synth.";
    case VW_S1: case VW_S1 + 1: case VW_S1 + 2: return "S: solo this Synth (several can be soloed).";
    case VW_ZIN: return "+: zoom in.";
    case VW_ZOUT: return "-: zoom out.";
    case VW_GRIP: return "Drag this corner to resize the whole window; it lands when you let go.";
    case VW_PREV: return "<: the panel before this one.";
    case VW_NEXT: return ">: the next panel.";
    case VW_BACK: return "BACK: the whole instrument again.";
    default: return NULL;
    }
}
#endif
