/* The desktop editor's materials and 3D parts: see skin.h. Everything is shaded here per pixel from a fixed light
 * (from the upper left, in front of the panel) with small hand-made noise functions: no image files. Sprites are
 * rendered at 3x3 (LEDs 4x4) samples per pixel, once per size, and kept until the last editor window closes. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "skin.h"

int WINAPI GdipFlush(void *g, int intention);   /* gdiplus.dll (declared by hand, as in gfx.h); 1 = sync */

typedef struct { uint32_t *px; int w, h, stride; } simg_t;   /* 0xAARRGGBB, premultiplied; stride in pixels */
typedef struct { uint64_t key; int w, h; uint32_t *px; } spr_t;

static struct {
    int refs, w, h;
    float s;
    HDC dc[2];
    HBITMAP bm[2];
    HGDIOBJ old[2];
    simg_t img[2];   /* 0: the static layer, 1: the frame being drawn */
    simg_t *tgt;
    RECT clip;
    void *g;
    double bake_ms;
    spr_t *spr;
    int nspr, cap;
} S;

/* ---- small maths ---- */
static const float LX = -0.369f, LY = -0.615f, LZ = 0.697f;   /* towards the light */
static const float HX = -0.200f, HY = -0.334f, HZ = 0.921f;   /* half-way between the light and the eye */
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float sat(float v) { return clampf(v, 0.0f, 1.0f); }
static inline float smooth(float a, float b, float x) {
    float t = sat((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline uint32_t hsh(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static inline float h2(int x, int y, uint32_t s) {   /* white noise 0..1 */
    return hsh((uint32_t)x * 0x9E3779B1u ^ hsh((uint32_t)y + s * 0x85EBCA77u)) * (1.0f / 4294967296.0f);
}
static float vnoise(float x, float y, uint32_t s) {   /* smooth value noise 0..1 */
    float fx0 = floorf(x), fy0 = floorf(y);
    int xi = (int)fx0, yi = (int)fy0;
    float fx = x - fx0, fy = y - fy0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = h2(xi, yi, s), b = h2(xi + 1, yi, s), c = h2(xi, yi + 1, s), d = h2(xi + 1, yi + 1, s);
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}
static inline uint32_t pack(float r, float g, float b) {
    return 0xFF000000u | (uint32_t)(sat(r) * 255.0f + 0.5f) << 16 | (uint32_t)(sat(g) * 255.0f + 0.5f) << 8 |
           (uint32_t)(sat(b) * 255.0f + 0.5f);
}
static inline void unpack(uint32_t c, float *r, float *g, float *b) {
    *r = (c >> 16 & 255) / 255.0f;
    *g = (c >> 8 & 255) / 255.0f;
    *b = (c & 255) / 255.0f;
}
static inline float lit(float nx, float ny, float nz) { return nx * LX + ny * LY + nz * LZ; }
static inline float spec(float nx, float ny, float nz, float p) {
    float d = nx * HX + ny * HY + nz * HZ;
    return d > 0 ? powf(d, p) : 0.0f;
}
static float sdbox(float px, float py, float hw, float hh, float r) {   /* rounded box, negative inside */
    float qx = fabsf(px) - (hw - r), qy = fabsf(py) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

/* ---- the drawing surface ---- */
static void sync(void) {   /* pending GDI+ and GDI drawing into memory before writing pixels ourselves */
    if (S.g) GdipFlush(S.g, 1);
    GdiFlush();
}
void skin_bind(void *g) { S.g = g; }
static RECT saved_clip;
void skin_clip_push(const RECT *r) {   /* narrow the pixel writes to r (one level) */
    saved_clip = S.clip;
    if (!IntersectRect(&S.clip, &S.clip, r)) SetRectEmpty(&S.clip);
}
void skin_clip_pop(void) { S.clip = saved_clip; }
double skin_bake_ms(void) { return S.bake_ms; }

/* the pixels of rect (x, y, w, h) that may be written: inside the surface and the clip */
static int span(int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1) {
    if (!S.tgt) return 0;
    *x0 = x > S.clip.left ? x : S.clip.left;
    *y0 = y > S.clip.top ? y : S.clip.top;
    *x1 = x + w < S.clip.right ? x + w : S.clip.right;
    *y1 = y + h < S.clip.bottom ? y + h : S.clip.bottom;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > S.tgt->w) *x1 = S.tgt->w;
    if (*y1 > S.tgt->h) *y1 = S.tgt->h;
    if (*x0 >= *x1 || *y0 >= *y1) return 0;
    sync();
    return 1;
}
#define PIX(x, y) S.tgt->px[(size_t)(y) * S.tgt->stride + (x)]

static void free_sprites(void) {
    for (int k = 0; k < S.nspr; k++) free(S.spr[k].px);
    free(S.spr);
    S.spr = NULL;
    S.nspr = S.cap = 0;
}
static void free_surface(int k) {
    if (S.dc[k]) {
        SelectObject(S.dc[k], S.old[k]);
        DeleteDC(S.dc[k]);
    }
    if (S.bm[k]) DeleteObject(S.bm[k]);
    S.dc[k] = NULL;
    S.bm[k] = NULL;
    memset(&S.img[k], 0, sizeof S.img[k]);
}
static void free_text(void);
static void free_wheels(void);
static void free_all(void) {
    free_surface(0);
    free_surface(1);
    free_sprites();
    free_text();
    free_wheels();
    S.tgt = NULL;
    S.w = S.h = 0;
}
static int make_surface(int k, int w, int h) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   /* top-down rows */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC screen = GetDC(NULL);
    S.dc[k] = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!S.dc[k]) return 0;
    S.bm[k] = CreateDIBSection(S.dc[k], &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!S.bm[k] || !bits) return 0;
    S.old[k] = SelectObject(S.dc[k], S.bm[k]);
    S.img[k].px = bits;
    S.img[k].w = S.img[k].stride = w;
    S.img[k].h = h;
    return 1;
}

void skin_acquire(void) { S.refs++; }
void skin_release(void) {
    if (S.refs > 0 && --S.refs == 0) free_all();
}

HDC skin_frame(int w, int h, float s, const RECT *clip, skin_bake_fn bake) {
    if (w <= 0 || h <= 0) return NULL;
    if (w != S.w || h != S.h || s != S.s || !S.dc[0] || !S.dc[1]) {
        free_all();
        if (!make_surface(0, w, h) || !make_surface(1, w, h)) { free_all(); return NULL; }
        S.w = w;
        S.h = h;
        S.s = s;
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        S.tgt = &S.img[0];
        SetRect(&S.clip, 0, 0, w, h);
        bake(S.dc[0]);
        S.g = NULL;
        GdiFlush();
        QueryPerformanceCounter(&t1);
        S.bake_ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    }
    S.tgt = &S.img[1];
    S.clip = *clip;
    GdiFlush();
    int x0 = clip->left < 0 ? 0 : clip->left, x1 = clip->right > w ? w : clip->right;
    for (int y = clip->top < 0 ? 0 : clip->top; y < clip->bottom && y < h && x1 > x0; y++)
        memcpy(&S.img[1].px[(size_t)y * w + x0], &S.img[0].px[(size_t)y * w + x0], (size_t)(x1 - x0) * 4);
    return S.dc[1];
}

void skin_present(HDC out, const RECT *clip) {
    GdiFlush();
    BitBlt(out, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, S.dc[1], clip->left,
           clip->top, SRCCOPY);
}

/* ---- materials ---- */
void skin_chassis(int x, int y, int w, int h) {
    int x0, y0, x1, y1;
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float g = 0.050f + 0.012f * (1.0f - (float)j / S.h) + 0.006f * (h2(i, j, 7) - 0.5f);
            PIX(i, j) = pack(g, g, g * 1.04f);
        }
}

/* ash: honey-coloured with darker growth rings drawn out along the board, open pores, fine fibres; the board's
 * rounded long edges a little darker, a soft varnish sheen across it */
void skin_wood(int x, int y, int w, int h, int vertical, unsigned seed) {
    int x0, y0, x1, y1;
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    float s = S.s > 0 ? S.s : 1.0f, across_n = (float)(vertical ? w : h);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float u = (vertical ? j - y : i - x) / s, v = (vertical ? i - x : j - y) / s;   /* along, across */
            float warp = 2.8f * vnoise(u * 0.0030f, v * 0.012f, seed) + 1.1f * vnoise(u * 0.011f, v * 0.04f, seed + 1);
            float f = v * (0.09f + 0.05f * vnoise(u * 0.002f, v * 0.01f, seed + 5)) + warp * 2.2f;
            float ph = f - floorf(f);   /* within a ring: early wood, then a sharper late-wood line */
            float ring = smooth(0.55f, 0.86f, ph) * (1.0f - smooth(0.90f, 1.0f, ph));
            ring *= 0.55f + 0.45f * vnoise(floorf(f) * 3.1f, u * 0.004f, seed + 6);
            float fib = vnoise(u * 0.06f, v * 1.6f, seed + 2);
            float pore = vnoise(u * 0.30f, v * 3.4f, seed + 3);
            pore = pore > 0.76f ? (pore - 0.76f) * 3.0f : 0.0f;
            float t = sat(0.62f * ring + 0.16f * fib + 0.40f * pore * (0.3f + ring));
            float r = mixf(0.875f, 0.660f, t), g = mixf(0.665f, 0.435f, t), b = mixf(0.420f, 0.225f, t);
            float a = ((vertical ? i - x : j - y) + 0.5f) / across_n;   /* 0..1 across the board */
            float edge = smooth(0.0f, 0.10f, a) * smooth(1.0f, 0.90f, a);
            float m = 0.78f + 0.22f * edge;
            float sheen = 0.10f * expf(-(a - 0.32f) * (a - 0.32f) / 0.05f) * (0.6f + 0.4f * vnoise(u * 0.01f, 0, seed + 4));
            PIX(i, j) = pack(r * m + sheen, g * m + sheen * 0.92f, b * m + sheen * 0.8f);
        }
}

