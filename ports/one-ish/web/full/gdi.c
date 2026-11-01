/* The web build's GDI (win32/windows.h): device contexts, bitmaps, brushes, pens, fonts, text, regions and BitBlt, as
 * the desktop editor uses them. Every surface is 32-bit pixels in memory (0xAARRGGBB, top-down rows), the layout of a
 * Windows 32-bit DIB, so the editor's own pixel code (skin.c, cables.c) works on it unchanged. Shapes and text are
 * drawn with plutovg (third_party/plutovg) into the same memory; text uses the Selawik fonts (web/full/fonts, open
 * stand-ins for Segoe UI). Single-threaded: all of this runs on the editor thread (host.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "gdi.h"

/* ---- regions: a list of non-overlapping rectangles ---- */
static rgn_t *rgn_new(void) {
    rgn_t *g = calloc(1, sizeof *g);
    if (g) g->hdr.type = OB_RGN;
    return g;
}
static void rgn_push(rgn_t *g, RECT r) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    if (g->n == g->cap) {
        int cap = g->cap ? g->cap * 2 : 8;
        RECT *nr = realloc(g->r, cap * sizeof *nr);
        if (!nr) return;
        g->r = nr;
        g->cap = cap;
    }
    g->r[g->n++] = r;
}
static void rgn_clear(rgn_t *g) { g->n = 0; }
static void rgn_copy(rgn_t *d, const rgn_t *s) {
    if (d == s) return;
    rgn_clear(d);
    for (int k = 0; k < s->n; k++) rgn_push(d, s->r[k]);
}
static void sub_rect(rgn_t *out, RECT a, const RECT *b) {   /* a minus b, up to four pieces */
    RECT i;
    if (!IntersectRect(&i, &a, b)) { rgn_push(out, a); return; }
    rgn_push(out, (RECT){a.left, a.top, a.right, i.top});
    rgn_push(out, (RECT){a.left, i.bottom, a.right, a.bottom});
    rgn_push(out, (RECT){a.left, i.top, i.left, i.bottom});
    rgn_push(out, (RECT){i.right, i.top, a.right, i.bottom});
}
static void rgn_sub(rgn_t *d, const rgn_t *a, const rgn_t *b) {   /* d = a - b (d may be a) */
    rgn_t cur = {0}, nxt = {0};
    for (int k = 0; k < a->n; k++) rgn_push(&cur, a->r[k]);
    for (int j = 0; j < b->n; j++) {
        rgn_clear(&nxt);
        for (int k = 0; k < cur.n; k++) sub_rect(&nxt, cur.r[k], &b->r[j]);
        rgn_t t = cur; cur = nxt; nxt = t;
    }
    rgn_copy(d, &cur);
    free(cur.r);
    free(nxt.r);
}
static void rgn_and(rgn_t *d, const rgn_t *a, const rgn_t *b) {
    rgn_t out = {0};
    RECT i;
    for (int k = 0; k < a->n; k++)
        for (int j = 0; j < b->n; j++)
            if (IntersectRect(&i, &a->r[k], &b->r[j])) rgn_push(&out, i);
    rgn_copy(d, &out);
    free(out.r);
}
static void rgn_or(rgn_t *d, const rgn_t *a, const rgn_t *b) {
    rgn_t out = {0}, extra = {0};
    rgn_copy(&out, a);
    rgn_sub(&extra, b, a);
    for (int k = 0; k < extra.n; k++) rgn_push(&out, extra.r[k]);
    rgn_copy(d, &out);
    free(out.r);
    free(extra.r);
}
static int rgn_kind(const rgn_t *g) { return g->n == 0 ? NULLREGION : g->n == 1 ? SIMPLEREGION : COMPLEXREGION; }
static RECT rgn_box(const rgn_t *g) {
    RECT b = {0, 0, 0, 0};
    for (int k = 0; k < g->n; k++) {
        if (k == 0) b = g->r[0];
        else UnionRect(&b, &b, &g->r[k]);
    }
    return b;
}
void rgn_add_rect(rgn_t *g, const RECT *r) {   /* union with r (the update region: win32.c) */
    rgn_t one = {0};
    rgn_push(&one, *r);
    rgn_or(g, g, &one);
    free(one.r);
    if (g->n > 32) {   /* many pieces: their bounds (a little more painted, nothing left out) */
        RECT b = rgn_box(g);
        rgn_clear(g);
        rgn_push(g, b);
    }
}
void rgn_free(rgn_t *g) {
    if (!g) return;
    free(g->r);
    free(g);
}
rgn_t *rgn_make(void) { return rgn_new(); }

