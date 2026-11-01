/* The web build's GDI+: the flat-API calls the desktop editor declares by hand (desktop/gfx.h, cables.c, more_pics.c,
 * skin.c), drawn with plutovg on the same pixels as gdi.c. GDI+ puts pixel centres on whole coordinates (its default
 * pixel offset), so every drawing is shifted half a pixel to land where GDI+ draws it. A Graphics made from a DC
 * takes that DC's clip and origin as they are when each shape is drawn. Colours are 0xAARRGGBB, not premultiplied. */
#include <math.h>
#include <stdlib.h>
#include "gdi.h"
#include "gfx.h"

typedef struct { dc_t *dc; dc_t *own; int clip_on; float cx, cy, cw, ch; } gp_graphics_t;
typedef struct { int grad; DWORD c1, c2; gpt_t p1, p2; } gp_brush_t;
typedef struct { DWORD c; float w; int cap0, cap1; } gp_pen_t;
typedef struct { bitmap_t bm; } gp_image_t;

int WINAPI GdiplusStartup(ULONG_PTR *token, const gp_input_t *in, void *out) {
    if (token) *token = 1;
    return 0;
}
int WINAPI GdipCreateFromHDC(HDC dc, void **g) {
    gp_graphics_t *G = calloc(1, sizeof *G);
    if (!G || !dc) { free(G); return 1; }
    G->dc = (dc_t *)dc;
    *g = G;
    return 0;
}
int WINAPI GdipDeleteGraphics(void *g) {
    gp_graphics_t *G = g;
    if (!G) return 0;
    if (G->own) DeleteDC((HDC)G->own);
    free(G);
    return 0;
}
int WINAPI GdipSetSmoothingMode(void *g, int mode) { return 0; }   /* (always anti-aliased) */
int WINAPI GdipFlush(void *g, int intention) { return 0; }
int WINAPI GdipSetClipRect(void *g, float x, float y, float w, float h, int mode) {
    gp_graphics_t *G = g;
    G->clip_on = 1;
    G->cx = x; G->cy = y; G->cw = w; G->ch = h;
    return 0;
}
int WINAPI GdipResetClip(void *g) { ((gp_graphics_t *)g)->clip_on = 0; return 0; }

int WINAPI GdipCreateSolidFill(DWORD argb, void **brush) {
    gp_brush_t *b = calloc(1, sizeof *b);
    if (!b) return 1;
    b->c1 = argb;
    *brush = b;
    return 0;
}
int WINAPI GdipCreateLineBrush(const gpt_t *p1, const gpt_t *p2, DWORD c1, DWORD c2, int wrap, void **brush) {
    gp_brush_t *b = calloc(1, sizeof *b);
    if (!b) return 1;
    b->grad = 1;
    b->c1 = c1;
    b->c2 = c2;
    b->p1 = *p1;
    b->p2 = *p2;
    *brush = b;
    return 0;
}
int WINAPI GdipDeleteBrush(void *brush) { free(brush); return 0; }
int WINAPI GdipCreatePen1(DWORD argb, float width, int unit, void **pen) {
    gp_pen_t *p = calloc(1, sizeof *p);
    if (!p) return 1;
    p->c = argb;
    p->w = width;
    *pen = p;
    return 0;
}
int WINAPI GdipDeletePen(void *pen) { free(pen); return 0; }
int WINAPI GdipSetPenStartCap(void *pen, int cap) { ((gp_pen_t *)pen)->cap0 = cap; return 0; }
int WINAPI GdipSetPenEndCap(void *pen, int cap) { ((gp_pen_t *)pen)->cap1 = cap; return 0; }

int WINAPI GdipCreateBitmapFromScan0(int w, int h, int stride, int format, BYTE *scan0, void **bitmap) {
    if (w <= 0 || h <= 0) return 1;
    gp_image_t *im = calloc(1, sizeof *im);
    if (!im) return 1;
    im->bm.hdr.type = OB_BITMAP;
    if (scan0) {
        im->bm.s.px = (uint32_t *)scan0;
        im->bm.s.stride = stride / 4;
    } else {
        im->bm.s.px = calloc((size_t)w * h, 4);
        im->bm.s.stride = w;
        im->bm.s.owned = 1;
        if (!im->bm.s.px) { free(im); return 1; }
    }
    im->bm.s.w = w;
    im->bm.s.h = h;
    *bitmap = im;
    return 0;
}
int WINAPI GdipGetImageGraphicsContext(void *image, void **g) {
    gp_image_t *im = image;
    gp_graphics_t *G = calloc(1, sizeof *G);
    if (!G) return 1;
    G->own = (dc_t *)dc_for_surface(&im->bm.s, NULL);
    G->dc = G->own;
    if (!G->dc) { free(G); return 1; }
    *g = G;
    return 0;
}
int WINAPI GdipDisposeImage(void *image) {
    gp_image_t *im = image;
    if (!im) return 0;
    if (im->bm.s.canvas) plutovg_canvas_destroy(im->bm.s.canvas);
    if (im->bm.s.pv) plutovg_surface_destroy(im->bm.s.pv);
    if (im->bm.s.owned) free(im->bm.s.px);
    free(im);
    return 0;
}