/* brushed aluminium: streaks along each bar (noise stretched along it), a rounded outer edge, a flat face and an
 * inner chamfer down to the panel; lit from the upper left */
static float brushed(float along, float across, uint32_t seed) {
    float s = S.s > 0 ? S.s : 1.0f;
    return 0.55f * vnoise(along * 0.006f / s, across * 1.3f / s, seed) + 0.30f * vnoise(along * 0.03f / s, across * 2.7f / s, seed + 1) +
           0.15f * h2((int)(along * 0.25f), (int)across, seed + 2);
}
void skin_metal_frame(int x, int y, int w, int h, int border) {
    int x0, y0, x1, y1;
    if (border < 1 || !span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    float b = (float)border;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float dl = i - x + 0.5f, dr = x + w - i - 0.5f, dt = j - y + 0.5f, db = y + h - j - 0.5f;
            float m = dl, ox = -1, oy = 0;
            if (dr < m) { m = dr; ox = 1; oy = 0; }
            if (dt < m) { m = dt; ox = 0; oy = -1; }
            if (db < m) { m = db; ox = 0; oy = 1; }
            if (m >= b) continue;   /* the panel inside */
            float t = m / b, a;
            if (t < 0.24f) a = (1.0f - t / 0.24f) * 1.15f;      /* rounded outer edge */
            else if (t < 0.70f) a = 0.0f;                       /* face */
            else a = -0.80f;                                     /* chamfer down into the panel */
            float sa = sinf(a), nx = ox * sa, ny = oy * sa, nz = cosf(a);
            float br = brushed(oy != 0 ? (float)i : (float)j, oy != 0 ? (float)j : (float)i, 11);
            float alb = 0.84f * (0.90f + 0.20f * br);
            float c = alb * (0.34f + 0.66f * clampf(lit(nx, ny, nz), 0, 1)) + 0.32f * spec(nx, ny, nz, 24.0f) +
                      0.06f * spec(nx, ny, nz, 3.0f);
            if (t > 0.94f) c *= 0.35f;   /* the seam where the panel meets the rim */
            PIX(i, j) = pack(c * 0.97f, c * 0.985f, c * 1.01f);
        }
}

void skin_metal_plate(int x, int y, int w, int h) {
    int x0, y0, x1, y1;
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float v = (j - y + 0.5f) / h, br = brushed((float)i, (float)j, 23);
            float c = (0.80f - 0.22f * v) * (0.90f + 0.20f * br);
            if (j == y) c = 0.95f;
            if (j == y + h - 1) c *= 0.55f;
            PIX(i, j) = pack(c * 0.97f, c * 0.985f, c * 1.01f);
        }
}