HRGN CreateRectRgn(int l, int t, int r, int b) {
    rgn_t *g = rgn_new();
    if (g) rgn_push(g, (RECT){l, t, r, b});
    return g;
}
HRGN CreateRectRgnIndirect(const RECT *r) { return CreateRectRgn(r->left, r->top, r->right, r->bottom); }
BOOL SetRectRgn(HRGN h, int l, int t, int r, int b) {
    rgn_t *g = h;
    rgn_clear(g);
    rgn_push(g, (RECT){l, t, r, b});
    return TRUE;
}
int CombineRgn(HRGN d, HRGN a, HRGN b, int mode) {
    rgn_t *D = d, *A = a, *B = b;
    switch (mode) {
    case RGN_AND: rgn_and(D, A, B); break;
    case RGN_OR: rgn_or(D, A, B); break;
    case RGN_DIFF: rgn_sub(D, A, B); break;
    case RGN_COPY: rgn_copy(D, A); break;
    case RGN_XOR: {
        rgn_t x = {0}, y = {0};
        rgn_sub(&x, A, B);
        rgn_sub(&y, B, A);
        rgn_or(D, &x, &y);
        free(x.r);
        free(y.r);
        break;
    }
    default: return ERROR;
    }
    return rgn_kind(D);
}
int OffsetRgn(HRGN h, int dx, int dy) {
    rgn_t *g = h;
    for (int k = 0; k < g->n; k++) OffsetRect(&g->r[k], dx, dy);
    return rgn_kind(g);
}
int GetRgnBox(HRGN h, LPRECT box) {
    *box = rgn_box(h);
    return rgn_kind(h);
}
DWORD GetRegionData(HRGN h, DWORD n, RGNDATA *d) {
    rgn_t *g = h;
    DWORD need = (DWORD)(sizeof(RGNDATAHEADER) + g->n * sizeof(RECT));
    if (!d) return need;
    if (n < need) return 0;
    d->rdh.dwSize = sizeof(RGNDATAHEADER);
    d->rdh.iType = 1;
    d->rdh.nCount = g->n;
    d->rdh.nRgnSize = g->n * sizeof(RECT);
    d->rdh.rcBound = rgn_box(g);
    memcpy(d->Buffer, g->r, g->n * sizeof(RECT));
    return need;
}
BOOL PtInRegion(HRGN h, int x, int y) {
    rgn_t *g = h;
    for (int k = 0; k < g->n; k++)
        if (PtInRect(&g->r[k], (POINT){x, y})) return TRUE;
    return FALSE;
}
BOOL RectInRegion(HRGN h, const RECT *rc) {
    rgn_t *g = h;
    RECT i;
    for (int k = 0; k < g->n; k++)
        if (IntersectRect(&i, &g->r[k], rc)) return TRUE;
    return FALSE;
}

/* ---- objects ---- */
static brush_t STOCK_BRUSH[2] = {{{OB_BRUSH, 1}, RGB(255, 255, 255), 0}, {{OB_BRUSH, 1}, RGB(0, 0, 0), 0}};
static brush_t STOCK_NULL_BRUSH = {{OB_BRUSH, 1}, 0, 1};
static pen_t STOCK_NULL_PEN = {{OB_PEN, 1}, 0, 0, 1};
static pen_t STOCK_BLACK_PEN = {{OB_PEN, 1}, 0, 1, 0};
static brush_t STOCK_DC_BRUSH = {{OB_BRUSH, 1}, 0, 0, 1};
static pen_t STOCK_DC_PEN = {{OB_PEN, 1}, 0, 1, 0, 1};
static font_t *STOCK_FONT;
static bitmap_t STOCK_BITMAP = {{OB_BITMAP, 1}};

HGDIOBJ GetStockObject(int i) {
    switch (i) {
    case WHITE_BRUSH: return &STOCK_BRUSH[0];
    case BLACK_BRUSH: return &STOCK_BRUSH[1];
    case NULL_BRUSH: return &STOCK_NULL_BRUSH;
    case NULL_PEN: return &STOCK_NULL_PEN;
    case DC_BRUSH: return &STOCK_DC_BRUSH;
    case DC_PEN: return &STOCK_DC_PEN;
    case DEFAULT_GUI_FONT:
        if (!STOCK_FONT) {
            STOCK_FONT = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Segoe UI");
            if (STOCK_FONT) STOCK_FONT->hdr.stock = 1;
        }
        return STOCK_FONT;
    }
    return &STOCK_BLACK_PEN;
}
HBRUSH CreateSolidBrush(COLORREF c) {
    brush_t *b = calloc(1, sizeof *b);
    if (b) { b->hdr.type = OB_BRUSH; b->c = c; }
    return b;
}
HPEN CreatePen(int style, int w, COLORREF c) {
    pen_t *p = calloc(1, sizeof *p);
    if (p) { p->hdr.type = OB_PEN; p->c = c; p->w = w < 1 ? 1 : w; p->null = style == PS_NULL; }
    return p;
}
BOOL DeleteObject(HGDIOBJ o) {
    obj_t *h = o;
    if (!h || h->stock) return TRUE;
    switch (h->type) {
    case OB_RGN: rgn_free(o); return TRUE;
    case OB_BITMAP: {
        bitmap_t *b = o;
        if (b->s.canvas) plutovg_canvas_destroy(b->s.canvas);
        if (b->s.pv) plutovg_surface_destroy(b->s.pv);
        if (b->s.owned) free(b->s.px);
        break;
    }
    }
    free(o);
    return TRUE;
}
int GetObjectA(HGDIOBJ o, int n, LPVOID out) {
    obj_t *h = o;
    if (!h || h->type != OB_BITMAP || n < (int)sizeof(BITMAP)) return 0;
    bitmap_t *b = o;
    BITMAP *bm = out;
    memset(bm, 0, sizeof *bm);
    bm->bmWidth = b->s.w;
    bm->bmHeight = b->s.h;
    bm->bmWidthBytes = b->s.stride * 4;
    bm->bmPlanes = 1;
    bm->bmBitsPixel = 32;
    bm->bmBits = b->s.px;
    return sizeof(BITMAP);
}