/* ---- drawing ---- */
static plutovg_canvas_t *begin(gp_graphics_t *G) {
    if (!G || !G->dc) return NULL;
    plutovg_canvas_t *c = dc_canvas(G->dc);
    if (!c) return NULL;
    if (G->clip_on) plutovg_canvas_clip_rect(c, G->cx, G->cy, G->cw, G->ch);
    plutovg_canvas_translate(c, 0.5f, 0.5f);
    plutovg_canvas_new_path(c);
    return c;
}
static void end(gp_graphics_t *G) { dc_canvas_end(G->dc); }
static plutovg_color_t col(DWORD c) {
    return (plutovg_color_t){(c >> 16 & 0xFF) / 255.0f, (c >> 8 & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
                             (c >> 24 & 0xFF) / 255.0f};
}
static void set_brush(plutovg_canvas_t *c, const gp_brush_t *b) {
    if (!b->grad) {
        plutovg_color_t k = col(b->c1);
        plutovg_canvas_set_rgba(c, k.r, k.g, k.b, k.a);
        return;
    }
    plutovg_gradient_stop_t st[2] = {{0.0f, col(b->c1)}, {1.0f, col(b->c2)}};
    plutovg_canvas_set_linear_gradient(c, b->p1.x, b->p1.y, b->p2.x, b->p2.y, PLUTOVG_SPREAD_METHOD_PAD, st, 2, NULL);
}
static void stroke(plutovg_canvas_t *c, const gp_pen_t *p) {
    plutovg_color_t k = col(p->c);
    plutovg_canvas_set_rgba(c, k.r, k.g, k.b, k.a);
    plutovg_canvas_set_line_width(c, p->w);
    /* GDI+ caps: 0 flat, 1 square, 2 round (both ends set alike here) */
    plutovg_canvas_set_line_cap(c, p->cap0 == 2 || p->cap1 == 2 ? PLUTOVG_LINE_CAP_ROUND
                                   : p->cap0 == 1 ? PLUTOVG_LINE_CAP_SQUARE : PLUTOVG_LINE_CAP_BUTT);
    plutovg_canvas_stroke(c);
}
static void ellipse_path(plutovg_canvas_t *c, float x, float y, float w, float h) {
    plutovg_canvas_ellipse(c, x + w / 2, y + h / 2, w / 2, h / 2);
}

int WINAPI GdipFillEllipse(void *g, void *brush, float x, float y, float w, float h) {
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    ellipse_path(c, x, y, w, h);
    set_brush(c, brush);
    plutovg_canvas_fill(c);
    end(g);
    return 0;
}
int WINAPI GdipDrawEllipse(void *g, void *pen, float x, float y, float w, float h) {
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    ellipse_path(c, x, y, w, h);
    stroke(c, pen);
    end(g);
    return 0;
}
int WINAPI GdipFillRectangle(void *g, void *brush, float x, float y, float w, float h) {
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    plutovg_canvas_rect(c, x, y, w, h);
    set_brush(c, brush);
    plutovg_canvas_fill(c);
    end(g);
    return 0;
}
int WINAPI GdipDrawRectangle(void *g, void *pen, float x, float y, float w, float h) {
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    plutovg_canvas_rect(c, x, y, w, h);
    stroke(c, pen);
    end(g);
    return 0;
}
int WINAPI GdipDrawLine(void *g, void *pen, float x1, float y1, float x2, float y2) {
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    plutovg_canvas_move_to(c, x1, y1);
    plutovg_canvas_line_to(c, x2, y2);
    stroke(c, pen);
    end(g);
    return 0;
}
int WINAPI GdipDrawLines(void *g, void *pen, const gpt_t *pts, int n) {
    if (n < 2) return 0;
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    plutovg_canvas_move_to(c, pts[0].x, pts[0].y);
    for (int k = 1; k < n; k++) plutovg_canvas_line_to(c, pts[k].x, pts[k].y);
    stroke(c, pen);
    end(g);
    return 0;
}
int WINAPI GdipDrawArc(void *g, void *pen, float x, float y, float w, float h, float start, float sweep) {
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    /* degrees clockwise from 3 o'clock; on a y-down canvas plutovg's angles turn the same way */
    float cx = x + w / 2, cy = y + h / 2, r = w / 2;
    float a0 = start * 3.14159265f / 180.0f, a1 = (start + sweep) * 3.14159265f / 180.0f;
    if (fabsf(w - h) > 0.01f && h > 0) {
        plutovg_canvas_save(c);
        plutovg_canvas_translate(c, cx, cy);
        plutovg_canvas_scale(c, 1.0f, h / w);
        plutovg_canvas_arc(c, 0, 0, r, a0, a1, sweep < 0);
        plutovg_canvas_restore(c);
    } else {
        plutovg_canvas_arc(c, cx, cy, r, a0, a1, sweep < 0);
    }
    stroke(c, pen);
    end(g);
    return 0;
}
int WINAPI GdipFillPolygon(void *g, void *brush, const gpt_t *pts, int n, int fill_mode) {
    if (n < 3) return 0;
    plutovg_canvas_t *c = begin(g);
    if (!c) return 1;
    plutovg_canvas_move_to(c, pts[0].x, pts[0].y);
    for (int k = 1; k < n; k++) plutovg_canvas_line_to(c, pts[k].x, pts[k].y);
    plutovg_canvas_close_path(c);
    plutovg_canvas_set_fill_rule(c, fill_mode == 0 ? PLUTOVG_FILL_RULE_EVEN_ODD : PLUTOVG_FILL_RULE_NON_ZERO);
    set_brush(c, brush);
    plutovg_canvas_fill(c);
    end(g);
    return 0;
}