/* black anodised aluminium: a very fine matte grain, a soft vignette, the frame's lip shading its top and left */
void skin_panel(int x, int y, int w, int h) {
    int x0, y0, x1, y1;
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    float s = S.s > 0 ? S.s : 1.0f, sh = 3.5f * s;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float nx = (i - x) / (float)w * 2 - 1, ny = (j - y) / (float)h * 2 - 1;
            float g = 0.112f + 0.016f * (h2(i, j, 3) - 0.5f) + 0.010f * (vnoise(i * 0.35f / s, j * 0.35f / s, 5) - 0.5f) +
                      0.008f * (vnoise(i * 0.02f / s, j * 0.03f / s, 9) - 0.5f);
            g *= 1.0f - 0.20f * (0.55f * nx * nx + 0.85f * ny * ny);
            g += 0.012f * (1.0f - 0.5f * (nx + 1) * 0.5f - 0.5f * (ny + 1) * 0.5f);
            float dt = j - y + 0.5f, dl = i - x + 0.5f;
            g *= 1.0f - 0.55f * expf(-dt / sh) - 0.35f * expf(-dl / sh);
            PIX(i, j) = pack(g, g, g * 1.05f);
        }
}

void skin_shadow(float x, float y, float w, float h, float soft, float alpha) {
    int x0, y0, x1, y1;
    if (soft < 0.5f) soft = 0.5f;
    if (!span((int)floorf(x - soft), (int)floorf(y - soft), (int)ceilf(w + 2 * soft) + 1, (int)ceilf(h + 2 * soft) + 1,
              &x0, &y0, &x1, &y1))
        return;
    float cx = x + w / 2, cy = y + h / 2;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float d = sdbox(i + 0.5f - cx, j + 0.5f - cy, w / 2, h / 2, soft * 0.6f);
            float a = alpha * (1.0f - smooth(-soft * 0.6f, soft, d));
            if (a <= 0.002f) continue;
            float r, g, b;
            unpack(PIX(i, j), &r, &g, &b);
            PIX(i, j) = pack(r * (1 - a), g * (1 - a), b * (1 - a));
        }
}

void skin_recess(int x, int y, int w, int h, float depth) {
    int x0, y0, x1, y1;
    if (depth < 0.5f || !span(x - 1, y - 1, w + 2, h + 2, &x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float r, g, b, m;
            unpack(PIX(i, j), &r, &g, &b);
            if (i < x || j < y) m = 0.55f;                               /* the well's top and left rims: in shade */
            else if (i >= x + w || j >= y + h) m = 1.35f;                /* bottom and right rims: catching light */
            else {
                float dt = j - y + 0.5f, dl = i - x + 0.5f, db = y + h - j - 0.5f, dr = x + w - i - 0.5f;
                m = (1.0f - 0.70f * expf(-dt / depth)) * (1.0f - 0.45f * expf(-dl / depth)) *
                    (1.0f - 0.25f * expf(-db / depth)) * (1.0f - 0.25f * expf(-dr / depth));
            }
            PIX(i, j) = pack(r * m + (m > 1 ? 0.03f : 0), g * m + (m > 1 ? 0.03f : 0), b * m + (m > 1 ? 0.03f : 0));
        }
}

void skin_lcd(int x, int y, int w, int h) {
    int x0, y0, x1, y1;
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float v = (j - y + 0.5f) / h, u = (i - x + 0.5f) / w;
            float m = 0.82f + 0.30f * v * (1.0f - 0.3f * fabsf(u - 0.5f)) + 0.03f * (h2(i, j, 21) - 0.5f);
            PIX(i, j) = pack(0.040f * m, 0.082f * m, 0.125f * m);
        }
}

static spr_t *spr_find(uint64_t key);
static spr_t *spr_add(uint64_t key, int w, int h, uint32_t *px);
void skin_glare(int x, int y, int w, int h) {   /* screen-blended: brightens dark glass, barely touches the text */
    int x0, y0, x1, y1;
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535 || !span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    uint64_t key = 4ull << 60 | (uint64_t)w << 16 | (uint64_t)h;
    spr_t *gm = spr_find(key);
    if (!gm) {   /* the sheen for a glass this size, once: 0..255 per pixel */
        float s = S.s > 0 ? S.s : 1.0f;
        uint32_t *m = malloc((size_t)w * h * 4);
        if (!m) return;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                float u = (i + 0.5f) / w, v = (j + 0.5f) / h, d = u * 0.62f + v * 0.95f;
                float gl = 0.050f * smooth(0.66f, 0.40f, d) + 0.018f * smooth(0.95f, 0.75f, d);
                gl += 0.10f * expf(-j / (1.2f * s)) + 0.03f * expf(-i / (1.5f * s));
                m[(size_t)j * w + i] = (uint32_t)(sat(gl) * 256.0f);
            }
        gm = spr_add(key, w, h, m);
        if (!gm) return;
    }
    for (int j = y0; j < y1; j++) {
        const uint32_t *g = &gm->px[(size_t)(j - y) * w + (x0 - x)];
        uint32_t *d = &PIX(x0, j);
        for (int i = x0; i < x1; i++, g++, d++) {
            uint32_t a = *g, c = *d;
            if (!a) continue;
            uint32_t r = c >> 16 & 255, gg = c >> 8 & 255, b = c & 255;
            r += (255 - r) * a >> 8;
            gg += (255 - gg) * a >> 8;
            b += (255 - b) * a >> 8;
            *d = 0xFF000000u | r << 16 | gg << 8 | b;
        }
    }
}

/* ---- text: each string drawn once by GDI (ClearType, white on black) into a coverage mask, kept, and blended in
 * with its colour after that; ClearType's three coverages are kept apart so it stays sharp. Drawing ~400 strings with
 * DrawText every paint was most of a full repaint. ---- */