/* ---- fonts: Selawik, by weight ---- */
static plutovg_font_face_t *FACES[5];   /* light, semilight, regular, semibold, bold */
static const char *const FACE_FILES[5] = {"/fonts/selawkl.ttf", "/fonts/selawksl.ttf", "/fonts/selawk.ttf",
                                          "/fonts/selawksb.ttf", "/fonts/selawkb.ttf"};
static plutovg_font_face_t *face_for(int weight) {
    int k = weight <= 300 ? 0 : weight <= 350 ? 1 : weight < 600 ? 2 : weight < 700 ? 3 : 4;
    if (!FACES[k]) FACES[k] = plutovg_font_face_load_from_file(FACE_FILES[k], 0);
    if (!FACES[k] && k != 2) return face_for(400);
    return FACES[k];
}
HFONT CreateFontA(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline, DWORD strike,
                  DWORD charset, DWORD out_prec, DWORD clip_prec, DWORD quality, DWORD pitch, LPCSTR face) {
    font_t *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->hdr.type = OB_FONT;
    f->face = face_for(weight ? weight : FW_NORMAL);
    if (!f->face) { free(f); return NULL; }
    float asc, desc, gap;
    plutovg_font_face_get_metrics(f->face, 1.0f, &asc, &desc, &gap, NULL);   /* (desc is negative) */
    float cell = asc - desc;
    /* negative: the character height (the em, as GDI means it); positive: the cell height */
    f->size = h < 0 ? (float)-h : h > 0 ? h / cell : 12.0f;
    f->ascent = ceilf(asc * f->size);
    f->descent = ceilf(-desc * f->size);
    f->weight = weight;
    return f;
}

/* ---- device contexts ---- */
static dc_t *dc_new(void) {
    dc_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->text = RGB(0, 0, 0);
    d->bk = RGB(255, 255, 255);
    d->bkmode = OPAQUE;
    d->brush = &STOCK_BRUSH[0];
    d->pen = &STOCK_BLACK_PEN;
    d->dc_brush = RGB(255, 255, 255);
    d->clip = NULL;   /* none: the whole surface */
    return d;
}
HDC dc_for_surface(surf_t *s, HWND w) {
    dc_t *d = dc_new();
    if (!d) return NULL;
    d->s = s;
    d->wnd = w;
    return (HDC)d;
}
void dc_set_sysrgn(HDC h, const rgn_t *r) {
    dc_t *d = (dc_t *)h;
    if (!d->sys) d->sys = rgn_new();
    if (d->sys) rgn_copy(d->sys, r);
}
HDC CreateCompatibleDC(HDC dc) { return (HDC)dc_new(); }
BOOL DeleteDC(HDC h) {
    dc_t *d = (dc_t *)h;
    if (!d) return FALSE;
    for (int k = 0; k < d->nsaved; k++) rgn_free(d->saved[k].clip);
    rgn_free(d->clip);
    rgn_free(d->sys);
    free(d);
    return TRUE;
}
HWND WindowFromDC(HDC h) { return h ? ((dc_t *)h)->wnd : NULL; }

HGDIOBJ SelectObject(HDC h, HGDIOBJ o) {
    dc_t *d = (dc_t *)h;
    obj_t *ob = o;
    if (!d || !ob) return NULL;
    void *old = NULL;
    switch (ob->type) {
    case OB_BRUSH: old = d->brush; d->brush = o; break;
    case OB_PEN: old = d->pen; d->pen = o; break;
    case OB_FONT: old = d->font; d->font = o; break;
    case OB_BITMAP:   /* a memory DC starts with the stock 1x1 bitmap: selecting it back leaves no surface */
        old = d->bitmap ? (void *)d->bitmap : (void *)&STOCK_BITMAP;
        d->bitmap = o == &STOCK_BITMAP ? NULL : o;
        d->s = d->bitmap ? &d->bitmap->s : NULL;
        break;
    case OB_RGN: SelectClipRgn(h, o); return (HGDIOBJ)(intptr_t)SIMPLEREGION;
    }
    return old;
}
HGDIOBJ GetCurrentObject(HDC h, UINT type) { return type == OBJ_BITMAP ? ((dc_t *)h)->bitmap : NULL; }

int SaveDC(HDC h) {
    dc_t *d = (dc_t *)h;
    if (d->nsaved >= DC_SAVES) return 0;
    dc_state_t *st = &d->saved[d->nsaved++];
    st->text = d->text;
    st->bk = d->bk;
    st->bkmode = d->bkmode;
    st->brush = d->brush;
    st->pen = d->pen;
    st->font = d->font;
    st->org = d->org;
    st->clip = NULL;
    if (d->clip) { st->clip = rgn_new(); if (st->clip) rgn_copy(st->clip, d->clip); }
    return d->nsaved;
}
BOOL RestoreDC(HDC h, int n) {
    dc_t *d = (dc_t *)h;
    if (n < 0) n = d->nsaved + 1 + n;
    if (n < 1 || n > d->nsaved) return FALSE;
    dc_state_t *st = &d->saved[n - 1];
    d->text = st->text;
    d->bk = st->bk;
    d->bkmode = st->bkmode;
    d->brush = st->brush;
    d->pen = st->pen;
    d->font = st->font;
    d->org = st->org;
    rgn_free(d->clip);
    d->clip = st->clip;
    st->clip = NULL;
    for (int k = n; k < d->nsaved; k++) rgn_free(d->saved[k].clip);
    d->nsaved = n - 1;
    return TRUE;
}

