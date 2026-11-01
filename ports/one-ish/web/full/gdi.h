/* The web build's GDI objects (gdi.c), shared with gdiplus.c and win32.c. */
#pragma once
#include <stdint.h>
#include "windows.h"
#include "plutovg.h"

enum { OB_BRUSH = 1, OB_PEN, OB_FONT, OB_BITMAP, OB_RGN };
typedef struct { int type, stock; } obj_t;

typedef struct {   /* 32-bit pixels, 0xAARRGGBB, top-down; stride in pixels */
    uint32_t *px;
    int w, h, stride, owned;
    plutovg_surface_t *pv;   /* plutovg's view of the same memory, made when first drawn on */
    plutovg_canvas_t *canvas;
} surf_t;

typedef struct { obj_t hdr; COLORREF c; int null, dc; } brush_t;
typedef struct { obj_t hdr; COLORREF c; int w, null, dc; } pen_t;
typedef struct { obj_t hdr; plutovg_font_face_t *face; float size, ascent, descent; int weight; } font_t;
typedef struct { obj_t hdr; surf_t s; } bitmap_t;
typedef struct { obj_t hdr; int n, cap; RECT *r; } rgn_t;

#define DC_SAVES 16
typedef struct {
    COLORREF text, bk;
    int bkmode;
    brush_t *brush;
    pen_t *pen;
    font_t *font;
    POINT org;
    rgn_t *clip;
} dc_state_t;

typedef struct {
    surf_t *s;          /* what it draws on: its bitmap's pixels, or a window's */
    HWND wnd;           /* a window's DC: that window */
    bitmap_t *bitmap;
    brush_t *brush;
    pen_t *pen;
    font_t *font;
    COLORREF text, bk, dc_brush, dc_pen;
    int bkmode;
    POINT org, cur;     /* viewport origin; MoveToEx's point */
    rgn_t *clip;        /* the DC's clip region (device coordinates), NULL: none */
    rgn_t *sys;         /* BeginPaint's: the part to paint (GetRandomRgn SYSRGN) */
    dc_state_t saved[DC_SAVES];
    int nsaved;
} dc_t;

HDC dc_for_surface(surf_t *s, HWND w);
void dc_set_sysrgn(HDC dc, const rgn_t *r);
int dc_clip_rects(dc_t *d, RECT *out, int max);
plutovg_canvas_t *dc_canvas(dc_t *d);
void dc_canvas_end(dc_t *d);
rgn_t *rgn_make(void);
void rgn_add_rect(rgn_t *g, const RECT *r);
void rgn_free(rgn_t *g);