typedef struct { uint32_t hash; HFONT font; UINT fmt; int w, h, next, ix0, iy0, ix1, iy1; char *s; uint8_t *cov; } txt_t;
/* cov: w*h*3; ix0..ix1, iy0..iy1: where the ink is */
enum { TBUCKETS = 4096 };   /* a power of two */
static struct {
    txt_t *t;
    int n, cap, head[TBUCKETS];   /* head: 1 + the first entry with that hash's low bits (0: none) */
    size_t bytes;
    HDC dc;
    HBITMAP bm;
    HGDIOBJ old;
    uint32_t *px;
    int w, h;
} T;
static void free_text(void) {
    for (int k = 0; k < T.n; k++) { free(T.t[k].s); free(T.t[k].cov); }
    free(T.t);
    if (T.dc) { SelectObject(T.dc, T.old); DeleteDC(T.dc); }
    if (T.bm) DeleteObject(T.bm);
    memset(&T, 0, sizeof T);
}
static int text_scratch(int w, int h) {   /* a white-on-black drawing surface at least w x h */
    if (T.dc && w <= T.w && h <= T.h) return 1;
    if (T.dc) { SelectObject(T.dc, T.old); DeleteDC(T.dc); DeleteObject(T.bm); T.dc = NULL; T.bm = NULL; }
    int nw = w > T.w ? w : T.w, nh = h > T.h ? h : T.h;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = nw;
    bi.bmiHeader.biHeight = -nh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC screen = GetDC(NULL);
    T.dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (T.dc) T.bm = CreateDIBSection(T.dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!T.dc || !T.bm || !bits) {
        if (T.dc) DeleteDC(T.dc);
        if (T.bm) DeleteObject(T.bm);
        T.dc = NULL; T.bm = NULL; T.w = T.h = 0;
        return 0;
    }
    T.old = SelectObject(T.dc, T.bm);
    T.px = bits;
    T.w = nw;
    T.h = nh;
    SetBkMode(T.dc, TRANSPARENT);
    SetTextColor(T.dc, RGB(255, 255, 255));
    return 1;
}
static txt_t *text_get(const char *s, HFONT f, UINT fmt, int w, int h) {
    uint32_t hs = 2166136261u;
    for (const char *p = s; *p; p++) hs = (hs ^ (uint8_t)*p) * 16777619u;
    hs = (hs ^ (uint32_t)(uintptr_t)f) * 16777619u;
    hs = (hs ^ fmt) * 16777619u;
    hs = (hs ^ (uint32_t)(w << 16 | h)) * 16777619u;
    for (int k = T.head[hs & (TBUCKETS - 1)] - 1; k >= 0; k = T.t[k].next) {
        txt_t *t = &T.t[k];
        if (t->hash == hs && t->font == f && t->fmt == fmt && t->w == w && t->h == h && !strcmp(t->s, s)) return t;
    }
    if (T.bytes > (24u << 20)) {   /* a lot of different values seen: start again */
        for (int k = 0; k < T.n; k++) { free(T.t[k].s); free(T.t[k].cov); }
        T.n = 0;
        T.bytes = 0;
        memset(T.head, 0, sizeof T.head);
    }
    if (T.n == T.cap) {
        int cap = T.cap ? T.cap * 2 : 512;
        txt_t *n = realloc(T.t, sizeof *n * cap);
        if (!n) return NULL;
        T.t = n;
        T.cap = cap;
    }
    if (!text_scratch(w, h)) return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    uint8_t *cov = malloc((size_t)w * h * 3);
    if (!copy || !cov) { free(copy); free(cov); return NULL; }
    memcpy(copy, s, len + 1);
    for (int j = 0; j < h; j++) memset(&T.px[(size_t)j * T.w], 0, (size_t)w * 4);
    HGDIOBJ of = SelectObject(T.dc, f);
    RECT r = {0, 0, w, h};
    DrawTextA(T.dc, s, -1, &r, fmt | DT_NOPREFIX);
    SelectObject(T.dc, of);
    GdiFlush();
    int ix0 = w, iy0 = h, ix1 = 0, iy1 = 0;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            uint32_t c = T.px[(size_t)j * T.w + i];
            uint8_t *o = &cov[((size_t)j * w + i) * 3];
            o[0] = (uint8_t)(c >> 16);
            o[1] = (uint8_t)(c >> 8);
            o[2] = (uint8_t)c;
            if (c & 0xFFFFFF) {
                if (i < ix0) ix0 = i;
                if (i >= ix1) ix1 = i + 1;
                if (j < iy0) iy0 = j;
                if (j >= iy1) iy1 = j + 1;
            }
        }
    txt_t *t = &T.t[T.n++];
    t->ix0 = ix0; t->iy0 = iy0; t->ix1 = ix1; t->iy1 = iy1;
    t->hash = hs; t->font = f; t->fmt = fmt; t->w = w; t->h = h; t->s = copy; t->cov = cov;
    t->next = T.head[hs & (TBUCKETS - 1)] - 1;
    T.head[hs & (TBUCKETS - 1)] = T.n;   /* (T.n is already this entry's index + 1) */
    T.bytes += (size_t)w * h * 3 + len + 1;
    return t;
}
int skin_text(const RECT *r, const char *s, HFONT f, COLORREF col, UINT fmt) {
    int w = r->right - r->left, h = r->bottom - r->top, x0, y0, x1, y1;
    if (!s || !*s || w <= 0 || h <= 0 || w > 4096 || h > 4096) return 1;
    if (!S.tgt) return 0;
    RECT o;
    if (!IntersectRect(&o, r, &S.clip)) return 1;   /* not in this paint */
    txt_t *t = text_get(s, f, fmt, w, h);
    if (!t) return 0;   /* (the caller draws it itself) */
    if (t->ix1 <= t->ix0 || !span(r->left + t->ix0, r->top + t->iy0, t->ix1 - t->ix0, t->iy1 - t->iy0, &x0, &y0, &x1, &y1))
        return 1;   /* no ink, or none of it to draw here */
    uint32_t cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);
    for (int j = y0; j < y1; j++) {
        const uint8_t *c = &t->cov[((size_t)(j - r->top) * w + (x0 - r->left)) * 3];
        uint32_t *d = &PIX(x0, j);
        for (int i = x0; i < x1; i++, c += 3, d++) {
            if (!(c[0] | c[1] | c[2])) continue;
            uint32_t v = *d, dr = v >> 16 & 255, dg = v >> 8 & 255, db = v & 255;
            dr += ((int)cr - (int)dr) * (int)c[0] / 255;
            dg += ((int)cg - (int)dg) * (int)c[1] / 255;
            db += ((int)cb - (int)db) * (int)c[2] / 255;
            *d = 0xFF000000u | (dr & 255) << 16 | (dg & 255) << 8 | (db & 255);
        }
    }
    return 1;
}

void skin_touch(int x, int y, int w, int h) {
    int x0, y0, x1, y1;
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    float s = S.s > 0 ? S.s : 1.0f;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float v = (j - y + 0.5f) / h;
            float g = 0.060f + 0.012f * v + 0.014f * (h2(i, j, 31) - 0.5f) + 0.008f * (vnoise(i * 0.5f / s, j * 0.5f / s, 33) - 0.5f);
            PIX(i, j) = pack(g, g, g * 1.03f);
        }
}

/* ---- keys ---- */
static inline float cover(int i, float x0, float x1) {   /* how much of pixel column i lies in [x0, x1) */
    float a = i > x0 ? (float)i : x0, b = i + 1 < x1 ? (float)(i + 1) : x1;
    return b > a ? b - a : 0.0f;
}
static inline void put_mix(int i, int j, float c, float r, float g, float b) {   /* c of (r,g,b) over the pixel */
    if (c >= 0.999f) { PIX(i, j) = pack(r, g, b); return; }
    float dr, dg, db;
    unpack(PIX(i, j), &dr, &dg, &db);
    PIX(i, j) = pack(dr + (r - dr) * c, dg + (g - dg) * c, db + (b - db) * c);
}