COLORREF SetTextColor(HDC h, COLORREF c) { dc_t *d = (dc_t *)h; COLORREF o = d->text; d->text = c; return o; }
COLORREF GetTextColor(HDC h) { return ((dc_t *)h)->text; }
COLORREF SetBkColor(HDC h, COLORREF c) { dc_t *d = (dc_t *)h; COLORREF o = d->bk; d->bk = c; return o; }
int SetBkMode(HDC h, int m) { dc_t *d = (dc_t *)h; int o = d->bkmode; d->bkmode = m; return o; }
COLORREF SetDCBrushColor(HDC h, COLORREF c) { dc_t *d = (dc_t *)h; COLORREF o = d->dc_brush; d->dc_brush = c; return o; }
COLORREF SetDCPenColor(HDC h, COLORREF c) { dc_t *d = (dc_t *)h; COLORREF o = d->dc_pen; d->dc_pen = c; return o; }
BOOL SetViewportOrgEx(HDC h, int x, int y, LPPOINT old) {
    dc_t *d = (dc_t *)h;
    if (old) *old = d->org;
    d->org = (POINT){x, y};
    return TRUE;
}
BOOL GetViewportOrgEx(HDC h, LPPOINT p) { *p = ((dc_t *)h)->org; return TRUE; }
BOOL OffsetViewportOrgEx(HDC h, int x, int y, LPPOINT old) {
    dc_t *d = (dc_t *)h;
    if (old) *old = d->org;
    d->org.x += x;
    d->org.y += y;
    return TRUE;
}
int SetGraphicsMode(HDC h, int m) { return 1; }
BOOL SetWorldTransform(HDC h, const XFORM *x) { return FALSE; }
int GetDeviceCaps(HDC h, int i) { return i == LOGPIXELSX || i == LOGPIXELSY ? 96 : 0; }

/* ---- clipping (device coordinates) ---- */
static void ensure_clip(dc_t *d) {   /* an explicit clip region, starting from the whole surface */
    if (d->clip) return;
    d->clip = rgn_new();
    if (d->clip && d->s) rgn_push(d->clip, (RECT){0, 0, d->s->w, d->s->h});
}
int SelectClipRgn(HDC h, HRGN r) {
    dc_t *d = (dc_t *)h;
    if (!r) { rgn_free(d->clip); d->clip = NULL; return SIMPLEREGION; }
    ensure_clip(d);
    rgn_copy(d->clip, r);
    return rgn_kind(d->clip);
}
int ExtSelectClipRgn(HDC h, HRGN r, int mode) {
    dc_t *d = (dc_t *)h;
    if (mode == RGN_COPY) return SelectClipRgn(h, r);
    ensure_clip(d);
    return CombineRgn(d->clip, d->clip, r, mode);
}
int GetClipRgn(HDC h, HRGN r) {
    dc_t *d = (dc_t *)h;
    if (!d->clip) return 0;
    rgn_copy(r, d->clip);
    return 1;
}
int GetRandomRgn(HDC h, HRGN r, INT i) {   /* SYSRGN: what BeginPaint is to paint (the window sits at 0, 0) */
    dc_t *d = (dc_t *)h;
    if (!d->sys) return 0;
    rgn_copy(r, d->sys);
    return 1;
}
int IntersectClipRect(HDC h, int l, int t, int r, int b) {
    dc_t *d = (dc_t *)h;
    ensure_clip(d);
    rgn_t one = {0};
    rgn_push(&one, (RECT){l + d->org.x, t + d->org.y, r + d->org.x, b + d->org.y});
    rgn_and(d->clip, d->clip, &one);
    free(one.r);
    return rgn_kind(d->clip);
}
int ExcludeClipRect(HDC h, int l, int t, int r, int b) {
    dc_t *d = (dc_t *)h;
    ensure_clip(d);
    rgn_t one = {0};
    rgn_push(&one, (RECT){l + d->org.x, t + d->org.y, r + d->org.x, b + d->org.y});
    rgn_sub(d->clip, d->clip, &one);
    free(one.r);
    return rgn_kind(d->clip);
}
int GetClipBox(HDC h, LPRECT r) {
    dc_t *d = (dc_t *)h;
    RECT b = d->clip ? rgn_box(d->clip) : (RECT){0, 0, d->s ? d->s->w : 0, d->s ? d->s->h : 0};
    if (d->sys) {
        RECT s = rgn_box(d->sys);
        if (!IntersectRect(&b, &b, &s)) SetRectEmpty(&b);
    }
    OffsetRect(&b, -d->org.x, -d->org.y);
    *r = b;
    return IsRectEmpty(&b) ? NULLREGION : SIMPLEREGION;
}

/* the rectangles drawing may touch, in device coordinates: surface ∩ clip ∩ system region */
static int clip_rects(dc_t *d, RECT *out, int max);
int dc_clip_rects(dc_t *d, RECT *out, int max) {   /* (more pieces than fit: their bounds, never fewer pixels) */
    int n = clip_rects(d, out, max);
    if (n < max) return n;
    static RECT big[4096];
    int all = clip_rects(d, big, 4096);
    RECT b = big[0];
    for (int k = 1; k < all; k++) UnionRect(&b, &b, &big[k]);
    out[0] = b;
    return all ? 1 : 0;
}
static int clip_rects(dc_t *d, RECT *out, int max) {
    if (!d->s) return 0;
    RECT surf = {0, 0, d->s->w, d->s->h}, i;
    int n = 0;
    const rgn_t *a = d->clip, *b = d->sys;
    if (!a && !b) { out[0] = surf; return 1; }
    if (a && b) {
        for (int k = 0; k < a->n && n < max; k++)
            for (int j = 0; j < b->n && n < max; j++)
                if (IntersectRect(&i, &a->r[k], &b->r[j]) && IntersectRect(&i, &i, &surf)) out[n++] = i;
        return n;
    }
    const rgn_t *g = a ? a : b;
    for (int k = 0; k < g->n && n < max; k++)
        if (IntersectRect(&i, &g->r[k], &surf)) out[n++] = i;
    return n;
}

