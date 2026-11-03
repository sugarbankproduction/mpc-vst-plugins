/* The window's size and PANEL ZOOM (2026-11-03, owner), on the rail under the keys; the plugin window only (the web page has
 * its own camera).
 *
 * SIZE: - and + (10% a click), the corner's grip (drag it; it lands when you let go) and FIT (as large as the screen allows,
 * never over 100%, as it always was). Settings.txt keeps it (window_size). The host is asked to resize its
 * window (audioMasterSizeWindow): a host's own frame can't resize a VST2, this is the plugin's way. A host that says no
 * shows the new size when the window next opens.
 *
 * PANEL ZOOM: click it, then a panel: the panel fills the window, drawn at the larger size, so sharp. < > the panels in turn,
 * BACK the whole instrument. The whole panel is laid out at the larger scale and this window, a child of the host's, grows
 * with it and slides so the panel shows: the host's window clips the rest. So every click, repaint and menu works in the same
 * coordinates as ever, nothing else knows. A zoom step is one of a few sizes: another panel at the same one only slides.
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
const char *view_help(int z) { (void)z; return NULL; }
int winv_rect(int z, RECT *r) { (void)z; (void)r; return 0; }
#else

static float zoom = 1.0f;
static int ox, oy, panel = -1, armed, drag_pct, drag_dx;
static const float STEPS[] = {1.25f, 1.5f, 1.75f, 2.0f, 2.4f};
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
    ox = x;
    oy = y;
    ed_each(place);
}
static void zoom_off(void) {
    panel = -1;
    armed = 0;
    if (zoom != 1.0f || ox || oy) zoom_to(1.0f, 0, 0);
}
void view_open(void) {   /* a window opens: the whole instrument */
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
    float s0 = (float)L.vw / BASE_W, bar = 18.0f * s0;
    float fit = fminf((float)L.vw / (w * s0), ((float)L.vh - bar) / (h * s0)) * 0.97f, k = 1.0f;
    for (unsigned j = 0; j < sizeof STEPS / sizeof STEPS[0]; j++)
        if (STEPS[j] <= fit && s0 * STEPS[j] <= MAX_SCALE) k = STEPS[j];
    panel = i;
    if (k != zoom) {
        zoom = k;
        editor_relayout();
    }
    float s = L.s;
    int vx = (int)lroundf((x + w * 0.5f) * s - L.vw * 0.5f), vy = (int)lroundf((y + h * 0.5f) * s - (L.vh + bar) * 0.5f);
    int mx = L.w - L.vw, my = L.h - L.vh;
    zoom_to(k, vx < 0 ? 0 : vx > mx ? mx : vx, vy < 0 ? 0 : vy > my ? my : vy);
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
static RECT bar(int part) {   /* zoomed: at the top right of what shows */
    static const short W[] = {18, 150, 18, 48};
    int right = ox + L.vw - sc(4), top = oy + sc(2), h = sc(14), gap = sc(3);
    int x = right;
    for (int k = 3; k >= part; k--) {
        x -= sc(W[k]);
        if (k > part) x -= gap;
    }
    RECT r = {x, top, x + sc(W[part]), top + h};
    return r;
}
int winv_rect(int z, RECT *r) {
    int k = z - Z_WINV;
    if (k < 0 || k >= VW_COUNT) return 0;
    if (zoom != 1.0f) {
        if (k < VW_PREV) return 0;
        *r = bar(k - VW_PREV);
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
    return -1;
}
static int pct_now(void) { return (int)lroundf(100.0f * L.vw / BASE_W); }
static void repaint(ed_t *e) {
    RECT r;
    for (int z = Z_WINV; z < Z_WINV + VW_COUNT; z++)
        if (winv_rect(z, &r)) InvalidateRect(e->wnd, &r, FALSE);
}
int view_press(ed_t *e, int z, int x, int y) {   /* 1: the press was ours */
    int k = z - Z_WINV;
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
    default: break;
    }
    return 1;
}
void view_drag(ed_t *e, int x, int y) {
    (void)y;
    int p = (int)lroundf(100.0f * (float)(x + drag_dx) / BASE_W / 5.0f) * 5;
    p = p < MIN_PCT ? MIN_PCT : p > MAX_PCT ? MAX_PCT : p;
    SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
    if (p == drag_pct) return;
    drag_pct = p;
    RECT r = rail(VW_SIZE);
    InvalidateRect(e->wnd, &r, FALSE);
}
void view_release(ed_t *e, int commit) {
    e->mode = M_NONE;
    int p = drag_pct;
    drag_pct = 0;
    if (commit && p != pct_now()) settings_ui_size_set(e, p);
    else repaint(e);
}

/* ---- drawing, over everything ---- */
static void button(HDC dc, RECT r, const char *t, int lit, int hot) {
    HBRUSH b = CreateSolidBrush(lit ? RGB(255, 150, 40) : hot ? RGB(0x5A, 0x34, 0x10) : RGB(0x1C, 0x14, 0x0E));
    FillRect(dc, &r, b);
    DeleteObject(b);
    b = CreateSolidBrush(RGB(255, 150, 40));
    FrameRect(dc, &r, b);
    DeleteObject(b);
    SelectObject(dc, font_tiny);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, lit ? RGB(0, 0, 0) : RGB(240, 205, 150));
    DrawTextA(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
void view_draw(HDC dc, ed_t *e) {
    int h = e->hover;
    RECT clip, area, tmp;   /* (a repaint elsewhere: nothing to do) */
    GetClipBox(dc, &clip);
    if (zoom != 1.0f) SetRect(&area, bar(0).left, bar(0).top, bar(3).right, bar(3).bottom);
    else SetRect(&area, rail(VW_MINUS).left, grip().top, L.w, L.h);
    if (!IntersectRect(&tmp, &clip, &area)) return;
    if (zoom != 1.0f) {
        char title[64] = "KEYBOARD";
        if (panel >= 0 && panel < NPANELS) {
            snprintf(title, sizeof title, "%s", PANELS[panel].title);
            for (char *c = title; *c; c++) if (*c == '|') *c = '/';
        }
        button(dc, bar(0), "<", 0, h == Z_WINV + VW_PREV);
        button(dc, bar(1), title, 0, 0);
        button(dc, bar(2), ">", 0, h == Z_WINV + VW_NEXT);
        button(dc, bar(3), "BACK", 1, h == Z_WINV + VW_BACK);
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
    case VW_ZOOM: return "PANEL ZOOM: click this, then a panel: it fills the window, sharp, and still works as usual. < > the next panels, BACK the whole instrument.";
    case VW_GRIP: return "Drag this corner to resize the whole window; it lands when you let go.";
    case VW_PREV: return "<: the panel before this one.";
    case VW_NEXT: return ">: the next panel.";
    case VW_BACK: return "BACK: the whole instrument again.";
    default: return NULL;
    }
}
#endif