void skin_white_key(float x0, float x1, int top, int h, int down) {
    int a0, b0, a1, b1;
    if (!span((int)floorf(x0), top, (int)ceilf(x1) - (int)floorf(x0), h, &a0, &b0, &a1, &b1)) return;
    float s = S.s > 0 ? S.s : 1.0f, gap = 0.7f * s, lip = (down ? 5.0f : 8.5f) * s, f0 = x0 + gap, f1 = x1 - gap;
    for (int j = b0; j < b1; j++) {
        float y = j - top + 0.5f, front = h - y;
        for (int i = a0; i < a1; i++) {
            float c = cover(i, x0, x1);
            if (c <= 0) continue;
            float fc = cover(i, f0, f1);
            float r = 0.05f, g = 0.05f, b = 0.05f;   /* the gap between keys */
            if (fc > 0) {
                float e = fminf(i + 0.5f - f0, f1 - i - 0.5f), k;
                if (front > lip) {   /* the top of the key */
                    k = 0.925f + 0.05f * (y / h);
                    k *= 1.0f - (down ? 0.62f : 0.50f) * expf(-y / ((down ? 26.0f : 20.0f) * s));   /* in the strip's shade */
                    k *= 1.0f - (down ? 0.22f : 0.07f) * expf(-e / ((down ? 3.0f : 1.2f) * s));   /* the sides */
                    k += 0.006f * (h2(i, j, 41) - 0.5f);
                } else {   /* the front lip */
                    float t = 1.0f - front / lip;
                    k = t < 0.14f ? 1.0f : 0.80f - 0.12f * t;
                    if (front < 1.0f * s) k = 0.40f;
                    k *= 1.0f - 0.10f * expf(-e / (1.2f * s));
                }
                r = mixf(r, k * 0.985f, fc);
                g = mixf(g, k * 0.975f, fc);
                b = mixf(b, k * 0.945f, fc);
                if (down) { r *= 0.93f; g *= 0.94f; b *= 0.96f; }
            }
            put_mix(i, j, c, r, g, b);
        }
    }
}

void skin_key_shadow(float x0, float x1, int top, int h) {
    float s = S.s > 0 ? S.s : 1.0f, w = x1 - x0;
    skin_shadow(x0 + 0.12f * w, (float)top - 4 * s, w * 1.08f, (float)h + 7 * s, 4.0f * s, 0.42f);
}

void skin_black_key(float x0, float x1, int top, int h, int down) {
    int a0, b0, a1, b1;
    if (!span((int)floorf(x0), top, (int)ceilf(x1) - (int)floorf(x0), h, &a0, &b0, &a1, &b1)) return;
    float s = S.s > 0 ? S.s : 1.0f, w = x1 - x0, sw = 0.13f * w, fh = (down ? 4.0f : 8.0f) * s;
    for (int j = b0; j < b1; j++) {
        float y = j - top + 0.5f, front = h - y, v = y / h;
        for (int i = a0; i < a1; i++) {
            float c = cover(i, x0, x1);
            if (c <= 0) continue;
            float u = (i + 0.5f - x0) / w, k;
            if (front < fh) {   /* the sloped front, facing up towards the player and the light */
                float t = front / fh;
                k = 0.10f + 0.16f * t + (front < 1.0f * s ? -0.06f : 0.0f);
                if (u * w < sw * 0.6f || (1 - u) * w < sw * 0.6f) k *= 0.6f;
            } else if (u * w < sw) {          /* the left side: lit */
                k = 0.070f + 0.05f * (u * w / sw);
            } else if ((1 - u) * w < sw) {    /* the right side: in shade */
                k = 0.025f;
            } else {                           /* the top: glossy, a long highlight on its left half */
                float uu = (u * w - sw) / (w - 2 * sw);
                k = 0.050f + 0.045f * v;
                k += (down ? 0.05f : 0.10f) * expf(-(uu - 0.30f) * (uu - 0.30f) / 0.012f) * smooth(0.0f, 0.5f, v);
                k += 0.03f * expf(-(front - fh) / (2.0f * s));   /* the edge before the slope */
            }
            if (down) k *= 0.85f;
            k *= 1.0f - 0.35f * expf(-y / (8.0f * s));   /* in the strip's shade at the back */
            put_mix(i, j, c, k, k, k * 1.05f);
        }
    }
}

/* a rubber wheel turning about a horizontal axis in its slot: shaded like a cylinder, fine ridges across it that move
 * with it, a notch at its centre position */
static struct { int x, y, w, h; float pos; uint32_t *px; } WC[4];   /* each wheel as last drawn */
static void free_wheels(void) {
    for (int k = 0; k < 4; k++) { free(WC[k].px); WC[k].px = NULL; }
}
/* a polished aluminium wheel turning about a horizontal axis: a cylinder, so it mirrors the bright room above along a
 * band and the dark panel below; fine grip ridges across it that move as it turns, a scooped notch at its centre
 * position, rounded rims at both sides (darker), and its ends curving away into the slot */