/* a plutovg canvas on the DC's surface, clipped to the DC (released with dc_canvas_end) */
plutovg_canvas_t *dc_canvas(dc_t *d) {
    surf_t *s = d->s;
    if (!s || !s->px) return NULL;
    if (!s->pv) s->pv = plutovg_surface_create_for_data((unsigned char *)s->px, s->w, s->h, s->stride * 4);
    if (s->pv && !s->canvas) s->canvas = plutovg_canvas_create(s->pv);
    if (!s->canvas) return NULL;
    plutovg_canvas_t *c = s->canvas;
    plutovg_canvas_save(c);
    RECT rs[64];
    int n = dc_clip_rects(d, rs, 64);
    if (n == 0) plutovg_canvas_clip_rect(c, 0, 0, 0, 0);
    else if (!(n == 1 && rs[0].left == 0 && rs[0].top == 0 && rs[0].right == s->w && rs[0].bottom == s->h)) {
        plutovg_canvas_new_path(c);
        for (int k = 0; k < n; k++)
            plutovg_canvas_rect(c, rs[k].left, rs[k].top, rs[k].right - rs[k].left, rs[k].bottom - rs[k].top);
        plutovg_canvas_clip(c);
    }
    plutovg_canvas_translate(c, d->org.x, d->org.y);
    return c;
}
void dc_canvas_end(dc_t *d) {
    if (d->s && d->s->canvas) plutovg_canvas_restore(d->s->canvas);
}

/* ---- bitmaps ---- */
static bitmap_t *bitmap_new(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    bitmap_t *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->hdr.type = OB_BITMAP;
    b->s.px = calloc((size_t)w * h, 4);
    if (!b->s.px) { free(b); return NULL; }
    b->s.w = b->s.stride = w;
    b->s.h = h;
    b->s.owned = 1;
    return b;
}
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT usage, void **bits, HANDLE sec, DWORD off) {
    const BITMAPINFOHEADER *h = &bi->bmiHeader;
    if (h->biBitCount != 32) return NULL;   /* (the editor only makes 32-bit top-down DIBs) */
    bitmap_t *b = bitmap_new(h->biWidth, h->biHeight < 0 ? -h->biHeight : h->biHeight);
    if (b && bits) *bits = b->s.px;
    return b;
}
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h) { return bitmap_new(w, h); }
int GetDIBits(HDC dc, HBITMAP hb, UINT start, UINT lines, LPVOID bits, BITMAPINFO *bi, UINT usage) {
    bitmap_t *b = hb;
    BITMAPINFOHEADER *h = &bi->bmiHeader;
    if (!b || !bits) return 0;
    int top_down = h->biHeight < 0, bpp = h->biBitCount, w = b->s.w;
    int row = ((w * bpp / 8) + 3) & ~3;
    for (UINT y = 0; y < lines && (int)(start + y) < b->s.h; y++) {
        int sy = top_down ? (int)(start + y) : b->s.h - 1 - (int)(start + y);
        const uint32_t *src = b->s.px + (size_t)sy * b->s.stride;
        BYTE *dst = (BYTE *)bits + (size_t)y * row;
        for (int x = 0; x < w; x++) {
            uint32_t p = src[x];
            if (bpp == 32) { memcpy(dst + 4 * x, &p, 4); continue; }
            dst[3 * x] = p & 0xFF;
            dst[3 * x + 1] = p >> 8 & 0xFF;
            dst[3 * x + 2] = p >> 16 & 0xFF;
        }
    }
    return (int)lines;
}

