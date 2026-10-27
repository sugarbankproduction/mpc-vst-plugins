/* Anti-aliased drawing for the desktop editor (desktop/editor.c), through GDI+'s flat C API (gdiplus.dll, part of
 * Windows). The few functions used are declared here by hand, so no C++ header is needed. Colours are 0xAARRGGBB.
 * Angles: degrees, 0 = straight up, clockwise (as a knob turns). Call gfx_begin(dc) before drawing, gfx_end() after;
 * all drawing happens on the editor's UI thread. */
#pragma once
#include <math.h>
#include <windows.h>

typedef struct { float x, y; } gpt_t;
typedef struct { UINT32 version; void *debug_cb; BOOL no_thread, no_codecs; } gp_input_t;
int WINAPI GdiplusStartup(ULONG_PTR *token, const gp_input_t *in, void *out);
int WINAPI GdipCreateFromHDC(HDC dc, void **g);
int WINAPI GdipDeleteGraphics(void *g);
int WINAPI GdipSetSmoothingMode(void *g, int mode);
int WINAPI GdipCreateSolidFill(DWORD argb, void **brush);
int WINAPI GdipCreateLineBrush(const gpt_t *p1, const gpt_t *p2, DWORD c1, DWORD c2, int wrap, void **brush);
int WINAPI GdipDeleteBrush(void *brush);
int WINAPI GdipCreatePen1(DWORD argb, float width, int unit, void **pen);
int WINAPI GdipDeletePen(void *pen);
int WINAPI GdipSetPenStartCap(void *pen, int cap);
int WINAPI GdipSetPenEndCap(void *pen, int cap);
int WINAPI GdipFillEllipse(void *g, void *brush, float x, float y, float w, float h);
int WINAPI GdipDrawEllipse(void *g, void *pen, float x, float y, float w, float h);
int WINAPI GdipFillRectangle(void *g, void *brush, float x, float y, float w, float h);
int WINAPI GdipDrawRectangle(void *g, void *pen, float x, float y, float w, float h);
int WINAPI GdipDrawLine(void *g, void *pen, float x1, float y1, float x2, float y2);
int WINAPI GdipDrawLines(void *g, void *pen, const gpt_t *pts, int n);
int WINAPI GdipDrawArc(void *g, void *pen, float x, float y, float w, float h, float start, float sweep);
int WINAPI GdipFillPolygon(void *g, void *brush, const gpt_t *pts, int n, int fill_mode);

static void *gfx_g;   /* the GDI+ surface being drawn on */

static int gfx_startup(void) {   /* once per process, from the UI thread (never from DllMain) */
    static ULONG_PTR token;
    static int ok = -1;
    if (ok < 0) {
        gp_input_t in = {1, NULL, FALSE, FALSE};
        ok = GdiplusStartup(&token, &in, NULL) == 0;
    }
    return ok;
}
static void gfx_begin(HDC dc) {
    gfx_g = NULL;
    if (gfx_startup() && GdipCreateFromHDC(dc, &gfx_g) == 0) GdipSetSmoothingMode(gfx_g, 4 /* anti-alias */);
}
static void gfx_end(void) {
    if (gfx_g) GdipDeleteGraphics(gfx_g);
    gfx_g = NULL;
}

static void *gfx_pen(DWORD c, float w) {
    void *p = NULL;
    GdipCreatePen1(c, w, 2 /* pixels */, &p);
    if (p) { GdipSetPenStartCap(p, 2); GdipSetPenEndCap(p, 2); }   /* round ends */
    return p;
}
static void gfx_disc(float cx, float cy, float r, DWORD c) {
    void *b = NULL;
    if (!gfx_g || GdipCreateSolidFill(c, &b)) return;
    GdipFillEllipse(gfx_g, b, cx - r, cy - r, 2 * r, 2 * r);
    GdipDeleteBrush(b);
}
static void gfx_disc_grad(float cx, float cy, float r, DWORD c1, DWORD c2) {   /* c1 top-left .. c2 bottom-right */
    gpt_t a = {cx - r, cy - r}, z = {cx + r, cy + r};
    void *b = NULL;
    if (!gfx_g || GdipCreateLineBrush(&a, &z, c1, c2, 0, &b)) return;
    GdipFillEllipse(gfx_g, b, cx - r, cy - r, 2 * r, 2 * r);
    GdipDeleteBrush(b);
}
static void gfx_circle(float cx, float cy, float r, float w, DWORD c) {
    void *p = gfx_pen(c, w);
    if (!gfx_g || !p) return;
    GdipDrawEllipse(gfx_g, p, cx - r, cy - r, 2 * r, 2 * r);
    GdipDeletePen(p);
}
static void gfx_rect(float x, float y, float w, float h, DWORD c) {
    void *b = NULL;
    if (!gfx_g || GdipCreateSolidFill(c, &b)) return;
    GdipFillRectangle(gfx_g, b, x, y, w, h);
    GdipDeleteBrush(b);
}
static void gfx_rect_grad(float x, float y, float w, float h, DWORD c1, DWORD c2, int horizontal) {
    gpt_t a = {x, y}, z = {horizontal ? x + w : x, horizontal ? y : y + h};
    void *b = NULL;
    if (!gfx_g || GdipCreateLineBrush(&a, &z, c1, c2, 0, &b)) return;
    GdipFillRectangle(gfx_g, b, x, y, w, h);
    GdipDeleteBrush(b);
}
static void gfx_frame(float x, float y, float w, float h, float lw, DWORD c) {
    void *p = gfx_pen(c, lw);
    if (!gfx_g || !p) return;
    GdipDrawRectangle(gfx_g, p, x, y, w, h);
    GdipDeletePen(p);
}
static void gfx_line(float x0, float y0, float x1, float y1, float w, DWORD c) {
    void *p = gfx_pen(c, w);
    if (!gfx_g || !p) return;
    GdipDrawLine(gfx_g, p, x0, y0, x1, y1);
    GdipDeletePen(p);
}
static void gfx_lines(const gpt_t *pts, int n, float w, DWORD c) {
    void *p = gfx_pen(c, w);
    if (!gfx_g || !p) return;
    GdipDrawLines(gfx_g, p, pts, n);
    GdipDeletePen(p);
}
static void gfx_arc(float cx, float cy, float r, float a0, float a1, float w, DWORD c) {   /* from a0 to a1 */
    if (fabsf(a1 - a0) < 0.5f) return;
    void *p = gfx_pen(c, w);
    if (!gfx_g || !p) return;
    float lo = a0 < a1 ? a0 : a1;
    GdipDrawArc(gfx_g, p, cx - r, cy - r, 2 * r, 2 * r, lo - 90.0f, fabsf(a1 - a0));
    GdipDeletePen(p);
}
static void gfx_poly(const gpt_t *pts, int n, DWORD c) {
    void *b = NULL;
    if (!gfx_g || GdipCreateSolidFill(c, &b)) return;
    GdipFillPolygon(gfx_g, b, pts, n, 0);
    GdipDeleteBrush(b);
}
static gpt_t gfx_polar(float cx, float cy, float r, float deg) {   /* 0 = up, clockwise */
    float a = deg * 3.14159265f / 180.0f;
    gpt_t p = {cx + r * sinf(a), cy - r * cosf(a)};
    return p;
}