static void wheel_render(uint32_t *px, int w, int h, float pos) {
    float cx = w / 2.0f, cy = h / 2.0f, hw = w / 2.0f, hh = h / 2.0f, rot = pos * 0.95f;
    float ridges = 2.0f * 3.14159265f * hh / (2.2f * (S.s > 0 ? S.s : 1.0f));   /* a ridge every ~2.2 base units */
    for (int j = 0; j < h; j++) {
        float sy = (j + 0.5f - cy) / hh, beta = asinf(clampf(sy * 0.97f, -1, 1));
        float phi = beta + rot, fade = powf(clampf(cosf(beta), 0, 1), 0.55f);
        for (int i = 0; i < w; i++) {
            float u = (i + 0.5f - cx) / hw, au = fabsf(u);
            float tilt = au > 0.72f ? (au - 0.72f) / 0.28f * 1.2f : 0.0f;   /* the rims */
            float rid = 0.0f;
            if (fabsf(phi) > 0.22f && au < 0.80f) rid = sinf(phi * ridges);   /* fine grip ridges */
            float ny = sinf(beta) + 0.10f * rid, nz = cosf(beta), nx = (u > 0 ? 1 : -1) * sinf(tilt);
            nz *= cosf(tilt);
            float l = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= l; ny /= l; nz /= l;
            /* polished metal: what it mirrors (bright above, dark below), the light's highlight, a little diffuse */
            float env = 0.26f + 0.50f * smooth(0.75f, -0.85f, ny) - 0.10f * smooth(0.1f, 0.9f, ny);
            float k = 0.72f * env + 0.12f * clampf(lit(nx, ny, nz), 0, 1) + 0.55f * spec(nx, ny, nz, 18.0f) +
                      0.10f * spec(nx, ny, nz, 4.0f);
            k += 0.025f * (h2(i, (int)(phi * 60.0f), 51) - 0.5f);   /* machining marks around it */
            if (au > 0.72f) k *= 0.62f + 0.38f * (1.0f - (au - 0.72f) / 0.28f);   /* darker at the rims */
            float notch = fabsf(phi) < 0.18f ? 1.0f - fabsf(phi) / 0.18f : 0.0f;
            if (notch > 0 && au < 0.86f) {   /* the centre notch: a scoop, its near wall in shade, the far one lit */
                k = phi < 0 ? 0.16f + 0.10f * (1.0f - notch) : 0.30f + 0.45f * notch;
            }
            k *= fade;
            px[(size_t)j * w + i] = pack(k * 0.97f, k * 0.985f, k * 1.02f);
        }
    }
}
void skin_wheel(int x, int y, int w, int h, float pos) {
    int a0, b0, a1, b1, slot = -1;
    if (w <= 0 || h <= 0 || !span(x, y, w, h, &a0, &b0, &a1, &b1)) return;
    for (int k = 0; k < 4 && slot < 0; k++)
        if (WC[k].px && WC[k].x == x && WC[k].y == y && WC[k].w == w && WC[k].h == h) slot = k;
    if (slot < 0) {   /* a new wheel: a free slot (or the first) */
        for (int k = 0; k < 4 && slot < 0; k++)
            if (!WC[k].px) slot = k;
        if (slot < 0) slot = 0;
        free(WC[slot].px);
        WC[slot].px = malloc((size_t)w * h * 4);
        if (!WC[slot].px) return;
        WC[slot].x = x; WC[slot].y = y; WC[slot].w = w; WC[slot].h = h;
        wheel_render(WC[slot].px, w, h, pos);
        WC[slot].pos = pos;
    } else if (WC[slot].pos != pos) {
        wheel_render(WC[slot].px, w, h, pos);
        WC[slot].pos = pos;
    }
    for (int j = b0; j < b1; j++)
        memcpy(&PIX(a0, j), &WC[slot].px[(size_t)(j - y) * w + (a0 - x)], (size_t)(a1 - a0) * 4);
}

/* ---- sprites ---- */
typedef void (*shade_fn)(const void *ctx, float x, float y, float o[4]);   /* premultiplied rgba (rgb may exceed a: light) */