/* ---- solid fills ---- */
static uint32_t px_of(COLORREF c) { return 0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF); }
static void fill_dev(dc_t *d, RECT r, uint32_t v) {   /* r in device coordinates */
    RECT rs[64], i;
    int n = dc_clip_rects(d, rs, 64);
    for (int k = 0; k < n; k++) {
        if (!IntersectRect(&i, &rs[k], &r)) continue;
        for (int y = i.top; y < i.bottom; y++) {
            uint32_t *row = d->s->px + (size_t)y * d->s->stride;
            for (int x = i.left; x < i.right; x++) row[x] = v;
        }
    }
}
static int brush_px(dc_t *d, HBRUSH hb, uint32_t *v) {
    brush_t *b = hb;
    if (!b || b->null) return 0;
    *v = px_of(b->dc ? d->dc_brush : b->c);
    return 1;
}
int FillRect(HDC h, const RECT *r, HBRUSH hb) {
    dc_t *d = (dc_t *)h;
    uint32_t v;
    if (!d->s || !brush_px(d, hb, &v)) return 0;
    RECT dr = *r;
    OffsetRect(&dr, d->org.x, d->org.y);
    fill_dev(d, dr, v);
    return 1;
}
int FrameRect(HDC h, const RECT *r, HBRUSH hb) {
    dc_t *d = (dc_t *)h;
    uint32_t v;
    if (!d->s || !brush_px(d, hb, &v) || r->right <= r->left || r->bottom <= r->top) return 0;
    RECT dr = *r;
    OffsetRect(&dr, d->org.x, d->org.y);
    fill_dev(d, (RECT){dr.left, dr.top, dr.right, dr.top + 1}, v);
    fill_dev(d, (RECT){dr.left, dr.bottom - 1, dr.right, dr.bottom}, v);
    fill_dev(d, (RECT){dr.left, dr.top, dr.left + 1, dr.bottom}, v);
    fill_dev(d, (RECT){dr.right - 1, dr.top, dr.right, dr.bottom}, v);
    return 1;
}
BOOL PatBlt(HDC h, int x, int y, int w, int hh, DWORD rop) {
    RECT r = {x, y, x + w, y + hh};
    dc_t *d = (dc_t *)h;
    return FillRect(h, &r, d->brush);
}
COLORREF SetPixel(HDC h, int x, int y, COLORREF c) {
    dc_t *d = (dc_t *)h;
    fill_dev(d, (RECT){x + d->org.x, y + d->org.y, x + d->org.x + 1, y + d->org.y + 1}, px_of(c));
    return c;
}
COLORREF GetPixel(HDC h, int x, int y) {
    dc_t *d = (dc_t *)h;
    x += d->org.x;
    y += d->org.y;
    if (!d->s || x < 0 || y < 0 || x >= d->s->w || y >= d->s->h) return 0xFFFFFFFFu;
    uint32_t p = d->s->px[(size_t)y * d->s->stride + x];
    return RGB(p >> 16 & 0xFF, p >> 8 & 0xFF, p & 0xFF);
}

/* ---- shapes with the pen and brush (plutovg) ---- */
static void set_color(plutovg_canvas_t *c, COLORREF v) {
    plutovg_canvas_set_rgba(c, (v & 0xFF) / 255.0f, (v >> 8 & 0xFF) / 255.0f, (v >> 16 & 0xFF) / 255.0f, 1.0f);
}
static void fill_stroke(dc_t *d, plutovg_canvas_t *c) {
    brush_t *b = d->brush;
    pen_t *p = d->pen;
    if (b && !b->null) { set_color(c, b->dc ? d->dc_brush : b->c); plutovg_canvas_fill_preserve(c); }
    if (p && !p->null) {
        set_color(c, p->dc ? d->dc_pen : p->c);
        plutovg_canvas_set_line_width(c, (float)p->w);
        plutovg_canvas_stroke_preserve(c);
    }
    plutovg_canvas_new_path(c);
}
BOOL Rectangle(HDC h, int l, int t, int r, int b) {
    dc_t *d = (dc_t *)h;
    plutovg_canvas_t *c = dc_canvas(d);
    if (!c) return FALSE;
    plutovg_canvas_new_path(c);
    plutovg_canvas_rect(c, l + 0.5f, t + 0.5f, r - l - 1, b - t - 1);
    fill_stroke(d, c);
    dc_canvas_end(d);
    return TRUE;
}
BOOL RoundRect(HDC h, int l, int t, int r, int b, int w, int hh) {
    dc_t *d = (dc_t *)h;
    plutovg_canvas_t *c = dc_canvas(d);
    if (!c) return FALSE;
    plutovg_canvas_new_path(c);
    plutovg_canvas_round_rect(c, l + 0.5f, t + 0.5f, r - l - 1, b - t - 1, w / 2.0f, hh / 2.0f);
    fill_stroke(d, c);
    dc_canvas_end(d);
    return TRUE;
}
BOOL Ellipse(HDC h, int l, int t, int r, int b) {
    dc_t *d = (dc_t *)h;
    plutovg_canvas_t *c = dc_canvas(d);
    if (!c) return FALSE;
    plutovg_canvas_new_path(c);
    plutovg_canvas_ellipse(c, (l + r) / 2.0f, (t + b) / 2.0f, (r - l - 1) / 2.0f, (b - t - 1) / 2.0f);
    fill_stroke(d, c);
    dc_canvas_end(d);
    return TRUE;
}
BOOL Polygon(HDC h, const POINT *p, int n) {
    dc_t *d = (dc_t *)h;
    plutovg_canvas_t *c = dc_canvas(d);
    if (!c || n < 2) { if (c) dc_canvas_end(d); return FALSE; }
    plutovg_canvas_new_path(c);
    plutovg_canvas_move_to(c, p[0].x + 0.5f, p[0].y + 0.5f);
    for (int k = 1; k < n; k++) plutovg_canvas_line_to(c, p[k].x + 0.5f, p[k].y + 0.5f);
    plutovg_canvas_close_path(c);
    fill_stroke(d, c);
    dc_canvas_end(d);
    return TRUE;
}
static void stroke_only(dc_t *d, plutovg_canvas_t *c) {
    pen_t *p = d->pen;
    if (p && !p->null) {
        set_color(c, p->dc ? d->dc_pen : p->c);
        plutovg_canvas_set_line_width(c, (float)p->w);
        plutovg_canvas_stroke(c);
    }
    plutovg_canvas_new_path(c);
}
BOOL Polyline(HDC h, const POINT *p, int n) {
    dc_t *d = (dc_t *)h;
    plutovg_canvas_t *c = dc_canvas(d);
    if (!c || n < 2) { if (c) dc_canvas_end(d); return FALSE; }
    plutovg_canvas_new_path(c);
    plutovg_canvas_move_to(c, p[0].x + 0.5f, p[0].y + 0.5f);
    for (int k = 1; k < n; k++) plutovg_canvas_line_to(c, p[k].x + 0.5f, p[k].y + 0.5f);
    stroke_only(d, c);
    dc_canvas_end(d);
    return TRUE;
}
BOOL MoveToEx(HDC h, int x, int y, LPPOINT old) {
    dc_t *d = (dc_t *)h;
    if (old) *old = d->cur;
    d->cur = (POINT){x, y};
    return TRUE;
}
BOOL LineTo(HDC h, int x, int y) {
    POINT p[2] = {((dc_t *)h)->cur, {x, y}};
    Polyline(h, p, 2);
    ((dc_t *)h)->cur = p[1];
    return TRUE;
}

/* ---- BitBlt: plain copies between surfaces ---- */
BOOL BitBlt(HDC hd, int x, int y, int w, int h, HDC hs, int sx, int sy, DWORD rop) {
    dc_t *d = (dc_t *)hd, *s = (dc_t *)hs;
    if (!d || !d->s) return FALSE;
    if (!s || !s->s) return PatBlt(hd, x, y, w, h, rop);
    x += d->org.x;
    y += d->org.y;
    sx += s->org.x;
    sy += s->org.y;
    RECT rs[64], i, dst = {x, y, x + w, y + h}, srcb = {0, 0, s->s->w, s->s->h};
    RECT srcd = srcb;
    OffsetRect(&srcd, x - sx, y - sy);   /* the source surface, placed in destination coordinates */
    if (!IntersectRect(&dst, &dst, &srcd)) return TRUE;
    int n = dc_clip_rects(d, rs, 64);
    for (int k = 0; k < n; k++) {
        if (!IntersectRect(&i, &rs[k], &dst)) continue;
        int cw = i.right - i.left;
        for (int yy = i.top; yy < i.bottom; yy++) {
            const uint32_t *src = s->s->px + (size_t)(yy - y + sy) * s->s->stride + (i.left - x + sx);
            uint32_t *out = d->s->px + (size_t)yy * d->s->stride + i.left;
            memmove(out, src, (size_t)cw * 4);
        }
    }
    return TRUE;
}
BOOL StretchBlt(HDC hd, int x, int y, int w, int h, HDC hs, int sx, int sy, int sw, int sh, DWORD rop) {
    if (w == sw && h == sh) return BitBlt(hd, x, y, w, h, hs, sx, sy, rop);
    return FALSE;
}
BOOL GdiFlush(void) { return TRUE; }

/* ---- text ---- */
static const uint16_t CP1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
                                    0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C,
                                    0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,
                                    0x017E, 0x0178};
static uint32_t cp_of(unsigned char ch) { return ch >= 0x80 && ch < 0xA0 ? CP1252[ch - 0x80] : ch; }
static int to_u32(const char *s, int n, uint32_t *out, int max) {   /* the editor's strings are Windows-1252 */
    int k = 0;
    for (int i = 0; i < n && k < max; i++) out[k++] = cp_of((unsigned char)s[i]);
    return k;
}
static font_t *dc_font(dc_t *d) { return d->font ? d->font : (font_t *)GetStockObject(DEFAULT_GUI_FONT); }
static float text_w(font_t *f, const uint32_t *u, int n) {
    if (n <= 0 || !f) return 0;
    return plutovg_font_face_text_extents(f->face, f->size, u, n, PLUTOVG_TEXT_ENCODING_UTF32, NULL);
}
static int line_h(font_t *f) { return f ? (int)(f->ascent + f->descent) : 16; }

BOOL GetTextExtentPoint32A(HDC h, LPCSTR s, int n, LPSIZE sz) {
    font_t *f = dc_font((dc_t *)h);
    uint32_t u[1024];
    int k = to_u32(s, n, u, 1024);
    sz->cx = (LONG)ceilf(text_w(f, u, k));
    sz->cy = line_h(f);
    return TRUE;
}
BOOL GetTextMetricsA(HDC h, TEXTMETRICA *tm) {
    font_t *f = dc_font((dc_t *)h);
    memset(tm, 0, sizeof *tm);
    if (!f) return FALSE;
    uint32_t x = 'x';
    tm->tmAscent = (LONG)f->ascent;
    tm->tmDescent = (LONG)f->descent;
    tm->tmHeight = tm->tmAscent + tm->tmDescent;
    tm->tmInternalLeading = tm->tmHeight - (LONG)lroundf(f->size);
    tm->tmAveCharWidth = (LONG)lroundf(text_w(f, &x, 1));
    tm->tmMaxCharWidth = (LONG)lroundf(f->size * 1.2f);
    tm->tmWeight = f->weight;
    return TRUE;
}