static spr_t *spr_find(uint64_t key) {
    for (int k = 0; k < S.nspr; k++)
        if (S.spr[k].key == key) return &S.spr[k];
    return NULL;
}
static spr_t *spr_add(uint64_t key, int w, int h, uint32_t *px) {   /* keeps px (malloc'd); frees it on failure */
    if (S.nspr == S.cap) {
        int cap = S.cap ? S.cap * 2 : 64;
        spr_t *n = realloc(S.spr, sizeof *n * cap);
        if (!n) { free(px); return NULL; }
        S.spr = n;
        S.cap = cap;
    }
    spr_t *sp = &S.spr[S.nspr++];
    sp->key = key;
    sp->w = w;
    sp->h = h;
    sp->px = px;
    return sp;
}
static spr_t *spr_make(uint64_t key, int w, int h, shade_fn f, const void *ctx, int ss) {
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) return NULL;
    float inv = 1.0f / (ss * ss);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            float acc[4] = {0, 0, 0, 0}, o[4];
            for (int b = 0; b < ss; b++)
                for (int a = 0; a < ss; a++) {
                    f(ctx, i + (a + 0.5f) / ss, j + (b + 0.5f) / ss, o);
                    acc[0] += o[0]; acc[1] += o[1]; acc[2] += o[2]; acc[3] += o[3];
                }
            uint32_t A = (uint32_t)(sat(acc[3] * inv) * 255 + 0.5f), R = (uint32_t)(sat(acc[0] * inv) * 255 + 0.5f),
                     G = (uint32_t)(sat(acc[1] * inv) * 255 + 0.5f), B = (uint32_t)(sat(acc[2] * inv) * 255 + 0.5f);
            px[(size_t)j * w + i] = A << 24 | R << 16 | G << 8 | B;
        }
    return spr_add(key, w, h, px);
}
static void spr_blit(const spr_t *sp, int x, int y) {   /* premultiplied over; light (rgb > alpha) adds */
    int x0, y0, x1, y1;
    if (!sp || !span(x, y, sp->w, sp->h, &x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++) {
        const uint32_t *s = &sp->px[(size_t)(j - y) * sp->w + (x0 - x)];
        uint32_t *d = &PIX(x0, j);
        for (int i = x0; i < x1; i++, s++, d++) {
            uint32_t c = *s;
            if (!c) continue;
            uint32_t a = c >> 24, inv = 255 - a, dc = *d;
            uint32_t r = (c >> 16 & 255) + ((dc >> 16 & 255) * inv + 127) / 255;
            uint32_t g = (c >> 8 & 255) + ((dc >> 8 & 255) * inv + 127) / 255;
            uint32_t b = (c & 255) + ((dc & 255) * inv + 127) / 255;
            *d = 0xFF000000u | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
        }
    }
}
/* where a sprite centred at c (snapped to half pixels) goes: its integer corner, and the half-pixel offset (0/1) */
static int half_snap(float c, int *frac) {
    int q = (int)lroundf(c * 2);
    int i = q >= 0 ? q / 2 : -((-q + 1) / 2);
    *frac = q - 2 * i;
    return i;
}

/* the knob: a soft drop shadow, a black skirt with fine knurling, and a polished aluminium cap brushed in circles
 * (so its highlight is the bow-tie along the light's direction), a bevelled edge. The light stays put as it turns;
 * only the knurling moves, so a few frames per ridge spacing cover every angle. */
typedef struct { float cx, cy, R, cap, nk, phase; int big; } knob_ctx;
static void knob_shade(const void *p, float x, float y, float o[4]) {
    const knob_ctx *k = p;
    float dx = x - k->cx, dy = y - k->cy, r = sqrtf(dx * dx + dy * dy), R = k->R;
    float sx = dx - 0.06f * R, sy = dy - 0.13f * R, rs = sqrtf(sx * sx + sy * sy);
    float sa = 0.70f * (1.0f - smooth(R * 0.78f, R * 1.28f, rs));
    o[0] = o[1] = o[2] = 0;
    o[3] = sa;
    if (r >= R) return;
    float ux = r > 1e-4f ? dx / r : 0, uy = r > 1e-4f ? dy / r : 0, cr = k->cap * R, c;
    if (r < cr) {   /* the cap */
        float t = r / cr, edge = k->big ? 0.90f : 0.84f;
        float nx = 0, ny = 0, nz = 1;
        if (t > edge) {
            float a = (t - edge) / (1 - edge) * 1.05f;
            nx = ux * sinf(a); ny = uy * sinf(a); nz = cosf(a);
            c = 0.22f + 0.78f * powf(clampf(lit(nx, ny, nz), 0, 1), 1.6f) + 0.5f * spec(nx, ny, nz, 30.0f);
        } else {
            float phi = atan2f(dy, dx), lphi = atan2f(LY, LX);
            float bow = powf(fabsf(cosf(phi - lphi)), 9.0f) * (0.30f + 0.70f * t);
            float ring = h2((int)(r * 2.7f), 0, 77) - 0.5f;
            c = 0.50f + 0.36f * bow + 0.07f * ring - 0.07f * (ux * LX + uy * LY) * t - 0.05f * t * t;
            if (k->big) c += 0.04f * expf(-t * t * 8.0f);   /* the dimple's soft centre */
        }
        o[0] = c * 0.965f; o[1] = c * 0.975f; o[2] = c;
        o[3] = 1;
        return;
    }
    /* the skirt: a steep black flank; a flat shoulder by the cap, a rounded lip at the bottom */
    float tilt = r < cr * 1.10f ? 0.35f : r > R * 0.95f ? 1.25f : 0.85f;
    float rid = 0;
    if (k->nk > 0 && r >= cr * 1.12f && r <= R * 0.95f) {
        rid = sinf(k->nk * atan2f(dy, dx) + k->phase);
        rid = rid > 0 ? sqrtf(rid) : -sqrtf(-rid);
    }
    float st = sinf(tilt), nx = ux * st - uy * 0.38f * rid, ny = uy * st + ux * 0.38f * rid, nz = cosf(tilt);
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    nx /= l; ny /= l; nz /= l;
    float alb = k->big ? 0.050f : 0.055f;
    c = alb * (0.30f + 0.95f * clampf(lit(nx, ny, nz), 0, 1)) + 0.42f * spec(nx, ny, nz, 40.0f) + 0.05f * spec(nx, ny, nz, 5.0f);
    if (r < cr * 1.03f) c *= 0.5f;   /* the cap's shadow line */
    o[0] = c; o[1] = c; o[2] = c * 1.05f;
    o[3] = 1;
}

void skin_knob(float cx, float cy, float r, float angle_deg, int big) {
    if (r < 2) return;
    cx += 0.5f;   /* GDI+ puts pixel centres on whole numbers, the sprites at half: line them up with the pointer */
    cy += 0.5f;
    enum { F = 6 };
    float R = roundf(r * 4) / 4;
    float nk = R < 7 ? 0 : clampf(roundf(6.2831853f * R / 3.3f), 14, 44);
    float per = 360.0f / (nk > 0 ? nk : 1);
    int frame = nk > 0 ? ((int)lroundf(angle_deg / per * F) % F + F) % F : 0, fx, fy;
    int ix = half_snap(cx, &fx), iy = half_snap(cy, &fy), m = (int)ceilf(R * 1.40f) + 2;
    uint64_t key = 1ull << 60 | (uint64_t)(R * 4) << 20 | (uint64_t)frame << 4 | (uint64_t)fx << 2 | (uint64_t)fy << 1 | (uint64_t)!!big;
    spr_t *sp = spr_find(key);
    if (!sp) {
        knob_ctx k = {m + fx * 0.5f, m + fy * 0.5f, R, big ? 0.56f : 0.62f, nk,
                      -6.2831853f * frame / F, big};
        sp = spr_make(key, 2 * m + 1, 2 * m + 1, knob_shade, &k, 3);
    }
    spr_blit(sp, ix - m, iy - m);
}

/* a push button: a rounded cap with a bevel in a hole in the panel, a soft shadow under it; latched (SK_ON) it sits
 * lower and glows from inside; red buttons are lit red with a faint halo */
typedef struct { float cx, cy, hw, hh, rr, bw, glow, s; int kind, state; } btn_ctx;
static void btn_shade(const void *p, float x, float y, float o[4]) {
    const btn_ctx *b = p;
    int on = b->state == SK_ON, hot = b->state == SK_HOT, red = b->kind == SK_RED;
    float px = x - b->cx, py = y - b->cy;
    float hole = sdbox(px, py, b->hw + 0.9f * b->s, b->hh + 0.9f * b->s, b->rr + 0.9f * b->s);
    float shadow = sdbox(px, py - (on ? 0.4f : 1.4f) * b->s, b->hw, b->hh, b->rr);
    float da = 0.55f * (1.0f - smooth(-1.0f * b->s, (on ? 1.5f : 3.0f) * b->s, shadow));
    if (hole < 0) da = fmaxf(da, 0.85f);
    o[0] = o[1] = o[2] = 0;
    o[3] = da;
    float gi = 0, gr = 0, gg = 0, gb = 0;
    if (on && !red) { gi = 0.50f; gr = 1.0f; gg = 0.90f; gb = 0.72f; }
    if (red) { gi = hot ? 0.38f : 0.22f; gr = 1.0f; gg = 0.16f; gb = 0.10f; }
    if (gi > 0) {
        float d = sdbox(px, py, b->hw, b->hh, b->rr);
        if (d > 0) {
            float g = gi * expf(-d / b->glow);
            o[0] += g * gr; o[1] += g * gg; o[2] += g * gb;
            o[3] = fminf(1.0f, o[3] + g * 0.15f);
        }
    }
    float cy = on ? 0.5f * b->s : 0.0f, hw = b->hw - (on ? 0.4f : 0) * b->s, hh = b->hh - (on ? 0.4f : 0) * b->s;
    float sd = sdbox(px, py - cy, hw, hh, b->rr);
    if (sd >= 0) return;
    float depth = -sd, bw = on ? b->bw * 0.7f : b->bw, nx = 0, ny = 0, nz = 1;
    if (depth < bw) {   /* the bevel: tilt outwards, along the box's gradient */
        float e = 0.25f, gx = sdbox(px + e, py - cy, hw, hh, b->rr) - sdbox(px - e, py - cy, hw, hh, b->rr);
        float gy = sdbox(px, py - cy + e, hw, hh, b->rr) - sdbox(px, py - cy - e, hw, hh, b->rr);
        float gl = sqrtf(gx * gx + gy * gy);
        if (gl > 1e-5f) { gx /= gl; gy /= gl; }
        float a = (1.0f - depth / bw) * 1.0f;
        nx = gx * sinf(a); ny = gy * sinf(a); nz = cosf(a);
    }
    float v = (py - cy) / hh, d = clampf(lit(nx, ny, nz), 0, 1), r, g, bl;
    if (red) {
        float e = hot ? 0.82f : 0.62f;
        r = 0.80f * e + 0.25f * d * e; g = 0.07f * e + 0.02f * d; bl = 0.05f * e;
        float gloss = 0.22f * smooth(-0.95f, -0.75f, v) * smooth(-0.15f, -0.45f, v) * (depth > bw ? 1.0f : 0.4f);
        float sp = 0.55f * spec(nx, ny, nz, 40.0f) + gloss;
        r += sp; g += sp * 0.85f; bl += sp * 0.8f;
        r *= 1.0f - 0.18f * (v + 1) * 0.5f;
    } else if (on) {
        float e = 0.84f + 0.14f * (1.0f - fabsf(v)) - (depth < bw ? 0.18f * (1 - depth / bw) : 0);
        r = 1.00f * e; g = 0.955f * e; bl = 0.86f * e;
        float sp = 0.25f * spec(nx, ny, nz, 30.0f);
        r += sp; g += sp; bl += sp;
    } else {
        float alb = hot ? 0.15f : 0.10f;
        float k = alb * (0.30f + 0.85f * d) + 0.35f * spec(nx, ny, nz, 28.0f) + 0.04f * spec(nx, ny, nz, 4.0f);
        k += 0.03f * smooth(-1.0f, -0.6f, v) * smooth(0.0f, -0.5f, v) * (depth > bw ? 1.0f : 0.0f);
        k *= 1.0f - 0.15f * (v + 1) * 0.5f;
        r = k; g = k; bl = k * 1.04f;
    }
    o[0] = r; o[1] = g; o[2] = bl;
    o[3] = 1;
}

void skin_button(float x, float y, float w, float h, int kind, int state) {
    if (w < 3 || h < 3) return;
    float s = S.s > 0 ? S.s : 1.0f;
    float qw = roundf(w * 2) / 2, qh = roundf(h * 2) / 2;
    int fx, fy, ix = half_snap(x, &fx), iy = half_snap(y, &fy);
    float glow = 0.30f * fminf(qw, qh) + 2.0f * s;
    int m = (int)ceilf(glow * 2.6f) + 3;
    uint64_t key = 2ull << 60 | (uint64_t)(qw * 2) << 40 | (uint64_t)(qh * 2) << 24 | (uint64_t)kind << 8 |
                   (uint64_t)state << 4 | (uint64_t)fx << 1 | (uint64_t)fy;
    spr_t *sp = spr_find(key);
    if (!sp) {
        float rr = fminf(2.2f * s, fminf(qw, qh) * 0.3f), bw = clampf(0.22f * fminf(qw, qh), 1.2f, 3.5f * s);
        btn_ctx c = {m + fx * 0.5f + qw / 2, m + fy * 0.5f + qh / 2, qw / 2, qh / 2, rr, bw, glow, s, kind, state};
        sp = spr_make(key, (int)ceilf(qw) + 2 * m + 1, (int)ceilf(qh) + 2 * m + 1, btn_shade, &c, 3);
    }
    spr_blit(sp, ix - m, iy - m);
}

/* an LED: a small glass dome in a dark hole; lit, a warm white core, amber at its edge, and a halo on the panel */
typedef struct { float cx, cy, r; int on; } led_ctx;
static void led_shade(const void *p, float x, float y, float o[4]) {
    const led_ctx *l = p;
    float dx = x - l->cx, dy = y - l->cy, d = sqrtf(dx * dx + dy * dy), r = l->r;
    o[0] = o[1] = o[2] = 0;
    o[3] = d < r * 1.32f ? 0.80f * (1.0f - smooth(r * 1.15f, r * 1.32f, d)) : 0.0f;
    int red = l->on == SK_LED_RED;
    if (l->on && d > r * 0.9f) {
        float g = 0.75f * expf(-(d - r) / (1.15f * r));
        o[0] += g * 1.0f; o[1] += g * (red ? 0.22f : 0.80f); o[2] += g * (red ? 0.12f : 0.52f);
        o[3] = fminf(1.0f, o[3] + g * 0.12f);
    }
    if (d >= r) return;
    float nx = dx / r, ny = dy / r, nz = sqrtf(fmaxf(0.0f, 1.0f - nx * nx - ny * ny)), c[3];
    if (l->on) {
        float t = nz * nz;
        c[0] = 1.0f; c[1] = red ? mixf(0.12f, 0.62f, t) : mixf(0.62f, 0.97f, t); c[2] = red ? mixf(0.06f, 0.50f, t) : mixf(0.28f, 0.86f, t);
        float e = 0.80f + 0.35f * nz;
        for (int k = 0; k < 3; k++) c[k] *= e;
    } else {
        float df = clampf(lit(nx, ny, nz), 0, 1);
        c[0] = 0.24f * (0.25f + 0.6f * df); c[1] = 0.17f * (0.25f + 0.6f * df); c[2] = 0.11f * (0.25f + 0.6f * df);
        (void)red;
    }
    float sp = (l->on ? 0.4f : 0.85f) * spec(nx, ny, nz, 50.0f);
    o[0] = c[0] + sp; o[1] = c[1] + sp; o[2] = c[2] + sp;
    o[3] = 1;
}

void skin_led(float cx, float cy, float r, int on) {
    if (r < 0.8f) r = 0.8f;
    cx += 0.5f;
    cy += 0.5f;
    float q = roundf(r * 4) / 4;
    int fx, fy, ix = half_snap(cx, &fx), iy = half_snap(cy, &fy), m = (int)ceilf(q * (on ? 4.8f : 1.6f)) + 2;
    on = on == SK_LED_RED ? SK_LED_RED : !!on;
    uint64_t key = 3ull << 60 | (uint64_t)(q * 4) << 8 | (uint64_t)on << 4 | (uint64_t)fx << 1 | (uint64_t)fy;
    spr_t *sp = spr_find(key);
    if (!sp) {
        led_ctx c = {m + fx * 0.5f, m + fy * 0.5f, q, on};
        sp = spr_make(key, 2 * m + 1, 2 * m + 1, led_shade, &c, 4);
    }
    spr_blit(sp, ix - m, iy - m);
}