static void draw_run(dc_t *d, font_t *f, const uint32_t *u, int n, float x, float y_top, const RECT *clip) {
    plutovg_canvas_t *c = dc_canvas(d);
    if (!c) return;
    if (clip) plutovg_canvas_clip_rect(c, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top);
    float w = text_w(f, u, n);
    if (d->bkmode == OPAQUE) {
        set_color(c, d->bk);
        plutovg_canvas_fill_rect(c, x, y_top, w, line_h(f));
    }
    set_color(c, d->text);
    plutovg_canvas_set_font(c, f->face, f->size);
    plutovg_canvas_fill_text(c, u, n, PLUTOVG_TEXT_ENCODING_UTF32, roundf(x), y_top + f->ascent);
    dc_canvas_end(d);
}
BOOL TextOutA(HDC h, int x, int y, LPCSTR s, int n) {
    dc_t *d = (dc_t *)h;
    font_t *f = dc_font(d);
    uint32_t u[1024];
    int k = to_u32(s, n, u, 1024);
    if (f && k) draw_run(d, f, u, k, (float)x, (float)y, NULL);
    return TRUE;
}
BOOL ExtTextOutA(HDC h, int x, int y, UINT opt, const RECT *r, LPCSTR s, UINT n, const INT *dx) {
    dc_t *d = (dc_t *)h;
    if ((opt & ETO_OPAQUE) && r) {
        RECT dr = *r;
        OffsetRect(&dr, d->org.x, d->org.y);
        fill_dev(d, dr, px_of(d->bk));
    }
    font_t *f = dc_font(d);
    uint32_t u[1024];
    int k = to_u32(s, (int)n, u, 1024);
    int was = d->bkmode;
    d->bkmode = TRANSPARENT;
    if (f && k) draw_run(d, f, u, k, (float)x, (float)y, (opt & ETO_CLIPPED) ? r : NULL);
    d->bkmode = was;
    return TRUE;
}

#define TXT_MAX 4096
typedef struct { int start, len; } tline_t;

/* the lines of a DrawText: '&' prefixes taken out (unless DT_NOPREFIX), split at newlines, wrapped at words to `width`
 * with DT_WORDBREAK */
static int layout_lines(font_t *f, const uint32_t *u, int n, UINT fmt, float width, tline_t *ls, int max) {
    int nl = 0, i = 0;
    if (fmt & DT_SINGLELINE) { ls[0] = (tline_t){0, n}; return 1; }
    while (i <= n && nl < max) {
        int e = i;
        while (e < n && u[e] != '\n') e++;
        int ls_end = e > i && u[e - 1] == '\r' ? e - 1 : e;
        int s = i;
        if (!(fmt & DT_WORDBREAK)) ls[nl++] = (tline_t){s, ls_end - s};
        else {
            while (nl < max) {
                if (text_w(f, u + s, ls_end - s) <= width || ls_end - s <= 1) { ls[nl++] = (tline_t){s, ls_end - s}; break; }
                int fit = s, last_space = -1;
                for (int k = s; k < ls_end; k++) {
                    if (text_w(f, u + s, k - s + 1) > width) break;
                    fit = k + 1;
                    if (u[k] == ' ') last_space = k;
                }
                int cut = last_space > s ? last_space : (fit > s ? fit : s + 1);
                ls[nl++] = (tline_t){s, cut - s};
                s = cut;
                while (s < ls_end && u[s] == ' ') s++;
                if (s >= ls_end) break;
            }
        }
        if (e >= n) break;
        i = e + 1;
    }
    return nl;
}

int DrawTextA(HDC h, LPCSTR s, int n, LPRECT r, UINT fmt) {
    dc_t *d = (dc_t *)h;
    font_t *f = dc_font(d);
    if (!f || !s || !r) return 0;
    if (n < 0) n = (int)strlen(s);
    static uint32_t u[TXT_MAX];
    int k = 0;
    for (int i = 0; i < n && k < TXT_MAX; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '&' && !(fmt & DT_NOPREFIX)) {   /* "&&" shows one '&'; "&x" shows x (its underline is left out) */
            if (i + 1 < n && s[i + 1] == '&') i++;
            else continue;
        }
        if ((fmt & DT_SINGLELINE) && (ch == '\n' || ch == '\r')) ch = ' ';
        u[k++] = cp_of(ch);
    }
    float width = (float)(r->right - r->left);
    static tline_t ls[256];
    int nl = layout_lines(f, u, k, fmt, width, ls, 256);
    int lh = line_h(f), total = nl * lh;
    if (fmt & DT_CALCRECT) {
        float wmax = 0;
        for (int j = 0; j < nl; j++) {
            float w = text_w(f, u + ls[j].start, ls[j].len);
            if (w > wmax) wmax = w;
        }
        if (!(fmt & DT_WORDBREAK) || wmax < width) r->right = r->left + (LONG)ceilf(wmax);
        r->bottom = r->top + total;
        return total;
    }
    int y = r->top;
    if (fmt & DT_SINGLELINE) {
        if (fmt & DT_VCENTER) y = r->top + ((r->bottom - r->top) - lh) / 2;
        else if (fmt & DT_BOTTOM) y = r->bottom - lh;
    }
    const RECT *clip = (fmt & DT_NOCLIP) ? NULL : r;
    static uint32_t tmp[TXT_MAX + 4];
    for (int j = 0; j < nl; j++, y += lh) {
        const uint32_t *line = u + ls[j].start;
        int len = ls[j].len;
        float w = text_w(f, line, len);
        if ((fmt & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS)) && w > width && len > 0) {
            uint32_t dots[3] = {'.', '.', '.'};
            float dw = text_w(f, dots, 3);
            int keep = len;
            while (keep > 0 && text_w(f, line, keep) + dw > width) keep--;
            memcpy(tmp, line, keep * 4);
            memcpy(tmp + keep, dots, sizeof dots);
            line = tmp;
            len = keep + 3;
            w = text_w(f, line, len);
        }
        float x = (float)r->left;
        if (fmt & DT_CENTER) x = r->left + (width - w) / 2;
        else if (fmt & DT_RIGHT) x = r->right - w;
        if (len > 0) draw_run(d, f, line, len, x, (float)y, clip);
    }
    return total;
}
